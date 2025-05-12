#include "xenocomm/core/security_manager.h"
#include "xenocomm/utils/logging.hpp"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <mutex>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/dh.h>
#include <openssl/ec.h>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <thread>

namespace xenocomm {
namespace core {

namespace {
    // Global OpenSSL initialization guard
    class OpenSSLGuard {
    public:
        OpenSSLGuard() {
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
        }
        ~OpenSSLGuard() {
            EVP_cleanup();
            ERR_free_strings();
        }
    };

    static std::once_flag sslInitFlag;
    static OpenSSLGuard* sslGuard = nullptr;

    void ensureSSLInitialized() {
        std::call_once(sslInitFlag, []() {
            sslGuard = new OpenSSLGuard();
        });
    }

    // Helper to get OpenSSL error string
    std::string getOpenSSLError() {
        std::stringstream ss;
        unsigned long err;
        while ((err = ERR_get_error()) != 0) {
            char buf[256];
            ERR_error_string_n(err, buf, sizeof(buf));
            ss << buf << "; ";
        }
        return ss.str();
    }

    // Convert our protocol enum to OpenSSL method
    const SSL_METHOD* getSSLMethod(EncryptionProtocol protocol, bool isServer) {
        switch (protocol) {
            case EncryptionProtocol::TLS_1_2:
                return isServer ? TLS_server_method() : TLS_client_method();
            case EncryptionProtocol::TLS_1_3:
                return isServer ? TLS_server_method() : TLS_client_method();
            case EncryptionProtocol::DTLS_1_2:
                return isServer ? DTLS_server_method() : DTLS_client_method();
            case EncryptionProtocol::DTLS_1_3:
                return isServer ? DTLS_server_method() : DTLS_client_method();
            default:
                return nullptr;
        }
    }

    // Convert our cipher suite enum to OpenSSL cipher string
    const char* getCipherString(CipherSuite suite) {
        switch (suite) {
            case CipherSuite::AES_128_GCM_SHA256:
                return "AES128-GCM-SHA256";
            case CipherSuite::AES_256_GCM_SHA384:
                return "AES256-GCM-SHA384";
            case CipherSuite::CHACHA20_POLY1305_SHA256:
                return "CHACHA20-POLY1305-SHA256";
            default:
                return nullptr;
        }
    }
}

// Pimpl struct for OpenSSL data
struct SecurityManager::SSLData {
    SSL_CTX* ctx = nullptr;
    std::mutex mutex;
    std::vector<SSL*> sslPool;
    EVP_PKEY* privateKey = nullptr;
    X509* certificate = nullptr;
    X509_STORE* trustStore = nullptr;
};

// Concrete implementation of SecureContext using OpenSSL
class OpenSSLContext : public SecureContext {
public:
    OpenSSLContext(SSL_CTX* ctx, bool isServer) 
        : ssl_(SSL_new(ctx)), isServer_(isServer) {
        if (!ssl_) {
            throw std::runtime_error("Failed to create SSL object: " + getOpenSSLError());
        }
    }

    ~OpenSSLContext() {
        if (ssl_) {
            SSL_free(ssl_);
        }
    }

    Result<void> handshake() override {
        if (!ssl_) {
            return Result<void>(std::string("SSL object not initialized"));
        }

        int result = isServer_ ? SSL_accept(ssl_) : SSL_connect(ssl_);
        if (result != 1) {
            int err = SSL_get_error(ssl_, result);
            return Result<void>(std::string("Handshake failed: ") + std::to_string(err) + 
                                     " - " + getOpenSSLError());
        }
        return Result<void>();
    }

    Result<void> shutdown() override {
        if (!ssl_) {
            return Result<void>(std::string("SSL object not initialized for shutdown."));
        }
        if (!SSL_is_init_finished(ssl_)) {
            return Result<void>(); 
        }

        int ret = SSL_shutdown(ssl_);
        if (ret < 0) {
            return Result<void>(std::string("SSL_shutdown failed: ") + getOpenSSLError());
        }
        if (ret == 0) {
             ret = SSL_shutdown(ssl_);
             if (ret <= 0) { 
                return Result<void>(std::string("SSL_shutdown did not complete: ") + getOpenSSLError());
             }
        }
        return Result<void>();
    }

    Result<std::vector<uint8_t>> encrypt(const std::vector<uint8_t>& data) override {
        if (!ssl_ || !SSL_is_init_finished(ssl_)) {
            return Result<std::vector<uint8_t>>(std::string("SSL not ready for encryption"));
        }

        std::vector<uint8_t> encrypted_output_buffer_for_BIO_read;
        int written = SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
        
        if (written <= 0) {
            int err = SSL_get_error(ssl_, written);
            return Result<std::vector<uint8_t>>(
                std::string("Encryption failed (SSL_write): ") + std::to_string(err) + " - " + getOpenSSLError());
        }

        std::vector<uint8_t> pseudo_encrypted_data = data;
        pseudo_encrypted_data.resize(written);
        return Result<std::vector<uint8_t>>(std::move(pseudo_encrypted_data));
    }

    Result<std::vector<uint8_t>> decrypt(const std::vector<uint8_t>& data) override {
        if (!ssl_ || !SSL_is_init_finished(ssl_)) {
            return Result<std::vector<uint8_t>>(std::string("SSL not ready for decryption"));
        }

        std::vector<uint8_t> decrypted(data.size());
        int read_len = SSL_read(ssl_, decrypted.data(), static_cast<int>(data.size()));
        
        if (read_len <= 0) {
            int err = SSL_get_error(ssl_, read_len);
            return Result<std::vector<uint8_t>>(
                std::string("Decryption failed (SSL_read): ") + std::to_string(err) + " - " + getOpenSSLError());
        }

        decrypted.resize(read_len);
        return Result<std::vector<uint8_t>>(std::move(decrypted));
    }

    bool isHandshakeComplete() const override {
        return ssl_ && SSL_is_init_finished(ssl_);
    }

    std::string getPeerCertificateInfo() const override {
        if (!ssl_) return "";

        X509* cert = SSL_get_peer_certificate(ssl_);
        if (!cert) return "";

        char subject[256];
        X509_NAME_oneline(X509_get_subject_name(cert), subject, sizeof(subject));
        X509_free(cert);
        return subject;
    }

    CipherSuite getNegotiatedCipherSuite() const override {
        if (!ssl_) return CipherSuite::AES_256_GCM_SHA384;

        const SSL_CIPHER* cipher_obj = SSL_get_current_cipher(ssl_);
        if (!cipher_obj) return CipherSuite::AES_256_GCM_SHA384;
        
        const char* cipher_name = SSL_CIPHER_get_name(cipher_obj);
        if (!cipher_name) return CipherSuite::AES_256_GCM_SHA384;

        if (strstr(cipher_name, "AES256-GCM")) {
            return CipherSuite::AES_256_GCM_SHA384;
        } else if (strstr(cipher_name, "AES128-GCM")) {
            return CipherSuite::AES_128_GCM_SHA256;
        } else if (strstr(cipher_name, "CHACHA20-POLY1305")) {
            return CipherSuite::CHACHA20_POLY1305_SHA256;
        }
        return CipherSuite::AES_256_GCM_SHA384;
    }

    bool isSelectiveEncryptionEnabled() const override {
        return selective_encryption_enabled_;
    }

    void setSelectiveEncryption(bool enable) override {
        selective_encryption_enabled_ = enable;
    }

    const SecurityMetrics& getMetrics() const override {
        return metrics_;
    }

    Result<void> doHandshakeStep() override {
        if (!ssl_) {
            return Result<void>(std::string("SSL object not initialized for handshake step"));
        }
        if (SSL_is_init_finished(ssl_)) {
            return Result<void>();
        }

        int ret = SSL_do_handshake(ssl_);
        if (ret == 1) {
            return Result<void>();
        }

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return Result<void>();
        } else {
            return Result<void>(std::string("Handshake step failed: ") + getOpenSSLError());
        }
    }

    std::string getNegotiatedProtocol() const override {
        if (!ssl_ || !SSL_is_init_finished(ssl_)) return "";
        const unsigned char *alpn_proto = nullptr;
        unsigned int alpn_len = 0;
        SSL_get0_alpn_selected(ssl_, &alpn_proto, &alpn_len);
        if (alpn_proto) {
            return std::string(reinterpret_cast<const char*>(alpn_proto), alpn_len);
        }
        return "";
    }

    std::string getCipherName() const override {
        if (!ssl_ || !SSL_is_init_finished(ssl_)) return "";
        const SSL_CIPHER* cipher_obj = SSL_get_current_cipher(ssl_);
        if (cipher_obj) {
            return SSL_CIPHER_get_name(cipher_obj);
        }
        return "";
    }

    int getKeySize() const override {
        if (!ssl_ || !SSL_is_init_finished(ssl_)) return 0;
        const SSL_CIPHER* cipher_obj = SSL_get_current_cipher(ssl_);
        if (cipher_obj) {
            int bits;
            SSL_CIPHER_get_bits(cipher_obj, &bits);
            return bits;
        }
        return 0;
    }

    Result<std::vector<uint8_t>> generateDTLSCookie() override {
        if (!isServer_ || !ssl_) {
            return Result<std::vector<uint8_t>>(std::string("Not in server mode or SSL not initialized for cookie generation"));
        }

        // OpenSSL doesn't provide a standard SSL_generate_cookie function
        // Instead, we'll generate a simple cookie based on connection info
        // In a real implementation, this would be more sophisticated with HMACs
        std::vector<uint8_t> cookie_data;
        
        // Get peer address if available
        BIO* bio = SSL_get_rbio(ssl_);
        if (!bio) {
            // No connection info available, generate a random cookie
            cookie_data.resize(32); // Use a reasonable size
            if (RAND_bytes(cookie_data.data(), static_cast<int>(cookie_data.size())) != 1) {
                return Result<std::vector<uint8_t>>(std::string("Failed to generate random DTLS cookie data"));
            }
            return Result<std::vector<uint8_t>>(std::move(cookie_data));
        }
        
        // Build a cookie with connection info + random data
        cookie_data.resize(32);
        if (RAND_bytes(cookie_data.data(), static_cast<int>(cookie_data.size())) != 1) {
            return Result<std::vector<uint8_t>>(std::string("Failed to generate random DTLS cookie data"));
        }
        
        return Result<std::vector<uint8_t>>(std::move(cookie_data));
    }

    bool verifyDTLSCookie(const std::vector<uint8_t>& cookie) override {
        (void)cookie; // Silence unused parameter warning
        if (!isServer_ || !ssl_) {
             return false;
        }
        return true; // Placeholder implementation
    }

private:
    SSL* ssl_;
    bool isServer_;
    bool selective_encryption_enabled_ = false;
    SecurityMetrics metrics_;
};

SecurityManager::SecurityManager(const SecurityConfig& config)
    : config_(config), sslData_(std::make_unique<SSLData>()) {
    auto configValidation = config.validate();
    if (configValidation) {
        throw std::runtime_error("Invalid security configuration: " + *configValidation);
    }
    
    if (auto result = initializeSSL(); result.has_error()) {
        throw std::runtime_error("Failed to initialize SSL: " + result.error());
    }
    
    if (auto result = loadCertificates(); result.has_error()) {
        throw std::runtime_error("Failed to load certificates: " + result.error());
    }
    
    initializeMonitoring();
    
    // Log configuration change event
    logSecurityEvent({
        SecurityEventType::CONFIG_CHANGE,
        std::chrono::system_clock::now(),
        "Security manager initialized with " + 
        std::string(config.level == SecurityLevel::HIGH ? "HIGH" :
                   config.level == SecurityLevel::MEDIUM ? "MEDIUM" :
                   config.level == SecurityLevel::LOW ? "LOW" : "CUSTOM") +
        " security level",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        false
    });
}

SecurityManager::~SecurityManager() {
    // The unique_ptr sslData_ will automatically clean up SSLData and its resources.
    // If SSLData itself needs more complex cleanup in its own destructor, that's handled there.
    // Additional cleanup specific to SecurityManager (not SSLData) could go here.
    cleanupMonitoring(); // Example: if monitoring has resources not tied to sslData_
}

Result<void> SecurityManager::updateConfig(const SecurityConfig& newConfig) {
    auto configValidation = newConfig.validate();
    if (configValidation) {
        return Result<void>("Invalid security configuration: " + *configValidation);
    }
    
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // Store old config for comparison
    auto oldConfig = config_;
    config_ = newConfig;
    
    // Reinitialize SSL if necessary security parameters changed
    if (oldConfig.protocol != newConfig.protocol ||
        oldConfig.allowedCipherSuites != newConfig.allowedCipherSuites ||
        oldConfig.certificatePath != newConfig.certificatePath ||
        oldConfig.privateKeyPath != newConfig.privateKeyPath ||
        oldConfig.trustedCAsPath != newConfig.trustedCAsPath) {
        
        cleanupSSL();
        if (auto result = initializeSSL(); result.has_error()) {
            return result;
        }
        if (auto result = loadCertificates(); result.has_error()) {
            return result;
        }
    }
    
    // Update monitoring configuration if changed
    if (oldConfig.monitoring != newConfig.monitoring) {
        cleanupMonitoring();
        initializeMonitoring();
    }
    
    // Log configuration change
    logSecurityEvent({
        SecurityEventType::CONFIG_CHANGE,
        std::chrono::system_clock::now(),
        "Security configuration updated",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        false
    });
    
    return Result<void>();
}

Result<std::shared_ptr<SecureContext>> SecurityManager::createContext(bool isServer) {
    std::lock_guard<std::mutex> lock(sslData_->mutex);
    if (!sslData_ || !sslData_->ctx) {
        return Result<std::shared_ptr<SecureContext>>("SSL context not initialized in SecurityManager");
    }
    try {
        auto context = std::make_shared<OpenSSLContext>(sslData_->ctx, isServer);
        // Add to connection pool if pooling is enabled & configured
        if (config_.connectionPool.enabled) {
            std::lock_guard<std::mutex> pool_lock(poolMutex_);
            if (connectionPool_.size() < config_.connectionPool.maxPoolSize) {
                connectionPool_.push_back(context);
                 // Update metrics accordingly
                metrics_.currentConnections++;
                if (metrics_.currentConnections > metrics_.peakConnections) {
                    metrics_.peakConnections = metrics_.currentConnections.load();
                }
            } else {
                // Pool is full, perhaps log or return a specific error if strict
                // For now, just don't add to pool but still return the context
            }
        }
        return Result<std::shared_ptr<SecureContext>>(context);
    } catch (const std::exception& e) {
        return Result<std::shared_ptr<SecureContext>>(std::string("Failed to create OpenSSLContext: ") + e.what());
    }
}

void SecurityManager::resetMetrics() {
    metrics_.totalEncryptionOps = 0;
    metrics_.totalDecryptionOps = 0;
    metrics_.totalHandshakes = 0;
    metrics_.totalAuthAttempts = 0;
    metrics_.totalAuthCacheHits = 0;
    metrics_.totalBytesEncrypted = 0;
    metrics_.totalBytesDecrypted = 0;
    metrics_.totalHandshakeTime = 0;
    metrics_.totalEncryptionTime = 0;
    metrics_.totalDecryptionTime = 0;
    metrics_.peakEncryptionLatency = 0;
    metrics_.peakDecryptionLatency = 0;
    // currentConnections should naturally go to 0 as contexts are destroyed or removed from pool
    // peakConnections remains as a high-water mark, not typically reset by a simple metrics reset.
    // If peakConnections should also be reset, add: metrics_.peakConnections = metrics_.currentConnections.load();
}

std::pair<size_t, size_t> SecurityManager::getConnectionPoolStatus() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return {connectionPool_.size(), config_.connectionPool.maxPoolSize};
}

std::pair<size_t, float> SecurityManager::getAuthCacheStats() const {
    std::lock_guard<std::mutex> lock(authCacheMutex_);
    float hitRate = metrics_.totalAuthAttempts.load() > 0
        ? static_cast<float>(metrics_.totalAuthCacheHits.load()) / metrics_.totalAuthAttempts.load()
        : 0.0f;
    return {authCache_.size(), hitRate};
}

std::vector<SecurityEvent> SecurityManager::getSecurityEvents(
    size_t maxEvents,
    std::optional<SecurityEventType> filterType) const {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    
    std::vector<SecurityEvent> events;
    events.reserve(std::min(maxEvents, securityEvents_.size()));
    
    for (auto it = securityEvents_.rbegin(); 
         it != securityEvents_.rend() && events.size() < maxEvents; 
         ++it) {
        if (!filterType || it->type == *filterType) {
            events.push_back(*it);
        }
    }
    
    return events;
}

void SecurityManager::logSecurityEvent(const SecurityEvent& event) {
    if (!config_.monitoring.enableSecurityEvents) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(eventsMutex_);
    
    // Add event to in-memory buffer
    securityEvents_.push_back(event);
    
    // Trim buffer if it gets too large
    while (securityEvents_.size() > 1000) {
        securityEvents_.erase(securityEvents_.begin());
    }
    
    // Write to log file if enabled
    if (config_.monitoring.enableAuditLog) {
        static std::mutex logMutex;
        std::lock_guard<std::mutex> logLock(logMutex);
        
        std::filesystem::path logPath = "security.log";
        std::ofstream logFile(logPath, std::ios::app);
        
        if (logFile) {
            auto now = std::chrono::system_clock::to_time_t(event.timestamp);
            logFile << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " | ";
            logFile << std::setw(20) << std::left;
            
            switch (event.type) {
                case SecurityEventType::HANDSHAKE_START: logFile << "HANDSHAKE_START"; break;
                case SecurityEventType::HANDSHAKE_COMPLETE: logFile << "HANDSHAKE_COMPLETE"; break;
                case SecurityEventType::HANDSHAKE_FAILED: logFile << "HANDSHAKE_FAILED"; break;
                case SecurityEventType::AUTH_SUCCESS: logFile << "AUTH_SUCCESS"; break;
                case SecurityEventType::AUTH_FAILURE: logFile << "AUTH_FAILURE"; break;
                case SecurityEventType::CERT_VALIDATION_SUCCESS: logFile << "CERT_VALIDATION_SUCCESS"; break;
                case SecurityEventType::CERT_VALIDATION_FAILURE: logFile << "CERT_VALIDATION_FAILURE"; break;
                case SecurityEventType::KEY_ROTATION: logFile << "KEY_ROTATION"; break;
                case SecurityEventType::CONFIG_CHANGE: logFile << "CONFIG_CHANGE"; break;
                case SecurityEventType::SECURITY_VIOLATION: logFile << "SECURITY_VIOLATION"; break;
            }
            
            logFile << " | ";
            
            // Mask sensitive data if configured
            std::string description = event.description;
            if (config_.monitoring.maskSensitiveData && event.isSensitive) {
                description = "[REDACTED]";
            }
            
            logFile << description;
            
            if (event.sourceIp) {
                logFile << " | IP: " << *event.sourceIp;
            }
            if (event.username) {
                logFile << " | User: " << *event.username;
            }
            if (event.certificateSubject) {
                logFile << " | Cert: " << *event.certificateSubject;
            }
            
            logFile << std::endl;
        }
        
        // Rotate log file if needed
        if (std::filesystem::exists(logPath) && 
            std::filesystem::file_size(logPath) > config_.monitoring.maxLogSize) {
            
            // Rotate existing log files
            for (size_t i = config_.monitoring.maxLogFiles - 1; i >= 1; --i) {
                std::filesystem::path oldPath = logPath.string() + "." + std::to_string(i);
                std::filesystem::path newPath = logPath.string() + "." + std::to_string(i + 1);
                
                if (std::filesystem::exists(oldPath)) {
                    if (i == config_.monitoring.maxLogFiles - 1) {
                        std::filesystem::remove(oldPath);
                    } else {
                        std::filesystem::rename(oldPath, newPath);
                    }
                }
            }
            
            // Rename current log file
            std::filesystem::rename(logPath, logPath.string() + ".1");
        }
    }
}

void SecurityManager::updateMetrics(
    const std::string& operation,
    size_t bytes,
    std::chrono::microseconds duration) {
    
    if (!config_.monitoring.enablePerformanceMetrics) {
        return;
    }
    
    if (operation == "encrypt") {
        metrics_.totalEncryptionOps++;
        metrics_.totalBytesEncrypted += bytes;
        metrics_.totalEncryptionTime += duration.count();
        metrics_.peakEncryptionLatency = std::max(
            metrics_.peakEncryptionLatency.load(),
            static_cast<uint64_t>(duration.count()));
    }
    else if (operation == "decrypt") {
        metrics_.totalDecryptionOps++;
        metrics_.totalBytesDecrypted += bytes;
        metrics_.totalDecryptionTime += duration.count();
        metrics_.peakDecryptionLatency = std::max(
            metrics_.peakDecryptionLatency.load(),
            static_cast<uint64_t>(duration.count()));
    }
}

void SecurityManager::initializeMonitoring() {
    if (!config_.monitoring.enablePerformanceMetrics &&
        !config_.monitoring.enableSecurityEvents) {
        return;
    }
    
    // Clear existing metrics and events
    resetMetrics();
    securityEvents_.clear();
    
    // Initialize log file if needed
    if (config_.monitoring.enableAuditLog) {
        std::filesystem::path logPath = "security.log";
        std::ofstream logFile(logPath, std::ios::app);
        if (logFile) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            logFile << "\n--- Security log initialized at " 
                   << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S")
                   << " ---\n\n";
        }
    }
}

void SecurityManager::cleanupMonitoring() {
    // Flush any remaining events to log
    if (config_.monitoring.enableAuditLog && !securityEvents_.empty()) {
        std::filesystem::path logPath = "security.log";
        std::ofstream logFile(logPath, std::ios::app);
        if (logFile) {
            auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            logFile << "\n--- Security log closed at "
                   << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S")
                   << " ---\n\n";
        }
    }
}

Result<void> SecurityManager::validateConfig(const SecurityConfig& config_to_validate) {
    std::optional<std::string> validation_error = config_to_validate.validate();
    if (validation_error.has_value()) {
        return Result<void>(validation_error.value());
    }
    return Result<void>();
}

Result<void> SecurityManager::validatePeerCertificate(const std::vector<uint8_t>& certData) {
    ensureSSLInitialized();
    if (!sslData_ || !sslData_->ctx) {
        return Result<void>("SSL context not initialized for certificate validation");
    }

    BIO* bio = BIO_new_mem_buf(certData.data(), static_cast<int>(certData.size()));
    if (!bio) {
        return Result<void>("Failed to create BIO for certificate: " + getOpenSSLError());
    }
    // Auto-free BIO
    std::unique_ptr<BIO, decltype(&BIO_free_all)> bio_ptr(bio, BIO_free_all);

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    if (!cert) {
        // Try DER format if PEM fails
        BIO_reset(bio);
        cert = d2i_X509_bio(bio, nullptr);
    }

    if (!cert) {
        return Result<void>("Failed to parse certificate (PEM/DER): " + getOpenSSLError());
    }
    // Auto-free X509
    std::unique_ptr<X509, decltype(&X509_free)> cert_ptr(cert, X509_free);

    X509_STORE* store = SSL_CTX_get_cert_store(sslData_->ctx);
    if (!store) {
        return Result<void>("No certificate store available in SSL_CTX");
    }

    X509_STORE_CTX* storeCtx = X509_STORE_CTX_new();
    if (!storeCtx) {
        return Result<void>("Failed to create X509_STORE_CTX: " + getOpenSSLError());
    }
    // Auto-free X509_STORE_CTX
    std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> storeCtx_ptr(storeCtx, X509_STORE_CTX_free);

    if (X509_STORE_CTX_init(storeCtx, store, cert, nullptr) != 1) {
        return Result<void>("Failed to initialize X509_STORE_CTX: " + getOpenSSLError());
    }

    // TODO: Add CRL checks, etc., if configured
    // X509_STORE_CTX_set_flags(storeCtx, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
    // X509_STORE_set_lookup_crls(store, X509_LOOKUP_file()); // Or other lookup methods

    int result = X509_verify_cert(storeCtx);
    if (result != 1) {
        return Result<void>("Certificate validation failed: " + 
                           std::string(X509_verify_cert_error_string(X509_STORE_CTX_get_error(storeCtx))) +
                           " (" + getOpenSSLError() + ")");
    }

    return Result<void>();
}

Result<void> SecurityManager::generateSelfSignedCert(
    const std::string& commonName, int validityDays) {
    
    ensureSSLInitialized();

    // Generate key pair
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) {
        return Result<void>(std::string("Failed to create key structure"));
    }

    RSA* rsa = RSA_new();
    BIGNUM* bn = BN_new();
    if (!bn || !BN_set_word(bn, RSA_F4) || !RSA_generate_key_ex(rsa, 2048, bn, nullptr)) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        BN_free(bn);
        return Result<void>(std::string("Failed to generate RSA key"));
    }

    if (!EVP_PKEY_assign_RSA(pkey, rsa)) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        BN_free(bn);
        return Result<void>(std::string("Failed to assign RSA key"));
    }
    BN_free(bn);

    // Create X509 certificate
    X509* x509 = X509_new();
    if (!x509) {
        EVP_PKEY_free(pkey);
        return Result<void>(std::string("Failed to create X509 structure"));
    }

    // Set version and serial number
    X509_set_version(x509, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

    // Set validity period
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), static_cast<long>(60 * 60 * 24 * validityDays));

    // Set subject and issuer
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(commonName.c_str()), -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Set public key and sign
    X509_set_pubkey(x509, pkey);
    X509_sign(x509, pkey, EVP_sha256());

    // Save to files
    FILE* f = fopen(config_.certificatePath.c_str(), "wb");
    if (!f) {
        EVP_PKEY_free(pkey);
        X509_free(x509);
        return Result<void>(std::string("Failed to open certificate file for writing"));
    }
    PEM_write_X509(f, x509);
    fclose(f);

    f = fopen(config_.privateKeyPath.c_str(), "wb");
    if (!f) {
        EVP_PKEY_free(pkey);
        X509_free(x509);
        return Result<void>(std::string("Failed to open private key file for writing"));
    }
    PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(f);

    EVP_PKEY_free(pkey);
    X509_free(x509);

    return Result<void>();
}

Result<void> SecurityManager::initializeSSL() {
    ensureSSLInitialized();

    std::lock_guard<std::mutex> lock(sslData_->mutex);

    // Clean up existing context if any
    if (sslData_->ctx) {
        SSL_CTX_free(sslData_->ctx);
        sslData_->ctx = nullptr;
    }

    // Create new context
    const SSL_METHOD* method = getSSLMethod(config_.protocol, true);
    if (!method) {
        return Result<void>(std::string("Unsupported protocol"));
    }

    sslData_->ctx = SSL_CTX_new(method);
    if (!sslData_->ctx) {
        return Result<void>(std::string("Failed to create SSL context: ") + getOpenSSLError());
    }

    // Set minimum protocol version
    switch (config_.protocol) {
        case EncryptionProtocol::TLS_1_2:
            SSL_CTX_set_min_proto_version(sslData_->ctx, TLS1_2_VERSION);
            break;
        case EncryptionProtocol::TLS_1_3:
            SSL_CTX_set_min_proto_version(sslData_->ctx, TLS1_3_VERSION);
            break;
        case EncryptionProtocol::DTLS_1_2:
            SSL_CTX_set_min_proto_version(sslData_->ctx, DTLS1_2_VERSION);
            break;
        case EncryptionProtocol::DTLS_1_3:
            SSL_CTX_set_min_proto_version(sslData_->ctx, DTLS1_2_VERSION); // DTLS 1.3 not yet widely supported
            break;
    }

    // Set cipher suites
    std::string cipherList;
    for (const auto& suite : config_.allowedCipherSuites) {
        const char* cipherStr = getCipherString(suite);
        if (cipherStr) {
            if (!cipherList.empty()) cipherList += ":";
            cipherList += cipherStr;
        }
    }
    
    if (!SSL_CTX_set_cipher_list(sslData_->ctx, cipherList.c_str())) {
        return Result<void>(std::string("Failed to set cipher list: ") + getOpenSSLError());
    }

    // Load certificates if paths are provided
    if (!config_.certificatePath.empty() && !config_.privateKeyPath.empty()) {
        if (SSL_CTX_use_certificate_file(sslData_->ctx, config_.certificatePath.c_str(), 
                                       SSL_FILETYPE_PEM) != 1) {
            return Result<void>(std::string("Failed to load certificate: ") + getOpenSSLError());
        }

        if (SSL_CTX_use_PrivateKey_file(sslData_->ctx, config_.privateKeyPath.c_str(), 
                                       SSL_FILETYPE_PEM) != 1) {
            return Result<void>(std::string("Failed to load private key: ") + getOpenSSLError());
        }

        if (SSL_CTX_check_private_key(sslData_->ctx) != 1) {
            return Result<void>(std::string("Private key does not match certificate: ") + getOpenSSLError());
        }
    }

    // Load CA certificate if provided
    if (!config_.trustedCAsPath.empty()) {
        if (SSL_CTX_load_verify_locations(sslData_->ctx, config_.trustedCAsPath.c_str(), nullptr) != 1) {
            return Result<void>(std::string("Failed to load CA certificate: ") + getOpenSSLError());
        }
    }

    // Configure peer verification
    SSL_CTX_set_verify(sslData_->ctx, 
        config_.verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, 
        nullptr);

    if (config_.allowSelfSigned) {
        SSL_CTX_set_verify_depth(sslData_->ctx, 1);
    }

    return Result<void>();
}

Result<void> SecurityManager::loadCertificates() {
    return initializeSSL();
}

void SecurityManager::cleanupSSL() {
    std::lock_guard<std::mutex> lock(sslData_->mutex);
    if (sslData_->ctx) {
        SSL_CTX_free(sslData_->ctx);
        sslData_->ctx = nullptr;
    }
}

Result<std::vector<uint8_t>> SecurityManager::generateHmac(const std::vector<uint8_t>& data) {
    if (hmac_key_.empty()) {
        // Simplified key generation for example, should be more robust
        hmac_key_.resize(32); // SHA256_DIGEST_LENGTH or similar
        if (RAND_bytes(hmac_key_.data(), static_cast<int>(hmac_key_.size())) != 1) {
            return Result<std::vector<uint8_t>>(std::string("Failed to generate HMAC key: ") + getOpenSSLError());
        }
    }
    std::vector<uint8_t> hmac_result(EVP_MAX_MD_SIZE);
    unsigned int hmac_len;
    if (!HMAC(EVP_sha256(), hmac_key_.data(), static_cast<int>(hmac_key_.size()),
              data.data(), data.size(), hmac_result.data(), &hmac_len)) {
        return Result<std::vector<uint8_t>>(std::string("Failed to generate HMAC: ") + getOpenSSLError());
    }
    hmac_result.resize(hmac_len);
    return Result<std::vector<uint8_t>>(hmac_result);
}

Result<std::vector<uint8_t>> SecurityManager::generateDtlsCookie(const NetworkAddress& client) {
    std::vector<uint8_t> client_data;
    // Serialize client address and potentially other info into client_data
    // Example: append IP and port
    uint32_t ip_addr_net = inet_addr(client.ip.c_str());
    client_data.insert(client_data.end(), reinterpret_cast<uint8_t*>(&ip_addr_net), reinterpret_cast<uint8_t*>(&ip_addr_net) + sizeof(ip_addr_net));
    uint16_t port_net = htons(client.port);
    client_data.insert(client_data.end(), reinterpret_cast<uint8_t*>(&port_net), reinterpret_cast<uint8_t*>(&port_net) + sizeof(port_net));

    auto hmacResult = generateHmac(client_data);
    if (hmacResult.has_error()) {
        return Result<std::vector<uint8_t>>(hmacResult.error());
    }

    std::vector<uint8_t> cookie = hmacResult.value(); // The HMAC itself can serve as the cookie
    // Or prepend a timestamp or other data to the cookie if needed before HMACing.
    // For DTLS 1.2, SSL_generate_cookie is an option if SSL object is available.
    // For a manager-level cookie, HMAC is a common approach.
    return Result<std::vector<uint8_t>>(cookie);
}

Result<void> SecurityManager::verifyDtlsCookie(const std::vector<uint8_t>& cookie, const NetworkAddress& source) {
     // This verification must match how generateDtlsCookie created it.
     // If generateDtlsCookie just returns HMAC of client data:
    std::vector<uint8_t> client_data;
    uint32_t ip_addr_net = inet_addr(source.ip.c_str());
    client_data.insert(client_data.end(), reinterpret_cast<uint8_t*>(&ip_addr_net), reinterpret_cast<uint8_t*>(&ip_addr_net) + sizeof(ip_addr_net));
    uint16_t port_net = htons(source.port);
    client_data.insert(client_data.end(), reinterpret_cast<uint8_t*>(&port_net), reinterpret_cast<uint8_t*>(&port_net) + sizeof(port_net));

    auto expectedHmacResult = generateHmac(client_data);
    if (expectedHmacResult.has_error()) {
        return Result<void>("Failed to generate expected HMAC for cookie verification: " + expectedHmacResult.error());
    }

    if (cookie.size() != expectedHmacResult.value().size() ||
        CRYPTO_memcmp(cookie.data(), expectedHmacResult.value().data(), cookie.size()) != 0) {
        return Result<void>("DTLS cookie verification failed (HMAC mismatch)");
    }
    return Result<void>();
}

} // namespace core
} // namespace xenocomm 
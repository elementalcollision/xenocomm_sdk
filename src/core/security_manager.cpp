#include "xenocomm/core/security_manager.h"
#include "xenocomm/utils/logging.h"
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
            return Result<void>::Error("SSL object not initialized");
        }

        int result = isServer_ ? SSL_accept(ssl_) : SSL_connect(ssl_);
        if (result != 1) {
            int err = SSL_get_error(ssl_, result);
            return Result<void>::Error("Handshake failed: " + std::to_string(err) + 
                                     " - " + getOpenSSLError());
        }

        return Result<void>::Success();
    }

    Result<std::vector<uint8_t>> encrypt(const std::vector<uint8_t>& data) override {
        if (!ssl_ || !SSL_is_init_finished(ssl_)) {
            return Result<std::vector<uint8_t>>::Error("SSL not ready for encryption");
        }

        std::vector<uint8_t> encrypted(data.size() + SSL_MAX_BLOCK_SIZE);
        int written = SSL_write(ssl_, data.data(), data.size());
        
        if (written <= 0) {
            int err = SSL_get_error(ssl_, written);
            return Result<std::vector<uint8_t>>::Error(
                "Encryption failed: " + std::to_string(err) + " - " + getOpenSSLError());
        }

        encrypted.resize(written);
        return Result<std::vector<uint8_t>>::Success(std::move(encrypted));
    }

    Result<std::vector<uint8_t>> decrypt(const std::vector<uint8_t>& data) override {
        if (!ssl_ || !SSL_is_init_finished(ssl_)) {
            return Result<std::vector<uint8_t>>::Error("SSL not ready for decryption");
        }

        std::vector<uint8_t> decrypted(data.size());
        int read = SSL_read(ssl_, decrypted.data(), data.size());
        
        if (read <= 0) {
            int err = SSL_get_error(ssl_, read);
            return Result<std::vector<uint8_t>>::Error(
                "Decryption failed: " + std::to_string(err) + " - " + getOpenSSLError());
        }

        decrypted.resize(read);
        return Result<std::vector<uint8_t>>::Success(std::move(decrypted));
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
        if (!ssl_) return CipherSuite::AES_256_GCM_SHA384; // Default

        const char* cipher = SSL_get_cipher(ssl_);
        if (strstr(cipher, "AES256-GCM")) {
            return CipherSuite::AES_256_GCM_SHA384;
        } else if (strstr(cipher, "AES128-GCM")) {
            return CipherSuite::AES_128_GCM_SHA256;
        } else if (strstr(cipher, "CHACHA20-POLY1305")) {
            return CipherSuite::CHACHA20_POLY1305_SHA256;
        }
        return CipherSuite::AES_256_GCM_SHA384; // Default
    }

private:
    SSL* ssl_;
    bool isServer_;
};

SecurityManager::SecurityManager(const SecurityConfig& config)
    : config_(config), sslData_(std::make_unique<SSLData>()) {
    auto configValidation = config.validate();
    if (configValidation) {
        throw std::runtime_error("Invalid security configuration: " + *configValidation);
    }
    
    if (auto result = initializeSSL(); !result) {
        throw std::runtime_error("Failed to initialize SSL: " + result.error());
    }
    
    if (auto result = loadCertificates(); !result) {
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

Result<void> SecurityManager::updateConfig(const SecurityConfig& newConfig) {
    auto configValidation = newConfig.validate();
    if (configValidation) {
        return Error("Invalid security configuration: " + *configValidation);
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
        if (auto result = initializeSSL(); !result) {
            return result;
        }
        if (auto result = loadCertificates(); !result) {
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
    
    return Success();
}

Result<std::shared_ptr<SecureContext>> SecurityManager::createContext(bool isServer) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    // Try to reuse a connection from the pool if enabled
    if (config_.connectionPool.enabled && !connectionPool_.empty()) {
        auto context = connectionPool_.back();
        connectionPool_.pop_back();
        metrics_.currentConnections++;
        metrics_.peakConnections = std::max(metrics_.peakConnections.load(),
                                          metrics_.currentConnections.load());
        return context;
    }
    
    // Create new context
    auto ssl = SSL_new(sslData_->ctx);
    if (!ssl) {
        return Error("Failed to create SSL context");
    }
    
    if (isServer) {
        SSL_set_accept_state(ssl);
    } else {
        SSL_set_connect_state(ssl);
    }
    
    // Configure context based on settings
    if (config_.enableSelectiveEncryption) {
        // Enable selective encryption if supported by OpenSSL version
        #if OPENSSL_VERSION_NUMBER >= 0x1010100fL
        SSL_set_mode(ssl, SSL_MODE_SEND_FALLBACK_SCSV);
        #endif
    }
    
    // Create and return secure context
    auto context = std::make_shared<OpenSSLContext>(ssl, isServer);
    metrics_.currentConnections++;
    metrics_.peakConnections = std::max(metrics_.peakConnections.load(),
                                      metrics_.currentConnections.load());
    
    // Log context creation
    logSecurityEvent({
        SecurityEventType::HANDSHAKE_START,
        std::chrono::system_clock::now(),
        std::string("New secure context created (") + (isServer ? "server" : "client") + ")",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        false
    });
    
    return context;
}

void SecurityManager::resetMetrics() {
    metrics_ = SecurityMetrics{};
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
            for (int i = config_.monitoring.maxLogFiles - 1; i >= 1; --i) {
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
    metrics_ = SecurityMetrics{};
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

Result<void> SecurityManager::validateConfig(const SecurityConfig& config) {
    return config.validate().transform_error([](const std::string& err) {
        return Error(err);
    });
}

Result<void> SecurityManager::validatePeerCertificate(const std::vector<uint8_t>& certData) {
    std::lock_guard<std::mutex> lock(sslData_->mutex);
    
    BIO* bio = BIO_new_mem_buf(certData.data(), certData.size());
    if (!bio) {
        return Result<void>::Error("Failed to create BIO: " + getOpenSSLError());
    }

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!cert) {
        return Result<void>::Error("Failed to parse certificate: " + getOpenSSLError());
    }

    X509_STORE* store = SSL_CTX_get_cert_store(sslData_->ctx);
    if (!store) {
        X509_free(cert);
        return Result<void>::Error("No certificate store available");
    }

    X509_STORE_CTX* storeCtx = X509_STORE_CTX_new();
    if (!storeCtx) {
        X509_free(cert);
        return Result<void>::Error("Failed to create store context: " + getOpenSSLError());
    }

    if (X509_STORE_CTX_init(storeCtx, store, cert, nullptr) != 1) {
        X509_STORE_CTX_free(storeCtx);
        X509_free(cert);
        return Result<void>::Error("Failed to initialize store context: " + getOpenSSLError());
    }

    int result = X509_verify_cert(storeCtx);
    X509_STORE_CTX_free(storeCtx);
    X509_free(cert);

    if (result != 1) {
        return Result<void>::Error("Certificate validation failed: " + getOpenSSLError());
    }

    return Result<void>::Success();
}

Result<void> SecurityManager::generateSelfSignedCert(
    const std::string& commonName, int validityDays) {
    
    std::lock_guard<std::mutex> lock(sslData_->mutex);

    // Generate key pair
    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey) {
        return Result<void>::Error("Failed to create key structure");
    }

    RSA* rsa = RSA_new();
    BIGNUM* bn = BN_new();
    if (!bn || !BN_set_word(bn, RSA_F4) || !RSA_generate_key_ex(rsa, 2048, bn, nullptr)) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        BN_free(bn);
        return Result<void>::Error("Failed to generate RSA key");
    }

    if (!EVP_PKEY_assign_RSA(pkey, rsa)) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        BN_free(bn);
        return Result<void>::Error("Failed to assign RSA key");
    }
    BN_free(bn);

    // Create X509 certificate
    X509* x509 = X509_new();
    if (!x509) {
        EVP_PKEY_free(pkey);
        return Result<void>::Error("Failed to create X509 structure");
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
        return Result<void>::Error("Failed to open certificate file for writing");
    }
    PEM_write_X509(f, x509);
    fclose(f);

    f = fopen(config_.privateKeyPath.c_str(), "wb");
    if (!f) {
        EVP_PKEY_free(pkey);
        X509_free(x509);
        return Result<void>::Error("Failed to open private key file for writing");
    }
    PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(f);

    EVP_PKEY_free(pkey);
    X509_free(x509);

    return Result<void>::Success();
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
        return Result<void>::Error("Unsupported protocol");
    }

    sslData_->ctx = SSL_CTX_new(method);
    if (!sslData_->ctx) {
        return Result<void>::Error("Failed to create SSL context: " + getOpenSSLError());
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
        return Result<void>::Error("Failed to set cipher list: " + getOpenSSLError());
    }

    // Load certificates if paths are provided
    if (!config_.certificatePath.empty() && !config_.privateKeyPath.empty()) {
        if (SSL_CTX_use_certificate_file(sslData_->ctx, config_.certificatePath.c_str(), 
                                       SSL_FILETYPE_PEM) != 1) {
            return Result<void>::Error("Failed to load certificate: " + getOpenSSLError());
        }

        if (SSL_CTX_use_PrivateKey_file(sslData_->ctx, config_.privateKeyPath.c_str(), 
                                       SSL_FILETYPE_PEM) != 1) {
            return Result<void>::Error("Failed to load private key: " + getOpenSSLError());
        }

        if (SSL_CTX_check_private_key(sslData_->ctx) != 1) {
            return Result<void>::Error("Private key does not match certificate: " + getOpenSSLError());
        }
    }

    // Load CA certificate if provided
    if (!config_.caPath.empty()) {
        if (SSL_CTX_load_verify_locations(sslData_->ctx, config_.caPath.c_str(), nullptr) != 1) {
            return Result<void>::Error("Failed to load CA certificate: " + getOpenSSLError());
        }
    }

    // Configure peer verification
    SSL_CTX_set_verify(sslData_->ctx, 
        config_.verifyPeer ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, 
        nullptr);

    if (config_.allowSelfSigned) {
        SSL_CTX_set_verify_depth(sslData_->ctx, 1);
    }

    return Result<void>::Success();
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
    unsigned int hmacLen;
    std::vector<uint8_t> hmac(EVP_MAX_MD_SIZE);
    
    if (!HMAC(EVP_sha256(), hmac_key_.data(), hmac_key_.size(),
              data.data(), data.size(), hmac.data(), &hmacLen)) {
        return Result<std::vector<uint8_t>>::Error("Failed to generate HMAC: " + getOpenSSLError());
    }
    
    hmac.resize(hmacLen);
    return Result<std::vector<uint8_t>>::Ok(hmac);
}

Result<std::vector<uint8_t>> SecurityManager::generateDtlsCookie(const NetworkAddress& client) {
    // Initialize HMAC key if not already done
    if (hmac_key_.empty()) {
        hmac_key_.resize(32); // 256 bits
        if (RAND_bytes(hmac_key_.data(), hmac_key_.size()) != 1) {
            return Result<std::vector<uint8_t>>::Error("Failed to generate HMAC key: " + getOpenSSLError());
        }
    }

    // Serialize client data with timestamp
    std::vector<uint8_t> clientData = client.serialize();
    
    // Generate HMAC
    auto hmacResult = generateHmac(clientData);
    if (!hmacResult) {
        return Result<std::vector<uint8_t>>::Error(hmacResult.error());
    }

    // Combine timestamp and HMAC for the final cookie
    std::vector<uint8_t> cookie;
    cookie.reserve(4 + hmacResult.value().size());
    
    // Add timestamp
    uint32_t timestamp = client.timestamp;
    for (int i = 0; i < 4; i++) {
        cookie.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
    }
    
    // Add HMAC
    cookie.insert(cookie.end(), hmacResult.value().begin(), hmacResult.value().end());
    
    return Result<std::vector<uint8_t>>::Ok(cookie);
}

Result<void> SecurityManager::verifyDtlsCookie(const std::vector<uint8_t>& cookie, const NetworkAddress& source) {
    if (cookie.size() < 4 + EVP_MD_SIZE) {
        return Result<void>::Error("Invalid cookie size");
    }

    // Extract timestamp from cookie
    uint32_t cookieTimestamp = 0;
    for (int i = 0; i < 4; i++) {
        cookieTimestamp |= static_cast<uint32_t>(cookie[i]) << (i * 8);
    }

    // Check if cookie has expired
    uint32_t currentTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (currentTime - cookieTimestamp > cookie_lifetime_.count()) {
        return Result<void>::Error("Cookie has expired");
    }

    // Create verification data
    NetworkAddress verifyAddr(source.ip, source.port);
    verifyAddr.timestamp = cookieTimestamp;
    std::vector<uint8_t> verifyData = verifyAddr.serialize();

    // Generate HMAC for verification
    auto hmacResult = generateHmac(verifyData);
    if (!hmacResult) {
        return Result<void>::Error(hmacResult.error());
    }

    // Extract HMAC from cookie
    std::vector<uint8_t> cookieHmac(cookie.begin() + 4, cookie.end());

    // Compare HMACs in constant time
    if (cookieHmac.size() != hmacResult.value().size() ||
        CRYPTO_memcmp(cookieHmac.data(), hmacResult.value().data(), cookieHmac.size()) != 0) {
        return Result<void>::Error("Cookie verification failed");
    }

    return Result<void>::Ok();
}

} // namespace core
} // namespace xenocomm 
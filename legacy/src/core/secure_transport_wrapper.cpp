#include "xenocomm/core/secure_transport_wrapper.hpp"
#include "xenocomm/utils/logging.hpp"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>

namespace xenocomm {
namespace core {

namespace {
    // Custom BIO method callbacks
    int bio_write(BIO* bio, const char* data, int len) {
        auto* wrapper = static_cast<SecureTransportWrapper*>(BIO_get_data(bio));
        if (!wrapper || !data || len <= 0) return -1;

        auto transport = wrapper->getTransport();
        if (!transport) return -1;

        ssize_t written = transport->send(reinterpret_cast<const uint8_t*>(data), len);
        BIO_clear_retry_flags(bio);
        
        if (written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                BIO_set_retry_write(bio);
            }
            return -1;
        }
        
        return static_cast<int>(written);
    }

    int bio_read(BIO* bio, char* data, int len) {
        auto* wrapper = static_cast<SecureTransportWrapper*>(BIO_get_data(bio));
        if (!wrapper || !data || len <= 0) return -1;

        auto transport = wrapper->getTransport();
        if (!transport) return -1;

        ssize_t read = transport->receive(reinterpret_cast<uint8_t*>(data), len);
        BIO_clear_retry_flags(bio);
        
        if (read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                BIO_set_retry_read(bio);
            }
            return -1;
        } else if (read == 0) {
            return 0;  // EOF
        }
        
        return static_cast<int>(read);
    }

    long bio_ctrl(BIO* bio, int cmd, long num, void* ptr) {
        switch (cmd) {
            case BIO_CTRL_FLUSH:
                return 1;
            case BIO_CTRL_PUSH:
            case BIO_CTRL_POP:
                return 0;
            default:
                return 0;
        }
    }

    int bio_create(BIO* bio) {
        BIO_set_init(bio, 1);
        BIO_set_data(bio, nullptr);
        BIO_set_shutdown(bio, 0);
        return 1;
    }

    int bio_destroy(BIO* bio) {
        if (!bio) return 0;
        BIO_set_data(bio, nullptr);
        return 1;
    }

    // BIO method for custom transport
    BIO_METHOD* create_bio_method() {
        BIO_METHOD* method = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "transport_bio");
        if (!method) return nullptr;

        BIO_meth_set_write(method, bio_write);
        BIO_meth_set_read(method, bio_read);
        BIO_meth_set_ctrl(method, bio_ctrl);
        BIO_meth_set_create(method, bio_create);
        BIO_meth_set_destroy(method, bio_destroy);

        return method;
    }

    // Verify callback for certificate hostname validation
    int verify_hostname_callback(int preverify_ok, X509_STORE_CTX* ctx) {
        if (!preverify_ok) return 0;

        SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx, 
            SSL_get_ex_data_X509_STORE_CTX_idx()));
        if (!ssl) return 0;

        SecureTransportWrapper* wrapper = static_cast<SecureTransportWrapper*>(
            SSL_get_app_data(ssl));
        if (!wrapper) return 0;

        X509* cert = X509_STORE_CTX_get_current_cert(ctx);
        if (!cert) return 0;

        return wrapper->verifyCertificateHostname(cert) ? 1 : 0;
    }

    // Static callback for OpenSSL certificate verification
    static int ssl_verify_callback_(int preverify_ok, X509_STORE_CTX* ctx) {
        if (!preverify_ok) {
            return 0;
        }
        SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
        SecureTransportWrapper* wrapper = static_cast<SecureTransportWrapper*>(SSL_get_app_data(ssl));

        if (!wrapper || !wrapper->getTransport()) { 
            return 0; 
        }
        // Bypassing custom hostname verification for now
        return 1; 
    }
}

// BIOData implementation
struct SecureTransportWrapper::BIOData {
    BIO_METHOD* method;
    BIO* bio;

    BIOData() : method(create_bio_method()), bio(nullptr) {
        if (!method) {
            throw std::runtime_error("Failed to create BIO method");
        }
        bio = BIO_new(method);
        if (!bio) {
            BIO_meth_free(method);
            throw std::runtime_error("Failed to create BIO");
        }
    }

    ~BIOData() {
        if (bio) BIO_free(bio);
        if (method) BIO_meth_free(method);
    }
};

SecureTransportWrapper::SecureTransportWrapper(
    std::shared_ptr<TransportProtocol> transport,
    std::shared_ptr<SecurityManager> security_manager,
    const SecureTransportConfig& config)
    : transport_(std::move(transport)),
      security_manager_(std::move(security_manager)),
      config_(config),
      state_(ConnectionState::DISCONNECTED),
      last_error_(TransportError::NONE),
      is_handshake_complete_(false),
      is_server_mode_(config_.expectedHostname.empty()),
      bio_data_(std::make_unique<BIOData>()) {
    
    BIO_set_data(bio_data_->bio, this);
    // SSL_set_app_data(security_manager_->getSSLHandleForContext(secure_context_.get()), this); // REMOVED: Incorrect call

    auto contextResult = setupSecureContext(is_server_mode_);
    if (!contextResult.has_value()) {
        cleanupSecureContext();
        throw std::runtime_error("Failed to setup secure context: " + contextResult.error());
    }

    Result<void> handshakeResult = performHandshake();
    if (!handshakeResult.has_value()) {
        state_ = ConnectionState::ERROR;
        throw std::runtime_error("Handshake failed during SecureTransportWrapper construction: " + handshakeResult.error());
    }

    if (config_.securityConfig.recordBatching.enabled) {
        if (!initializeBatching().has_value()) {
            XLOG_WARN("Failed to initialize record batching.");
        }
    }
    if (config_.securityConfig.adaptiveRecord.enabled) {
        if (!initializeAdaptiveRecordSizing().has_value()) {
            XLOG_WARN("Failed to initialize adaptive record sizing.");
        }
    }
}

SecureTransportWrapper::~SecureTransportWrapper() {
    disconnect();
    cleanupSecureContext();
    if (bio_data_ && bio_data_->bio) {
        BIO_free_all(bio_data_->bio);
        bio_data_->bio = nullptr;
    }
    if (batchContext_ && batchContext_->batchThread.joinable()) {
        batchContext_->shouldStop = true;
        batchContext_->cv.notify_all();
        batchContext_->batchThread.join();
    }
}

bool SecureTransportWrapper::connect(const std::string& endpoint, const ConnectionConfig& /* DONT USE: outerConfig */) {
    if (state_ == ConnectionState::CONNECTED && is_handshake_complete_) {
        return true;
    }
    if (state_ == ConnectionState::CONNECTING) {
        return false;
    }

    updateConnectionState(ConnectionState::CONNECTING);

    if (!transport_->connect(endpoint, config_.connectionConfig)) {
        handleSecurityError("Underlying transport failed to connect: " + transport_->getLastError());
        updateConnectionState(ConnectionState::ERROR);
        return false;
    }

    if (!secure_context_) {
        is_server_mode_ = false;
        auto contextResult = setupSecureContext(is_server_mode_);
        if (!contextResult.has_value()) {
            handleSecurityError("Failed to setup secure context for connect: " + contextResult.error());
            transport_->disconnect();
            updateConnectionState(ConnectionState::ERROR);
            return false;
        }
    }

    auto handshakeResult = performHandshake();
    if (!handshakeResult.has_value()) {
        handleSecurityError("Handshake failed: " + handshakeResult.error());
        transport_->disconnect();
        updateConnectionState(ConnectionState::ERROR);
        return false;
    }
    
    return true;
}

bool SecureTransportWrapper::disconnect() {
    if (!isConnected() && state_ != ConnectionState::CONNECTING) {
        return true; // Already disconnected or not even trying to connect
    }

    updateConnectionState(ConnectionState::DISCONNECTING);
    bool disconnect_success = true;

    if (batchContext_ && batchContext_->batchThread.joinable()) {
        batchContext_->shouldStop = true;
        batchContext_->cv.notify_one();
        try {
            if(batchContext_->batchThread.joinable()) batchContext_->batchThread.join();
        } catch (const std::system_error& e) {
            // Log error if needed, but continue disconnecting
            // Consider this a non-fatal issue for the disconnect process itself
        }
    }

    if (secure_context_) {
        Result<void> shutdown_result = secure_context_->shutdown();
        if (!shutdown_result.has_value()) {
            // Log error from secure_context_->shutdown()
            // e.g., last_error_message_ = shutdown_result.error();
            disconnect_success = false; // Mark as not entirely clean
        }
    }

    // Underlying transport disconnect
    if (transport_ && transport_->isConnected()) {
        if (!transport_->disconnect()) {
            // Log error from underlying transport
            disconnect_success = false;
        }
    }

    cleanupSecureContext(); 
    updateConnectionState(ConnectionState::DISCONNECTED);
    is_handshake_complete_ = false;
    negotiated_protocol_.clear();
    if (batchContext_) batchContext_->clear();
    if (adaptiveContext_) adaptiveContext_->clear();

    // Notify callbacks
    if (state_callback_) {
        state_callback_(ConnectionState::DISCONNECTED);
    }

    return disconnect_success;
}

bool SecureTransportWrapper::isConnected() const {
    return state_ == ConnectionState::CONNECTED && is_handshake_complete_;
}

ssize_t SecureTransportWrapper::send(const uint8_t* data, size_t size) {
    if (!is_handshake_complete_ || !secure_context_) {
        handleSecurityError("Send attempt before handshake or no context");
        return -1;
    }
    std::vector<uint8_t> plaintext(data, data + size);
    auto result = secure_context_->encrypt(plaintext);
    if (!result.has_value()) {
        handleSecurityError("Encryption failed: " + result.error());
        return -1;
    }
    const auto& ciphertext = result.value();
    return transport_->send(ciphertext.data(), ciphertext.size());
}

ssize_t SecureTransportWrapper::receive(uint8_t* buffer, size_t size) {
    if (!is_handshake_complete_ || !secure_context_) {
        handleSecurityError("Receive attempt before handshake or no context");
        return -1;
    }
    std::vector<uint8_t> encrypted_data_buffer(size);
    ssize_t bytes_read_from_transport = transport_->receive(encrypted_data_buffer.data(), encrypted_data_buffer.size());
    if (bytes_read_from_transport <= 0) {
        if (bytes_read_from_transport < 0) last_error_message_ = transport_->getLastError();
        return bytes_read_from_transport;
    }
    encrypted_data_buffer.resize(bytes_read_from_transport);

    auto result = secure_context_->decrypt(encrypted_data_buffer);
    if (!result.has_value()) {
        handleSecurityError("Decryption failed: " + result.error());
        return -1;
    }
    const auto& plaintext = result.value();
    size_t to_copy = std::min(size, plaintext.size());
    std::memcpy(buffer, plaintext.data(), to_copy);
    return static_cast<ssize_t>(to_copy);
}

bool SecureTransportWrapper::getPeerAddress(std::string& address, uint16_t& port) {
    if (!transport_) return false;
    return transport_->getPeerAddress(address, port);
}

int SecureTransportWrapper::getSocketFd() const {
    if (!transport_) return -1;
    return transport_->getSocketFd();
}

bool SecureTransportWrapper::setNonBlocking(bool nonBlocking) {
    if (!transport_) return false;
    return transport_->setNonBlocking(nonBlocking);
}

bool SecureTransportWrapper::setReceiveTimeout(const std::chrono::milliseconds& timeout) {
    if (!transport_) return false;
    return transport_->setReceiveTimeout(timeout);
}

bool SecureTransportWrapper::setSendTimeout(const std::chrono::milliseconds& timeout) {
    if (!transport_) return false;
    return transport_->setSendTimeout(timeout);
}

bool SecureTransportWrapper::setKeepAlive(bool enable) {
    if (!transport_) return false;
    return transport_->setKeepAlive(enable);
}

bool SecureTransportWrapper::setTcpNoDelay(bool enable) {
    if (!transport_) return false;
    return transport_->setTcpNoDelay(enable);
}

bool SecureTransportWrapper::setReuseAddress(bool enable) {
    if (!transport_) return false;
    return transport_->setReuseAddress(enable);
}

bool SecureTransportWrapper::setReceiveBufferSize(size_t size) {
    if (!transport_) return false;
    return transport_->setReceiveBufferSize(size);
}

bool SecureTransportWrapper::setSendBufferSize(size_t size) {
    if (!transport_) return false;
    return transport_->setSendBufferSize(size);
}

std::string SecureTransportWrapper::getLastError() const {
    return last_error_message_;
}

bool SecureTransportWrapper::setLocalPort(uint16_t port) {
    return transport_->setLocalPort(port);
}

ConnectionState SecureTransportWrapper::getState() const {
    return state_;
}

TransportError SecureTransportWrapper::getLastErrorCode() const {
    return last_error_;
}

std::string SecureTransportWrapper::getErrorDetails() const {
    return last_error_message_;
}

bool SecureTransportWrapper::reconnect(uint32_t maxAttempts, uint32_t delayMs) {
    for (uint32_t attempt = 0; attempt < maxAttempts; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        disconnect();
        cleanupSecureContext();

        if (transport_->reconnect(1, 0)) {
            auto result = setupSecureContext(false);
            if (!result.has_value()) continue;

            result = performHandshake();
            if (!result.has_value()) continue;

            updateConnectionState(ConnectionState::CONNECTED);
            return true;
        }
    }

    last_error_ = TransportError::RECONNECTION_FAILED;
    last_error_message_ = "Failed to reconnect after " + std::to_string(maxAttempts) + " attempts";
    return false;
}

void SecureTransportWrapper::setStateCallback(std::function<void(ConnectionState)> callback) {
    state_callback_ = std::move(callback);
}

void SecureTransportWrapper::setErrorCallback(
    std::function<void(TransportError, const std::string&)> callback) {
    error_callback_ = std::move(callback);
}

bool SecureTransportWrapper::checkHealth() {
    if (!isConnected()) return false;
    return transport_->checkHealth();
}

std::string SecureTransportWrapper::getNegotiatedProtocol() const {
    if (!is_handshake_complete_ || !secure_context_) {
        return "";
    }
    return secure_context_->getNegotiatedProtocol();
}

std::string SecureTransportWrapper::getPeerCertificateInfo() const {
    return secure_context_ ? secure_context_->getPeerCertificateInfo() : "";
}

CipherSuite SecureTransportWrapper::getNegotiatedCipherSuite() const {
    return secure_context_ ? secure_context_->getNegotiatedCipherSuite() 
                         : CipherSuite::AES_256_GCM_SHA384;
}

bool SecureTransportWrapper::isTLS13() const {
    if (!is_handshake_complete_ || !secure_context_) return false;
    return secure_context_->getNegotiatedProtocol() == "TLSv1.3";
}

bool SecureTransportWrapper::renegotiate() {
    if (!isConnected() || !secure_context_) {
        XLOG_WARN("Renegotiation called when not connected or no secure context.");
        return false;
    }
    is_handshake_complete_ = false;
    if (!performHandshake().has_value()) {
        XLOG_ERROR("Renegotiation failed.");
        return false;
    }
    return true;
}

std::string SecureTransportWrapper::getSecurityLevel() const {
    if (!is_handshake_complete_ || !secure_context_) return "Security context not available";
    std::stringstream ss;
    ss << "Protocol: " << getNegotiatedProtocol() << ", ";
    ss << "Cipher: " << secure_context_->getCipherName() << ", ";
    ss << "Key Size: " << secure_context_->getKeySize() << " bits";
    return ss.str();
}

Result<void> SecureTransportWrapper::performHandshake() {
    if (!secure_context_ || !transport_) {
        return Result<void>("Context or transport not initialized for handshake");
    }
    if (is_handshake_complete_) {
        return Result<void>();
    }

    if (config_.securityConfig.protocol == EncryptionProtocol::DTLS_1_2 || 
        config_.securityConfig.protocol == EncryptionProtocol::DTLS_1_3) {
        if (is_server_mode_) {
        } else {
        }
    }

    is_handshake_complete_ = false;
    while (!is_handshake_complete_) {
        auto stepResult = secure_context_->doHandshakeStep();
        if (!stepResult.has_value()) {
            handleSecurityError("Handshake step failed: " + stepResult.error());
            cleanupSecureContext();
            return Result<void>("Handshake step failed: " + stepResult.error());
        }
        is_handshake_complete_ = secure_context_->isHandshakeComplete();
        if (is_handshake_complete_) break;
    }

    negotiated_protocol_ = secure_context_->getNegotiatedProtocol();
    XLOG_INFO("Handshake complete. Negotiated protocol: " + negotiated_protocol_);
    updateConnectionState(ConnectionState::CONNECTED);
    return Result<void>();
}

Result<void> SecureTransportWrapper::setupSecureContext(bool isServer) {
    auto contextResult = security_manager_->createContext(isServer);
    if (!contextResult.has_value()) {
        return Result<void>("Failed to create secure context from security manager: " + contextResult.error());
    }
    secure_context_ = std::move(contextResult.value());

    if (!secure_context_) {
        return Result<void>("Secure context is null after creation");
    }

    // Commenting out ALPN and Session Resumption as they require direct SSL_CTX access
    // if (!configureALPN()) {
    //     return Result<void>("Failed to configure ALPN");
    // }

    // if (config_.enableSessionResumption && !setupSessionResumption()) {
    //     return Result<void>("Failed to setup session resumption");
    // }

    return Result<void>();
}

bool SecureTransportWrapper::configureALPN() {
    // if (config_.alpnProtocols.empty()) return true;

    // std::vector<unsigned char> protocols;
    // for (const auto& proto : config_.alpnProtocols) {
    //     if (proto.length() > 255) continue;
    //     protocols.push_back(static_cast<unsigned char>(proto.length()));
    //     protocols.insert(protocols.end(), proto.begin(), proto.end());
    // }

    // // Requires direct SSL_CTX* access, which SecureContext should abstract.
    // // This functionality should be part of SecurityManager or SecureContext implementation.
    // // return SSL_CTX_set_alpn_protos(secure_context_->getSSLContext(), 
    // //                               protocols.data(), protocols.size()) == 0;
    return true; // STUBBED
}

bool SecureTransportWrapper::setupSessionResumption() {
    // if (!config_.enableSessionResumption) return true;

    // // SSL_CTX* ctx = secure_context_->getSSLContext();
    // // if (!ctx) return false;

    // // SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_SERVER);
    // // SSL_CTX_set_session_id_context(ctx, 
    // //     reinterpret_cast<const unsigned char*>(\"xenocomm\"), 8);
    // // SSL_CTX_sess_set_cache_size(ctx, config_.sessionCacheSize);

    return true; // STUBBED
}

void SecureTransportWrapper::updateConnectionState(ConnectionState newState) {
    state_ = newState;
    if (state_callback_) {
        state_callback_(newState);
    }
}

void SecureTransportWrapper::handleSecurityError(const std::string& operation) {
    last_error_ = TransportError::CONNECTION_FAILED;
    last_error_message_ = operation;
    
    if (error_callback_) {
        error_callback_(last_error_, last_error_message_);
    }
}

bool SecureTransportWrapper::verifyCertificateHostname(X509* cert) {
    if (!config_.verifyHostname || config_.expectedHostname.empty()) {
        return true;
    }
    if (!cert) return false;

    bool result = false;
    GENERAL_NAMES* sans = static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

    if (sans) {
        for (int i = 0; i < sk_GENERAL_NAME_num(sans); i++) {
            GENERAL_NAME* name = sk_GENERAL_NAME_value(sans, i);
            if (name->type == GEN_DNS) {
                const char* dns_name = reinterpret_cast<const char*>(
                    ASN1_STRING_get0_data(name->d.dNSName));
                if (dns_name && config_.expectedHostname == dns_name) {
                    result = true;
                    break;
                }
            }
        }
        GENERAL_NAMES_free(sans);
    }

    if (!result) {
        X509_NAME* subject_name = X509_get_subject_name(cert);
        if (subject_name) {
            char common_name[256];
            if (X509_NAME_get_text_by_NID(subject_name, NID_commonName, 
                                        common_name, sizeof(common_name)) > 0) {
                if (config_.expectedHostname == common_name) {
                    result = true;
                }
            }
        }
    }

    return result;
}

Result<void> SecureTransportWrapper::handleSessionResumption() {
    if (!config_.enableSessionResumption || !secure_context_) {
        return Result<void>();
    }

    // // SSL* ssl = secure_context_->getSSL(); // Requires OpenSSL specific access
    // // if (!ssl) return Result<void>("Invalid SSL context for session resumption");

    // // SSL_SESSION* session = SSL_get1_session(ssl);
    // // if (!session) {
    // //     XLOG_INFO("No SSL session available to cache/resume.");
    // //     return Result<void>();
    // // }
    // // SSL_SESSION_free(session); 
    return Result<void>(); // STUBBED - actual logic depends on SecureContext impl.
}

void SecureTransportWrapper::cleanupSecureContext() {
    if (secure_context_) {
        handleSessionResumption();
        secure_context_.reset();
    }
    is_handshake_complete_ = false;
    negotiated_protocol_.clear();
}

Result<void> SecureTransportWrapper::initializeBatching() {
    if (!config_.securityConfig.recordBatching.enabled) {
        return Result<void>();
    }

    batchContext_ = std::make_unique<BatchContext>();
    batchContext_->batchThread = std::thread(&SecureTransportWrapper::batchingThread, this);
    return Result<void>();
}

void SecureTransportWrapper::batchingThread() {
    while (!batchContext_->shouldStop) {
        bool shouldProcess = false;
        {
            std::unique_lock<std::mutex> lock(batchContext_->mutex);
            if (batchContext_->messages.empty()) {
                batchContext_->cv.wait_for(lock, 
                    config_.securityConfig.recordBatching.maxDelay,
                    [this] { return !batchContext_->messages.empty() || batchContext_->shouldStop; });
                if (batchContext_->shouldStop) break;
            }
            
            if (!batchContext_->messages.empty()) {
                auto oldestMessage = batchContext_->messages.front().timestamp;
                auto now = std::chrono::steady_clock::now();
                shouldProcess = batchContext_->currentBatchSize >= config_.securityConfig.recordBatching.maxBatchSize ||
                              batchContext_->messages.size() >= config_.securityConfig.recordBatching.maxMessagesPerBatch ||
                              (now - oldestMessage) >= config_.securityConfig.recordBatching.maxDelay;
            }
        }

        if (shouldProcess) {
            auto result = processBatch();
            if (!result.has_value()) {
                XLOG_ERROR("Error processing batch: " + result.error());
            }
        }
    }

    flushBatch();
}

Result<void> SecureTransportWrapper::processBatch() {
    std::vector<uint8_t> batchedData;
    size_t totalSize = 0;
    std::queue<BatchedMessage> currentBatch;

    {
        std::lock_guard<std::mutex> lock(batchContext_->mutex);
        while (!batchContext_->messages.empty() && 
               (totalSize + batchContext_->messages.front().data.size() <= config_.securityConfig.recordBatching.maxBatchSize) &&
               currentBatch.size() < config_.securityConfig.recordBatching.maxMessagesPerBatch) {
            
            auto& msg = batchContext_->messages.front();
            totalSize += msg.data.size();
            currentBatch.push(std::move(msg));
            batchContext_->messages.pop();
        }
        batchContext_->currentBatchSize = 0;
    }

    if (currentBatch.empty()) {
        return Result<void>();
    }

    batchedData.reserve(totalSize);
    while (!currentBatch.empty()) {
        auto& msg = currentBatch.front();
        batchedData.insert(batchedData.end(), msg.data.begin(), msg.data.end());
        currentBatch.pop();
    }

    if (batchedData.empty()) {
        return Result<void>(); // Success, nothing to send
    }

    ssize_t bytes_sent = this->send(batchedData.data(), batchedData.size());

    if (bytes_sent < 0) {
        // Error occurred
        return Result<void>(std::string("Failed to send batched data: ") + getLastError());
    }
    if (static_cast<size_t>(bytes_sent) != batchedData.size()) {
        // Not all data was sent
        return Result<void>(std::string("Failed to send complete batched data: partial send"));
    }

    // TODO: Update RTT samples if necessary

    return Result<void>(); // Success
}

Result<void> SecureTransportWrapper::flushBatch() {
    return processBatch();
}

bool SecureTransportWrapper::shouldBatchMessage(size_t messageSize) const {
    return config_.securityConfig.recordBatching.enabled &&
           messageSize >= config_.securityConfig.recordBatching.minMessageSize;
}

Result<void> SecureTransportWrapper::initializeAdaptiveRecordSizing() {
    if (!config_.securityConfig.adaptiveRecord.enabled) {
        return Result<void>();
    }

    adaptiveContext_ = std::make_unique<AdaptiveRecordContext>(config_.securityConfig.adaptiveRecord);
    return Result<void>();
}

std::chrono::microseconds SecureTransportWrapper::calculateAverageRTT() const {
    if (!adaptiveContext_ || adaptiveContext_->rttSamples.empty()) {
        return std::chrono::microseconds(0);
    }

    std::lock_guard<std::mutex> lock(adaptiveContext_->mutex);
    
    std::chrono::microseconds totalRTT(0);
    size_t count = 0;
    
    auto now = std::chrono::steady_clock::now();
    auto windowStart = now - config_.securityConfig.adaptiveRecord.rttWindow;
    
    for (const auto& sample : adaptiveContext_->rttSamples) {
        if (sample.sendTime >= windowStart) {
            totalRTT += sample.getRTT();
            count++;
        }
    }
    
    return count > 0 ? std::chrono::microseconds(totalRTT.count() / count) : std::chrono::microseconds(0);
}

bool SecureTransportWrapper::shouldAdjustRecordSize() const {
    if (!adaptiveContext_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(adaptiveContext_->mutex);
    
    return !adaptiveContext_->rttSamples.empty() &&
           (std::chrono::steady_clock::now() - adaptiveContext_->lastAdjustment) >= 
           config_.securityConfig.adaptiveRecord.rttWindow;
}

void SecureTransportWrapper::adjustRecordSize(std::chrono::microseconds avgRTT) {
    if (!adaptiveContext_) {
        return;
    }

    std::lock_guard<std::mutex> lock(adaptiveContext_->mutex);
    
    std::chrono::microseconds baselineRTT = std::chrono::microseconds::max();
    for (const auto& sample : adaptiveContext_->rttSamples) {
        baselineRTT = std::min(baselineRTT, sample.getRTT());
    }
    
    float rttRatio = baselineRTT.count() > 0 ? 
        static_cast<float>(avgRTT.count()) / baselineRTT.count() : 2.0f;
    
    size_t newSize = adaptiveContext_->currentRecordSize;
    
    if (rttRatio < 1.1f) {
        newSize = static_cast<size_t>(newSize * config_.securityConfig.adaptiveRecord.growthFactor);
    } else if (rttRatio > 1.5f) {
        newSize = static_cast<size_t>(newSize * config_.securityConfig.adaptiveRecord.shrinkFactor);
    }
    
    newSize = std::max(config_.securityConfig.adaptiveRecord.minSize,
                      std::min(newSize, config_.securityConfig.adaptiveRecord.maxSize));
    
    adaptiveContext_->currentRecordSize = newSize;
    adaptiveContext_->lastAdjustment = std::chrono::steady_clock::now();
}

void SecureTransportWrapper::updateRecordSize() {
    if (shouldAdjustRecordSize()) {
        auto avgRTT = calculateAverageRTT();
        if (avgRTT.count() > 0) {
            adjustRecordSize(avgRTT);
        }
    }
}

Result<void> SecureTransportWrapper::sendv(const std::vector<std::vector<uint8_t>>& buffers) {
    if (buffers.empty()) {
        return Result<void>(); // Success with default constructor
    }

    // Check if we should use optimized vectored I/O
    if (shouldUseVectoredIO(buffers)) {
        return processVectoredIO(buffers);
    }

    // Fall back to regular send for small or single buffers
    for (const auto& buffer : buffers) {
        if (!buffer.empty()) {
            ssize_t sent = send(buffer.data(), buffer.size());
            if (sent < 0 || static_cast<size_t>(sent) != buffer.size()) {
                return Result<void>("Failed to send buffer data: " + getLastError());
            }
        }
    }

    return Result<void>(); // Success with default constructor
}

bool SecureTransportWrapper::shouldUseVectoredIO(const std::vector<std::vector<uint8_t>>& buffers) const {
    // Use vectored I/O if we have multiple buffers and total size is significant
    if (buffers.size() <= 1) {
        return false;
    }

    // Calculate total size
    size_t totalSize = 0;
    for (const auto& buffer : buffers) {
        totalSize += buffer.size();
    }

    // Only use vectored I/O for larger transfers
    return totalSize > 8192;  // Threshold for using optimized path (8KB)
}

Result<void> SecureTransportWrapper::processVectoredIO(const std::vector<std::vector<uint8_t>>& buffers) {
    // Encrypt each buffer
    std::vector<std::vector<uint8_t>> encryptedBuffers;
    encryptedBuffers.reserve(buffers.size());

    for (const auto& buffer : buffers) {
        if (buffer.empty()) {
            continue;
        }

        // TODO: Implement actual encryption of each buffer
        // For now, just copy the buffer as a placeholder
        encryptedBuffers.push_back(buffer);
    }

    // Send using vectored I/O
    return sendEncryptedv(encryptedBuffers);
}

Result<void> SecureTransportWrapper::sendEncryptedv(const std::vector<std::vector<uint8_t>>& buffers) {
    // Lazy initialize vectored I/O context if needed
    if (!vectoredContext_) {
        vectoredContext_ = std::make_unique<VectoredIOContext>();
    }

    vectoredContext_->reset();
    vectoredContext_->encryptedBuffers = buffers;

    // Prepare iovec structures
    size_t iovCount = std::min(buffers.size(), vectoredContext_->iovecs.size());
    for (size_t i = 0; i < iovCount; i++) {
        auto& buffer = buffers[i];
        if (!buffer.empty()) {
            vectoredContext_->iovecs[i].iov_base = const_cast<uint8_t*>(buffer.data());
            vectoredContext_->iovecs[i].iov_len = buffer.size();
        }
    }

    // Call writev on the underlying transport
    // Since we don't have direct access to writev in the Transport interface,
    // we'll use individual sends for now
    for (size_t i = 0; i < iovCount; i++) {
        auto& iov = vectoredContext_->iovecs[i];
        if (iov.iov_base && iov.iov_len > 0) {
            ssize_t sent = transport_->send(
                static_cast<const uint8_t*>(iov.iov_base), 
                iov.iov_len);
                
            if (sent < 0 || static_cast<size_t>(sent) != iov.iov_len) {
                return Result<void>("Failed to send in vectored I/O: " + transport_->getLastError());
            }
        }
    }

    return Result<void>(); // Success with default constructor
}

} // namespace core
} // namespace xenocomm 
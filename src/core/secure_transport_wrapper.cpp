#include "xenocomm/core/secure_transport_wrapper.hpp"
#include "xenocomm/utils/logging.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <chrono>
#include <thread>

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
            SSL_get_ex_data(ssl, 0));
        if (!wrapper) return 0;

        X509* cert = X509_STORE_CTX_get_current_cert(ctx);
        if (!cert) return 0;

        return wrapper->verifyCertificateHostname(cert) ? 1 : 0;
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
    : transport_(transport),
      security_manager_(security_manager),
      config_(config),
      state_(ConnectionState::DISCONNECTED),
      last_error_(TransportError::NONE),
      is_handshake_complete_(false),
      bio_data_(std::make_unique<BIOData>()) {
    
    BIO_set_data(bio_data_->bio, this);
}

SecureTransportWrapper::~SecureTransportWrapper() {
    disconnect();
}

bool SecureTransportWrapper::connect(const std::string& endpoint, const ConnectionConfig& config) {
    if (state_ == ConnectionState::CONNECTED) {
        last_error_ = TransportError::ALREADY_CONNECTED;
        last_error_message_ = "Already connected";
        return false;
    }

    updateConnectionState(ConnectionState::CONNECTING);

    // Connect underlying transport
    if (!transport_->connect(endpoint, config)) {
        last_error_ = transport_->getLastErrorCode();
        last_error_message_ = transport_->getLastError();
        updateConnectionState(ConnectionState::ERROR);
        return false;
    }

    // Setup secure context
    auto result = setupSecureContext(false);  // Client mode
    if (!result.isSuccess()) {
        last_error_ = TransportError::INITIALIZATION_FAILED;
        last_error_message_ = result.error();
        disconnect();
        return false;
    }

    // Perform TLS/DTLS handshake
    result = performHandshake();
    if (!result.isSuccess()) {
        last_error_ = TransportError::CONNECTION_FAILED;
        last_error_message_ = result.error();
        disconnect();
        return false;
    }

    updateConnectionState(ConnectionState::CONNECTED);
    return true;
}

bool SecureTransportWrapper::disconnect() {
    if (state_ == ConnectionState::DISCONNECTED) {
        return true;
    }

    updateConnectionState(ConnectionState::DISCONNECTING);
    
    if (secure_context_ && is_handshake_complete_) {
        // Send close notify alert
        secure_context_->encrypt(std::vector<uint8_t>());
    }

    cleanupSecureContext();
    bool result = transport_->disconnect();
    
    updateConnectionState(ConnectionState::DISCONNECTED);
    return result;
}

bool SecureTransportWrapper::isConnected() const {
    return state_ == ConnectionState::CONNECTED && is_handshake_complete_;
}

ssize_t SecureTransportWrapper::send(const uint8_t* data, size_t size) {
    if (!isConnected() || !secure_context_) {
        last_error_ = TransportError::NOT_CONNECTED;
        last_error_message_ = "Not connected";
        return -1;
    }

    auto result = secure_context_->encrypt(std::vector<uint8_t>(data, data + size));
    if (!result.isSuccess()) {
        handleSecurityError("Encryption failed: " + result.error());
        return -1;
    }

    return result.value().size();
}

ssize_t SecureTransportWrapper::receive(uint8_t* buffer, size_t size) {
    if (!isConnected() || !secure_context_) {
        last_error_ = TransportError::NOT_CONNECTED;
        last_error_message_ = "Not connected";
        return -1;
    }

    std::vector<uint8_t> encrypted(size);
    ssize_t read = transport_->receive(encrypted.data(), size);
    if (read <= 0) return read;

    encrypted.resize(read);
    auto result = secure_context_->decrypt(encrypted);
    if (!result.isSuccess()) {
        handleSecurityError("Decryption failed: " + result.error());
        return -1;
    }

    const auto& decrypted = result.value();
    size_t to_copy = std::min(size, decrypted.size());
    std::memcpy(buffer, decrypted.data(), to_copy);
    return to_copy;
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
            if (!result.isSuccess()) continue;

            result = performHandshake();
            if (!result.isSuccess()) continue;

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
    return negotiated_protocol_;
}

std::string SecureTransportWrapper::getPeerCertificateInfo() const {
    return secure_context_ ? secure_context_->getPeerCertificateInfo() : "";
}

CipherSuite SecureTransportWrapper::getNegotiatedCipherSuite() const {
    return secure_context_ ? secure_context_->getNegotiatedCipherSuite() 
                         : CipherSuite::AES_256_GCM_SHA384;
}

bool SecureTransportWrapper::isTLS13() const {
    if (!secure_context_) return false;
    return secure_context_->getNegotiatedProtocol() == "TLSv1.3";
}

bool SecureTransportWrapper::renegotiate() {
    if (!isConnected() || !secure_context_) return false;
    return performHandshake().isSuccess();
}

std::string SecureTransportWrapper::getSecurityLevel() const {
    if (!secure_context_) return "None";
    
    std::stringstream ss;
    ss << "Protocol: " << (isTLS13() ? "TLS 1.3" : "TLS 1.2") << ", ";
    ss << "Cipher: " << secure_context_->getCipherName() << ", ";
    ss << "Key Size: " << secure_context_->getKeySize() << " bits";
    return ss.str();
}

Result<void> SecureTransportWrapper::performHandshake() {
    if (!transport_ || !secure_context_) {
        return Result<void>::Error("Transport or secure context not initialized");
    }

    // For DTLS, handle cookie exchange first
    if (config_.securityConfig.protocol == EncryptionProtocol::DTLS_1_2 ||
        config_.securityConfig.protocol == EncryptionProtocol::DTLS_1_3) {
        
        // Get peer address
        std::string peerIp;
        uint16_t peerPort;
        if (!transport_->getPeerAddress(peerIp, peerPort)) {
            return Result<void>::Error("Failed to get peer address for DTLS cookie exchange");
        }
        NetworkAddress peerAddr(peerIp, peerPort);

        if (transport_->isServer()) {
            // Server: Generate and send cookie
            auto cookieResult = security_manager_->generateDtlsCookie(peerAddr);
            if (!cookieResult) {
                return Result<void>::Error("Failed to generate DTLS cookie: " + cookieResult.error());
            }

            // Set the cookie in the SSL context
            if (!SSL_set_dtls_cookie(secure_context_->getNativeHandle(), 
                                   cookieResult.value().data(),
                                   cookieResult.value().size())) {
                return Result<void>::Error("Failed to set DTLS cookie");
            }
        } else {
            // Client: Handle cookie verification
            std::vector<uint8_t> cookie;
            cookie.resize(SSL_get_dtls_cookie_len(secure_context_->getNativeHandle()));
            
            if (SSL_get_dtls_cookie(secure_context_->getNativeHandle(), 
                                  cookie.data(), cookie.size()) > 0) {
                auto verifyResult = security_manager_->verifyDtlsCookie(cookie, peerAddr);
                if (!verifyResult) {
                    return Result<void>::Error("DTLS cookie verification failed: " + verifyResult.error());
                }
            }
        }
    }

    // Perform the actual TLS/DTLS handshake
    auto result = secure_context_->handshake();
    if (!result) {
        handleSecurityError("Handshake failed");
        return result;
    }

    is_handshake_complete_ = true;
    updateConnectionState(ConnectionState::Connected);

    // Get negotiated protocol if ALPN was used
    if (!config_.alpnProtocols.empty()) {
        const unsigned char* protocol = nullptr;
        unsigned int protocolLen = 0;
        SSL_get0_alpn_selected(secure_context_->getNativeHandle(), &protocol, &protocolLen);
        if (protocol && protocolLen > 0) {
            negotiated_protocol_ = std::string(reinterpret_cast<const char*>(protocol), protocolLen);
        }
    }

    return Result<void>::Ok();
}

Result<void> SecureTransportWrapper::setupSecureContext(bool isServer) {
    try {
        secure_context_ = security_manager_->createContext(isServer).value();
        
        if (!configureALPN()) {
            return Result<void>::Error("Failed to configure ALPN");
        }

        if (config_.enableSessionResumption && !setupSessionResumption()) {
            return Result<void>::Error("Failed to setup session resumption");
        }

        return Result<void>::Success();
    } catch (const std::exception& e) {
        return Result<void>::Error(std::string("Failed to setup secure context: ") + e.what());
    }
}

bool SecureTransportWrapper::configureALPN() {
    if (config_.alpnProtocols.empty()) return true;

    std::vector<unsigned char> protocols;
    for (const auto& proto : config_.alpnProtocols) {
        if (proto.length() > 255) continue;
        protocols.push_back(static_cast<unsigned char>(proto.length()));
        protocols.insert(protocols.end(), proto.begin(), proto.end());
    }

    return SSL_CTX_set_alpn_protos(secure_context_->getSSLContext(), 
                                  protocols.data(), protocols.size()) == 0;
}

bool SecureTransportWrapper::setupSessionResumption() {
    if (!config_.enableSessionResumption) return true;

    SSL_CTX* ctx = secure_context_->getSSLContext();
    if (!ctx) return false;

    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_SERVER);
    SSL_CTX_set_session_id_context(ctx, 
        reinterpret_cast<const unsigned char*>("xenocomm"), 8);
    SSL_CTX_sess_set_cache_size(ctx, config_.sessionCacheSize);

    return true;
}

void SecureTransportWrapper::updateConnectionState(ConnectionState newState) {
    state_ = newState;
    if (state_callback_) {
        state_callback_(newState);
    }
}

void SecureTransportWrapper::handleSecurityError(const std::string& operation) {
    last_error_ = TransportError::SECURITY_ERROR;
    last_error_message_ = operation;
    
    if (error_callback_) {
        error_callback_(last_error_, last_error_message_);
    }
}

bool SecureTransportWrapper::verifyCertificateHostname(const std::string& hostname) {
    if (!secure_context_) return false;

    X509* cert = SSL_get_peer_certificate(secure_context_->getSSL());
    if (!cert) return false;

    bool result = false;
    GENERAL_NAMES* sans = static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));

    if (sans) {
        for (int i = 0; i < sk_GENERAL_NAME_num(sans); i++) {
            GENERAL_NAME* name = sk_GENERAL_NAME_value(sans, i);
            if (name->type == GEN_DNS) {
                const char* dns = reinterpret_cast<const char*>(
                    ASN1_STRING_get0_data(name->d.dNSName));
                if (hostname == dns) {
                    result = true;
                    break;
                }
            }
        }
        GENERAL_NAMES_free(sans);
    }

    if (!result) {
        X509_NAME* subject = X509_get_subject_name(cert);
        if (subject) {
            char common_name[256];
            if (X509_NAME_get_text_by_NID(subject, NID_commonName, 
                                        common_name, sizeof(common_name)) > 0) {
                result = (hostname == common_name);
            }
        }
    }

    X509_free(cert);
    return result;
}

Result<void> SecureTransportWrapper::handleSessionResumption() {
    if (!config_.enableSessionResumption || !secure_context_) {
        return Result<void>::Success();
    }

    SSL* ssl = secure_context_->getSSL();
    if (!ssl) return Result<void>::Error("Invalid SSL context");

    SSL_SESSION* session = SSL_get1_session(ssl);
    if (!session) return Result<void>::Error("Failed to get session");

    // Store session for future use
    // TODO: Implement session storage and retrieval
    SSL_SESSION_free(session);
    return Result<void>::Success();
}

void SecureTransportWrapper::cleanupSecureContext() {
    if (secure_context_) {
        handleSessionResumption();
        secure_context_.reset();
    }
    is_handshake_complete_ = false;
    negotiated_protocol_.clear();
}

std::shared_ptr<TransportProtocol> SecureTransportWrapper::getTransport() const {
    return transport_;
}

Result<void> SecureTransportWrapper::initializeBatching() {
    if (!config_.securityConfig.recordBatching.enabled) {
        return Result<void>::Ok();
    }

    batchContext_ = std::make_unique<BatchContext>();
    batchContext_->batchThread = std::thread(&SecureTransportWrapper::batchingThread, this);
    return Result<void>::Ok();
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
            if (!result) {
                // Log error but continue processing
                // TODO: Add proper error handling/logging
            }
        }
    }

    // Final flush when stopping
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
        return Result<void>::Ok();
    }

    // Prepare batched data
    batchedData.reserve(totalSize);
    while (!currentBatch.empty()) {
        auto& msg = currentBatch.front();
        batchedData.insert(batchedData.end(), msg.data.begin(), msg.data.end());
        currentBatch.pop();
    }

    // Send the batched data
    return sendEncrypted(batchedData);
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
        return Result<void>::Ok();
    }

    adaptiveContext_ = std::make_unique<AdaptiveRecordContext>(config_.securityConfig.adaptiveRecord);
    return Result<void>::Ok();
}

std::chrono::microseconds SecureTransportWrapper::calculateAverageRTT() const {
    if (!adaptiveContext_ || adaptiveContext_->rttSamples.empty()) {
        return std::chrono::microseconds(0);
    }

    std::lock_guard<std::mutex> lock(adaptiveContext_->mutex);
    
    // Calculate average RTT from recent samples
    std::chrono::microseconds totalRTT(0);
    size_t count = 0;
    
    // Use only samples within the RTT window
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
    
    // Check if we have enough samples and if enough time has passed since last adjustment
    return !adaptiveContext_->rttSamples.empty() &&
           (std::chrono::steady_clock::now() - adaptiveContext_->lastAdjustment) >= 
           config_.securityConfig.adaptiveRecord.rttWindow;
}

void SecureTransportWrapper::adjustRecordSize(std::chrono::microseconds avgRTT) {
    if (!adaptiveContext_) {
        return;
    }

    std::lock_guard<std::mutex> lock(adaptiveContext_->mutex);
    
    // Get the baseline RTT (minimum RTT observed)
    std::chrono::microseconds baselineRTT = std::chrono::microseconds::max();
    for (const auto& sample : adaptiveContext_->rttSamples) {
        baselineRTT = std::min(baselineRTT, sample.getRTT());
    }
    
    // Calculate RTT ratio (current vs baseline)
    float rttRatio = baselineRTT.count() > 0 ? 
        static_cast<float>(avgRTT.count()) / baselineRTT.count() : 2.0f;
    
    size_t newSize = adaptiveContext_->currentRecordSize;
    
    // Adjust record size based on RTT ratio
    if (rttRatio < 1.1f) {
        // Network conditions are good, try increasing record size
        newSize = static_cast<size_t>(newSize * config_.securityConfig.adaptiveRecord.growthFactor);
    } else if (rttRatio > 1.5f) {
        // Network congestion detected, decrease record size
        newSize = static_cast<size_t>(newSize * config_.securityConfig.adaptiveRecord.shrinkFactor);
    }
    
    // Clamp to configured limits
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

Result<void> SecureTransportWrapper::send(const std::vector<uint8_t>& data) {
    if (!is_handshake_complete_) {
        return Result<void>::Error("Handshake not complete");
    }

    // Update record size based on network conditions
    updateRecordSize();

    // Handle batching if enabled and appropriate
    if (shouldBatchMessage(data.size())) {
        std::lock_guard<std::mutex> lock(batchContext_->mutex);
        batchContext_->messages.emplace(std::vector<uint8_t>(data));
        batchContext_->currentBatchSize += data.size();
        batchContext_->cv.notify_one();
        return Result<void>::Ok();
    }

    // Record send time for RTT measurement
    auto sendTime = std::chrono::steady_clock::now();
    
    // Send the data
    auto result = sendEncrypted(data);
    
    // Record RTT sample if send was successful
    if (result && adaptiveContext_) {
        RTTSample sample{
            sendTime,
            std::chrono::steady_clock::now(),
            data.size()
        };
        adaptiveContext_->addSample(sample);
    }

    return result;
}

Result<void> SecureTransportWrapper::close() {
    if (batchContext_) {
        batchContext_->shouldStop = true;
        batchContext_->cv.notify_one();
        if (batchContext_->batchThread.joinable()) {
            batchContext_->batchThread.join();
        }
    }

    // ... existing close implementation ...
}

bool SecureTransportWrapper::shouldUseVectoredIO(
    const std::vector<std::vector<uint8_t>>& buffers) const {
    if (!config_.securityConfig.enableVectoredIO || buffers.size() <= 1) {
        return false;
    }

    // Calculate total size
    size_t totalSize = 0;
    for (const auto& buffer : buffers) {
        totalSize += buffer.size();
        if (buffer.empty()) {
            return false; // Skip vectored I/O if any buffer is empty
        }
    }

    // Use vectored I/O if we have multiple non-empty buffers and total size is significant
    return buffers.size() <= VectoredIOContext::MAX_IOV && totalSize >= 4096;
}

Result<void> SecureTransportWrapper::sendv(
    const std::vector<std::vector<uint8_t>>& buffers) {
    if (!is_handshake_complete_) {
        return Result<void>::Error("Handshake not complete");
    }

    // Update record size based on network conditions
    updateRecordSize();

    // Use vectored I/O if appropriate
    if (shouldUseVectoredIO(buffers)) {
        return sendEncryptedv(buffers);
    }

    // Fall back to regular send for each buffer
    for (const auto& buffer : buffers) {
        auto result = send(buffer);
        if (!result) {
            return result;
        }
    }

    return Result<void>::Ok();
}

Result<void> SecureTransportWrapper::sendEncryptedv(
    const std::vector<std::vector<uint8_t>>& buffers) {
    if (!vectoredContext_) {
        vectoredContext_ = std::make_unique<VectoredIOContext>();
    }
    vectoredContext_->reset();

    // Record send time for RTT measurement
    auto sendTime = std::chrono::steady_clock::now();

    // Encrypt each buffer
    size_t totalSize = 0;
    for (size_t i = 0; i < buffers.size() && i < VectoredIOContext::MAX_IOV; ++i) {
        const auto& buffer = buffers[i];
        
        // Allocate space for encrypted data
        size_t maxCiphertext = buffer.size() + EVP_MAX_BLOCK_LENGTH + 
                              EVP_MAX_IV_LENGTH + EVP_MAX_MD_SIZE;
        vectoredContext_->encryptedBuffers.emplace_back(maxCiphertext);
        
        // Encrypt the buffer
        int encryptedLength = 0;
        if (!SSL_write(secure_context_->getNativeHandle(), 
                      buffer.data(), buffer.size())) {
            return Result<void>::Error("Failed to encrypt buffer");
        }
        
        // Get the encrypted data
        encryptedLength = BIO_read(secure_context_->getWriteBIO(),
                                 vectoredContext_->encryptedBuffers.back().data(),
                                 maxCiphertext);
        
        if (encryptedLength <= 0) {
            return Result<void>::Error("Failed to get encrypted data");
        }
        
        vectoredContext_->encryptedBuffers.back().resize(encryptedLength);
        vectoredContext_->iovecs[i].iov_base = vectoredContext_->encryptedBuffers.back().data();
        vectoredContext_->iovecs[i].iov_len = encryptedLength;
        totalSize += encryptedLength;
    }

    // Send all encrypted buffers using writev
    ssize_t sent = writev(transport_->getSocketFd(),
                         vectoredContext_->iovecs.data(),
                         vectoredContext_->encryptedBuffers.size());

    if (sent < 0) {
        return Result<void>::Error("Failed to send encrypted data: " + 
                                 std::string(strerror(errno)));
    }

    if (static_cast<size_t>(sent) != totalSize) {
        return Result<void>::Error("Incomplete send of encrypted data");
    }

    // Record RTT sample
    if (adaptiveContext_) {
        RTTSample sample{
            sendTime,
            std::chrono::steady_clock::now(),
            totalSize
        };
        adaptiveContext_->addSample(sample);
    }

    return Result<void>::Ok();
}

} // namespace core
} // namespace xenocomm 
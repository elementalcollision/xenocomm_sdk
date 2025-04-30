#pragma once

#include "xenocomm/core/transport_protocol.hpp"
#include "xenocomm/core/security_manager.h"
#include "xenocomm/core/security_config.hpp"
#include "xenocomm/core/transport_interface.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <deque>
#include <sys/uio.h>

namespace xenocomm {
namespace core {

/**
 * @brief Configuration for secure transport connections
 */
struct SecureTransportConfig {
    SecurityConfig securityConfig;  // Base security configuration
    ConnectionConfig connectionConfig;  // Base connection configuration
    bool verifyHostname = true;  // Whether to verify hostname in certificate
    std::string expectedHostname;  // Expected hostname for verification
    bool allowInsecureFallback = false;  // Whether to allow fallback to insecure connection
    uint32_t handshakeTimeoutMs = 30000;  // Timeout for TLS/DTLS handshake
    uint32_t sessionCacheSize = 1000;  // Maximum number of cached sessions
    bool enableSessionResumption = true;  // Whether to enable session resumption
    bool enableOCSPStapling = true;  // Whether to enable OCSP stapling
    std::vector<std::string> alpnProtocols;  // ALPN protocol list
};

/**
 * @brief Wraps a transport protocol with TLS/DTLS encryption
 * 
 * This class decorates any TransportProtocol implementation with encryption
 * capabilities using TLS for TCP and DTLS for UDP transports.
 */
class SecureTransportWrapper : public TransportProtocol {
public:
    /**
     * @brief Constructs a secure transport wrapper
     * 
     * @param transport The underlying transport to wrap
     * @param security_manager The security manager to use for encryption
     * @param config Configuration for the secure transport
     */
    SecureTransportWrapper(
        std::shared_ptr<TransportProtocol> transport,
        std::shared_ptr<SecurityManager> security_manager,
        const SecureTransportConfig& config
    );

    ~SecureTransportWrapper() override;

    // TransportProtocol interface implementation
    bool connect(const std::string& endpoint, const ConnectionConfig& config) override;
    bool disconnect() override;
    bool isConnected() const override;
    ssize_t send(const uint8_t* data, size_t size) override;
    ssize_t receive(uint8_t* buffer, size_t size) override;
    std::string getLastError() const override;
    bool setLocalPort(uint16_t port) override;
    ConnectionState getState() const override;
    TransportError getLastErrorCode() const override;
    std::string getErrorDetails() const override;
    bool reconnect(uint32_t maxAttempts = 3, uint32_t delayMs = 1000) override;
    void setStateCallback(std::function<void(ConnectionState)> callback) override;
    void setErrorCallback(std::function<void(TransportError, const std::string&)> callback) override;
    bool checkHealth() override;

    /**
     * @brief Get the negotiated protocol via ALPN
     * 
     * @return The negotiated protocol or empty string if none
     */
    std::string getNegotiatedProtocol() const;

    /**
     * @brief Get the peer's certificate information
     * 
     * @return Certificate information string
     */
    std::string getPeerCertificateInfo() const;

    /**
     * @brief Get the negotiated cipher suite
     * 
     * @return The negotiated CipherSuite
     */
    CipherSuite getNegotiatedCipherSuite() const;

    /**
     * @brief Check if the connection is using TLS 1.3
     * 
     * @return true if using TLS 1.3, false otherwise
     */
    bool isTLS13() const;

    /**
     * @brief Force renegotiation of the secure connection
     * 
     * @return true if renegotiation successful, false otherwise
     */
    bool renegotiate();

    /**
     * @brief Get the current security level (key size, etc.)
     * 
     * @return Security level information string
     */
    std::string getSecurityLevel() const;

    /**
     * @brief Send multiple buffers using vectored I/O
     * 
     * @param buffers Vector of data buffers to send
     * @return Result<void> Success if all data was sent, error otherwise
     */
    Result<void> sendv(const std::vector<std::vector<uint8_t>>& buffers);

private:
    struct BatchedMessage {
        std::vector<uint8_t> data;
        std::chrono::steady_clock::time_point timestamp;
        
        BatchedMessage(std::vector<uint8_t>&& d) 
            : data(std::move(d)), 
              timestamp(std::chrono::steady_clock::now()) {}
    };

    struct BatchContext {
        std::queue<BatchedMessage> messages;
        size_t currentBatchSize{0};
        std::mutex mutex;
        std::condition_variable cv;
        std::thread batchThread;
        std::atomic<bool> shouldStop{false};
        
        void clear() {
            std::lock_guard<std::mutex> lock(mutex);
            while (!messages.empty()) {
                messages.pop();
            }
            currentBatchSize = 0;
        }
    };

    struct RTTSample {
        std::chrono::steady_clock::time_point sendTime;
        std::chrono::steady_clock::time_point receiveTime;
        size_t recordSize;
        
        std::chrono::microseconds getRTT() const {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                receiveTime - sendTime);
        }
    };

    struct AdaptiveRecordContext {
        std::deque<RTTSample> rttSamples;
        size_t currentRecordSize;
        std::chrono::steady_clock::time_point lastAdjustment;
        std::mutex mutex;
        
        AdaptiveRecordContext(const AdaptiveRecordConfig& config)
            : currentRecordSize(config.initialSize),
              lastAdjustment(std::chrono::steady_clock::now()) {}
              
        void addSample(const RTTSample& sample) {
            std::lock_guard<std::mutex> lock(mutex);
            rttSamples.push_back(sample);
            while (rttSamples.size() > 100) { // Keep last 100 samples
                rttSamples.pop_front();
            }
        }
        
        void clear() {
            std::lock_guard<std::mutex> lock(mutex);
            rttSamples.clear();
        }
    };

    struct VectoredIOContext {
        static constexpr size_t MAX_IOV = 16;  // Maximum number of iovec structures
        std::vector<iovec> iovecs;
        std::vector<std::vector<uint8_t>> encryptedBuffers;
        
        VectoredIOContext() : iovecs(MAX_IOV) {}
        
        void reset() {
            encryptedBuffers.clear();
            std::fill(iovecs.begin(), iovecs.end(), iovec{});
        }
    };

    Result<void> performHandshake();
    Result<void> setupSecureContext(bool isServer);
    bool configureALPN();
    bool setupSessionResumption();
    void updateConnectionState(ConnectionState newState);
    void handleSecurityError(const std::string& operation);
    bool verifyCertificateHostname(const std::string& hostname);
    Result<void> handleSessionResumption();
    void cleanupSecureContext();
    Result<void> initializeBatching();
    void batchingThread();
    Result<void> processBatch();
    Result<void> flushBatch();
    bool shouldBatchMessage(size_t messageSize) const;
    Result<void> initializeAdaptiveRecordSizing();
    void updateRecordSize();
    std::chrono::microseconds calculateAverageRTT() const;
    bool shouldAdjustRecordSize() const;
    void adjustRecordSize(std::chrono::microseconds avgRTT);
    
    Result<void> sendEncryptedv(const std::vector<std::vector<uint8_t>>& buffers);
    Result<void> processVectoredIO(const std::vector<std::vector<uint8_t>>& buffers);
    bool shouldUseVectoredIO(const std::vector<std::vector<uint8_t>>& buffers) const;
    
    std::shared_ptr<TransportProtocol> transport_;
    std::shared_ptr<SecurityManager> security_manager_;
    SecureTransportConfig config_;
    std::shared_ptr<SecureContext> secure_context_;
    ConnectionState state_;
    TransportError last_error_;
    std::string last_error_message_;
    std::function<void(ConnectionState)> state_callback_;
    std::function<void(TransportError, const std::string&)> error_callback_;
    bool is_handshake_complete_;
    std::string negotiated_protocol_;

    // Custom BIO for OpenSSL integration
    struct BIOData;
    std::unique_ptr<BIOData> bio_data_;

    std::unique_ptr<BatchContext> batchContext_;
    std::unique_ptr<AdaptiveRecordContext> adaptiveContext_;
    std::unique_ptr<VectoredIOContext> vectoredContext_;
};

} // namespace core
} // namespace xenocomm 
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <atomic>
#include "xenocomm/utils/result.h"
#include "xenocomm/core/security_config.hpp"

namespace xenocomm {
namespace core {

/**
 * @brief Performance metrics for security operations
 */
struct SecurityMetrics {
    std::atomic<uint64_t> totalEncryptionOps{0};
    std::atomic<uint64_t> totalDecryptionOps{0};
    std::atomic<uint64_t> totalHandshakes{0};
    std::atomic<uint64_t> totalAuthAttempts{0};
    std::atomic<uint64_t> totalAuthCacheHits{0};
    std::atomic<uint64_t> totalBytesEncrypted{0};
    std::atomic<uint64_t> totalBytesDecrypted{0};
    std::atomic<uint64_t> totalHandshakeTime{0};  // in milliseconds
    std::atomic<uint64_t> totalEncryptionTime{0}; // in microseconds
    std::atomic<uint64_t> totalDecryptionTime{0}; // in microseconds
    std::atomic<uint64_t> peakEncryptionLatency{0}; // in microseconds
    std::atomic<uint64_t> peakDecryptionLatency{0}; // in microseconds
    std::atomic<uint64_t> currentConnections{0};
    std::atomic<uint64_t> peakConnections{0};
};

/**
 * @brief Security event types for logging
 */
enum class SecurityEventType {
    HANDSHAKE_START,
    HANDSHAKE_COMPLETE,
    HANDSHAKE_FAILED,
    AUTH_SUCCESS,
    AUTH_FAILURE,
    CERT_VALIDATION_SUCCESS,
    CERT_VALIDATION_FAILURE,
    KEY_ROTATION,
    CONFIG_CHANGE,
    SECURITY_VIOLATION
};

/**
 * @brief Security event data for logging
 */
struct SecurityEvent {
    SecurityEventType type;
    std::chrono::system_clock::time_point timestamp;
    std::string description;
    std::optional<std::string> sourceIp;
    std::optional<std::string> username;
    std::optional<std::string> certificateSubject;
    bool isSensitive{false};
};

/**
 * @brief Supported encryption protocols
 */
enum class EncryptionProtocol {
    TLS_1_2,
    TLS_1_3,
    DTLS_1_2,
    DTLS_1_3
};

/**
 * @brief Supported cipher suites
 */
enum class CipherSuite {
    AES_128_GCM_SHA256,
    AES_256_GCM_SHA384,
    CHACHA20_POLY1305_SHA256,
    // Add more as needed
};

/**
 * @brief Configuration for the SecurityManager
 */
struct SecurityConfig {
    EncryptionProtocol protocol = EncryptionProtocol::TLS_1_3;
    std::vector<CipherSuite> allowedCipherSuites = {
        CipherSuite::AES_256_GCM_SHA384,
        CipherSuite::CHACHA20_POLY1305_SHA256
    };
    std::string certificatePath;  // Path to certificate file
    std::string privateKeyPath;   // Path to private key file
    std::string caPath;           // Path to CA certificate
    bool verifyPeer = true;       // Whether to verify peer certificates
    bool allowSelfSigned = false; // Whether to allow self-signed certificates
    std::chrono::seconds sessionTimeout{3600}; // Session timeout
    uint32_t maxSessionCacheSize{1000}; // Maximum number of cached sessions
};

/**
 * @brief Represents a secure connection context
 */
class SecureContext {
public:
    virtual ~SecureContext() = default;
    virtual Result<void> handshake() = 0;
    virtual Result<std::vector<uint8_t>> encrypt(const std::vector<uint8_t>& data) = 0;
    virtual Result<std::vector<uint8_t>> decrypt(const std::vector<uint8_t>& data) = 0;
    virtual bool isHandshakeComplete() const = 0;
    virtual std::string getPeerCertificateInfo() const = 0;
    virtual CipherSuite getNegotiatedCipherSuite() const = 0;
    virtual bool isSelectiveEncryptionEnabled() const = 0;
    virtual void setSelectiveEncryption(bool enable) = 0;
    virtual const SecurityMetrics& getMetrics() const = 0;
};

/**
 * @brief Manages security operations including encryption, authentication, and monitoring
 */
class SecurityManager {
public:
    /**
     * @brief Creates a SecurityManager instance
     * 
     * @param config Security configuration
     */
    explicit SecurityManager(const SecurityConfig& config);
    virtual ~SecurityManager() = default;

    /**
     * @brief Creates a new secure context for a connection
     * 
     * @param isServer Whether this end is acting as a server
     * @return Result<std::shared_ptr<SecureContext>> New secure context or error
     */
    Result<std::shared_ptr<SecureContext>> createContext(bool isServer);

    /**
     * @brief Updates the security configuration
     * 
     * @param config New configuration to apply
     * @return Result<void> Success or error status
     */
    Result<void> updateConfig(const SecurityConfig& config);

    /**
     * @brief Gets the current security configuration
     * 
     * @return const SecurityConfig& Current configuration
     */
    const SecurityConfig& getConfig() const { return config_; }

    /**
     * @brief Gets the current security metrics
     * 
     * @return const SecurityMetrics& Current metrics
     */
    const SecurityMetrics& getMetrics() const { return metrics_; }

    /**
     * @brief Validates a peer certificate
     * 
     * @param certData Raw certificate data
     * @return Result<void> Success or error status
     */
    Result<void> validatePeerCertificate(const std::vector<uint8_t>& certData);

    /**
     * @brief Generates a self-signed certificate for testing
     * 
     * @param commonName Common name for the certificate
     * @param validityDays Number of days the certificate is valid
     * @return Result<void> Success or error status
     */
    Result<void> generateSelfSignedCert(const std::string& commonName, int validityDays = 365);

    /**
     * @brief Generate a DTLS cookie for a client
     * 
     * @param client Network address of the client
     * @return Result<std::vector<uint8_t>> Generated cookie or error
     */
    Result<std::vector<uint8_t>> generateDtlsCookie(const NetworkAddress& client);

    /**
     * @brief Verify a DTLS cookie from a client
     * 
     * @param cookie Cookie to verify
     * @param source Network address that provided the cookie
     * @return Result<void> Success if cookie is valid, error otherwise
     */
    Result<void> verifyDtlsCookie(const std::vector<uint8_t>& cookie, const NetworkAddress& source);

    /**
     * @brief Gets recent security events
     * 
     * @param maxEvents Maximum number of events to return
     * @param filterType Optional event type to filter by
     * @return std::vector<SecurityEvent> List of recent security events
     */
    std::vector<SecurityEvent> getSecurityEvents(
        size_t maxEvents,
        std::optional<SecurityEventType> filterType = std::nullopt) const;

    /**
     * @brief Resets performance metrics
     */
    void resetMetrics();

    /**
     * @brief Gets the connection pool status
     * 
     * @return std::pair<size_t, size_t> Current and available connections
     */
    std::pair<size_t, size_t> getConnectionPoolStatus() const;

    /**
     * @brief Gets authentication cache statistics
     * 
     * @return std::pair<size_t, float> Cache size and hit rate
     */
    std::pair<size_t, float> getAuthCacheStats() const;

protected:
    /**
     * @brief Logs a security event
     * 
     * @param event Event to log
     */
    virtual void logSecurityEvent(const SecurityEvent& event);

    /**
     * @brief Updates performance metrics
     * 
     * @param operation Type of operation (e.g., "encrypt", "decrypt")
     * @param bytes Number of bytes processed
     * @param duration Operation duration
     */
    virtual void updateMetrics(
        const std::string& operation,
        size_t bytes,
        std::chrono::microseconds duration);

private:
    Result<void> initializeSSL();
    Result<void> loadCertificates();
    void cleanupSSL();
    Result<void> validateConfig(const SecurityConfig& config);
    void initializeMonitoring();
    void cleanupMonitoring();
    
    SecurityConfig config_;
    SecurityMetrics metrics_;
    struct SSLData;
    std::unique_ptr<SSLData> sslData_; // Pimpl for OpenSSL data
    std::vector<SecurityEvent> securityEvents_;
    std::mutex eventsMutex_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> authCache_;
    std::mutex authCacheMutex_;
    std::vector<std::shared_ptr<SecureContext>> connectionPool_;
    std::mutex poolMutex_;
    Result<std::vector<uint8_t>> generateHmac(const std::vector<uint8_t>& data);
    std::vector<uint8_t> hmac_key_; // HMAC key for cookie generation
    std::chrono::seconds cookie_lifetime_{300}; // Cookie lifetime (5 minutes)
};

} // namespace core
} // namespace xenocomm 
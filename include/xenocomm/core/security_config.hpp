#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <optional>

namespace xenocomm {
namespace core {

/**
 * @brief Security level presets for easy configuration
 */
enum class SecurityLevel {
    HIGH,     // Maximum security, may impact performance
    MEDIUM,   // Balanced security and performance
    LOW,      // Optimized for performance
    CUSTOM    // Custom configuration
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
    CHACHA20_POLY1305_SHA256
};

/**
 * @brief Configuration for record batching optimization
 */
struct RecordBatchingConfig {
    bool enabled{true};                     // Enable/disable record batching
    size_t maxBatchSize{16384};            // Maximum size of a batched record
    size_t minMessageSize{1024};           // Minimum message size to trigger batching
    size_t maxMessagesPerBatch{32};        // Maximum number of messages per batch
    std::chrono::milliseconds maxDelay{5}; // Maximum delay before sending a partial batch
};

/**
 * @brief Configuration for adaptive record sizing
 */
struct AdaptiveRecordConfig {
    bool enabled{true};                     // Enable/disable adaptive record sizing
    size_t minSize{1024};                  // Minimum record size
    size_t maxSize{16384};                 // Maximum record size
    size_t initialSize{4096};              // Initial record size
    std::chrono::milliseconds rttWindow{1000}; // Window for RTT measurements
    float growthFactor{1.5f};              // Factor to increase size by
    float shrinkFactor{0.75f};             // Factor to decrease size by
};

/**
 * @brief Configuration for authentication caching
 */
struct AuthCacheConfig {
    bool enabled{true};                     // Enable/disable auth caching
    size_t maxCacheSize{10000};            // Maximum number of cached entries
    std::chrono::seconds cacheTimeout{300}; // How long to cache auth results
    bool useSharedCache{false};            // Use shared cache across instances
};

/**
 * @brief Configuration for connection pooling
 */
struct ConnectionPoolConfig {
    bool enabled{true};                     // Enable/disable connection pooling
    size_t minPoolSize{5};                 // Minimum number of connections to maintain
    size_t maxPoolSize{50};                // Maximum number of connections
    std::chrono::seconds maxIdleTime{300}; // Maximum time a connection can be idle
    bool validateOnBorrow{true};           // Validate connections when borrowed
};

/**
 * @brief Configuration for security monitoring and logging
 */
struct SecurityMonitorConfig {
    bool enablePerformanceMetrics{true};    // Collect performance metrics
    bool enableSecurityEvents{true};        // Log security events
    bool enableAuditLog{true};             // Enable detailed audit logging
    std::string logLevel{"INFO"};          // Log level for security events
    bool maskSensitiveData{true};          // Mask sensitive data in logs
    size_t maxLogSize{10 * 1024 * 1024};   // Maximum log file size
    size_t maxLogFiles{5};                 // Maximum number of log files to keep
};

/**
 * @brief Main security configuration structure
 */
struct SecurityConfig {
    // Basic security settings
    SecurityLevel level{SecurityLevel::MEDIUM};
    EncryptionProtocol protocol{EncryptionProtocol::TLS_1_3};
    std::vector<CipherSuite> allowedCipherSuites{
        CipherSuite::AES_256_GCM_SHA384,
        CipherSuite::CHACHA20_POLY1305_SHA256
    };
    
    // Certificate settings
    std::string certificatePath;
    std::string privateKeyPath;
    std::string trustedCAsPath;
    bool verifyPeer{true};
    bool allowSelfSigned{false};
    
    // Session settings
    std::chrono::milliseconds handshakeTimeout{5000};
    std::chrono::seconds sessionTimeout{3600};
    bool enableSessionTickets{true};
    bool enableOCSPStapling{true};
    std::vector<std::string> alpnProtocols;
    uint32_t maxSessionCacheSize{1000};
    
    // DTLS specific settings
    std::chrono::seconds cookieLifetime{300};
    size_t maxDTLSRetransmits{5};
    std::chrono::milliseconds initialRTT{100};
    
    // Performance optimization settings
    RecordBatchingConfig recordBatching;
    AdaptiveRecordConfig adaptiveRecord;
    AuthCacheConfig authCache;
    ConnectionPoolConfig connectionPool;
    bool enableVectoredIO{true};
    bool enableSelectiveEncryption{true};
    
    // Monitoring and logging
    SecurityMonitorConfig monitoring;
    
    /**
     * @brief Apply a security level preset
     * 
     * @param newLevel Security level to apply
     */
    void applySecurityLevel(SecurityLevel newLevel) {
        level = newLevel;
        switch (newLevel) {
            case SecurityLevel::HIGH:
                protocol = EncryptionProtocol::TLS_1_3;
                allowedCipherSuites = {CipherSuite::AES_256_GCM_SHA384};
                verifyPeer = true;
                allowSelfSigned = false;
                enableSessionTickets = false;
                enableOCSPStapling = true;
                recordBatching.enabled = false;
                adaptiveRecord.enabled = false;
                authCache.enabled = false;
                enableSelectiveEncryption = false;
                monitoring.enableAuditLog = true;
                monitoring.maskSensitiveData = true;
                break;
                
            case SecurityLevel::MEDIUM:
                protocol = EncryptionProtocol::TLS_1_3;
                allowedCipherSuites = {
                    CipherSuite::AES_256_GCM_SHA384,
                    CipherSuite::CHACHA20_POLY1305_SHA256
                };
                verifyPeer = true;
                allowSelfSigned = false;
                enableSessionTickets = true;
                enableOCSPStapling = true;
                recordBatching.enabled = true;
                adaptiveRecord.enabled = true;
                authCache.enabled = true;
                enableSelectiveEncryption = true;
                monitoring.enableAuditLog = true;
                monitoring.maskSensitiveData = true;
                break;
                
            case SecurityLevel::LOW:
                protocol = EncryptionProtocol::TLS_1_2;
                allowedCipherSuites = {
                    CipherSuite::AES_128_GCM_SHA256,
                    CipherSuite::AES_256_GCM_SHA384,
                    CipherSuite::CHACHA20_POLY1305_SHA256
                };
                verifyPeer = true;
                allowSelfSigned = true;
                enableSessionTickets = true;
                enableOCSPStapling = false;
                recordBatching.enabled = true;
                adaptiveRecord.enabled = true;
                authCache.enabled = true;
                enableSelectiveEncryption = true;
                monitoring.enableAuditLog = false;
                monitoring.maskSensitiveData = true;
                break;
                
            case SecurityLevel::CUSTOM:
                // Keep current settings
                break;
        }
    }
    
    /**
     * @brief Validate the configuration
     * 
     * @return std::optional<std::string> Error message if invalid, nullopt if valid
     */
    std::optional<std::string> validate() const {
        if (certificatePath.empty() && !allowSelfSigned) {
            return "Certificate path is required unless self-signed certificates are allowed";
        }
        
        if (allowedCipherSuites.empty()) {
            return "At least one cipher suite must be allowed";
        }
        
        if (handshakeTimeout.count() <= 0) {
            return "Handshake timeout must be positive";
        }
        
        if (sessionTimeout.count() <= 0) {
            return "Session timeout must be positive";
        }
        
        if (maxSessionCacheSize == 0) {
            return "Session cache size must be positive";
        }
        
        if (recordBatching.enabled) {
            if (recordBatching.maxBatchSize < recordBatching.minMessageSize) {
                return "Record batching max size must be greater than min message size";
            }
            if (recordBatching.maxMessagesPerBatch == 0) {
                return "Record batching max messages must be positive";
            }
        }
        
        if (adaptiveRecord.enabled) {
            if (adaptiveRecord.maxSize < adaptiveRecord.minSize) {
                return "Adaptive record max size must be greater than min size";
            }
            if (adaptiveRecord.initialSize < adaptiveRecord.minSize ||
                adaptiveRecord.initialSize > adaptiveRecord.maxSize) {
                return "Adaptive record initial size must be between min and max size";
            }
            if (adaptiveRecord.growthFactor <= 1.0f || adaptiveRecord.shrinkFactor >= 1.0f) {
                return "Invalid adaptive record growth/shrink factors";
            }
        }
        
        if (authCache.enabled && authCache.maxCacheSize == 0) {
            return "Auth cache size must be positive when enabled";
        }
        
        if (connectionPool.enabled) {
            if (connectionPool.maxPoolSize < connectionPool.minPoolSize) {
                return "Connection pool max size must be greater than min size";
            }
            if (connectionPool.maxPoolSize == 0) {
                return "Connection pool max size must be positive";
            }
        }
        
        return std::nullopt;
    }
};

} // namespace core
} // namespace xenocomm 
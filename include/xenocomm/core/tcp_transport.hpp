#ifndef XENOCOMM_CORE_TCP_TRANSPORT_HPP
#define XENOCOMM_CORE_TCP_TRANSPORT_HPP

#include "xenocomm/core/transport_protocol.hpp"
#include <string>
#include <atomic>
#include <cstdint>
#include <utility>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include <queue>
#include <future>
#include <functional>
#include <condition_variable>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace xenocomm {
namespace core {

/**
 * @brief TCP transport implementation providing reliable, stream-based communication.
 * 
 * This class provides a TCP socket-based transport layer with configurable timeouts,
 * connection pooling, and error handling. It ensures thread safety for concurrent 
 * operations and proper resource cleanup. Supports both synchronous and asynchronous
 * operations.
 */
class TCPTransport : public TransportProtocol {
public:
    /**
     * @brief Configuration for a TCP connection pool
     */
    struct PoolConfig {
        size_t maxConnections = 10;        ///< Maximum number of connections in the pool
        size_t initialConnections = 1;     ///< Initial number of connections to create
        uint32_t connectionTimeout = 5000; ///< Connection timeout in milliseconds
        uint32_t idleTimeout = 60000;     ///< Time before an idle connection is closed (ms)
        bool validateOnBorrow = true;      ///< Whether to validate connections when borrowed
        bool validateOnReturn = false;     ///< Whether to validate connections when returned
        bool enableHealthMonitoring = true; ///< Whether to enable health monitoring
        uint32_t healthCheckInterval = 5000; ///< Health check interval in milliseconds
        uint32_t maxReconnectAttempts = 3; ///< Maximum number of reconnection attempts
        uint32_t reconnectDelayMs = 1000;  ///< Initial delay between reconnection attempts
    };

    /**
     * @brief Represents the state of a TCP connection
     */
    enum class ConnectionState {
        DISCONNECTED,  ///< Connection is closed
        CONNECTING,    ///< Connection is being established
        CONNECTED,     ///< Connection is established and ready
        DISCONNECTING, ///< Connection is being closed gracefully
        RECONNECTING,  ///< Connection is being re-established after failure
        ERROR         ///< Connection is in error state
    };

    /**
     * @brief Structure to track connection information
     */
    struct ConnectionInfo {
#ifdef _WIN32
        SOCKET socket = INVALID_SOCKET;
#else
        int socket = -1;
#endif
        std::string endpoint;
        ConnectionState state = ConnectionState::DISCONNECTED;
        std::chrono::steady_clock::time_point lastUsed;
        std::chrono::steady_clock::time_point created;
        size_t totalBytesReceived = 0;
        size_t totalBytesSent = 0;
        uint32_t errorCount = 0;
        bool inUse = false;
        TransportError lastError = TransportError::NONE;
        std::string lastErrorDetails;
    };

    /**
     * @brief Asynchronous operation result type
     */
    template<typename T>
    using AsyncResult = std::future<T>;

    /**
     * @brief Callback type for asynchronous operations
     */
    template<typename T>
    using AsyncCallback = std::function<void(T, const std::string&)>;

    /**
     * @brief Statistics for a connection pool
     */
    struct PoolStats {
        size_t activeConnections;      ///< Currently active connections
        size_t availableConnections;   ///< Available connections in the pool
        size_t totalCreated;          ///< Total connections created
        size_t failedAttempts;        ///< Failed connection attempts
        double avgResponseTime;        ///< Average response time in milliseconds
        size_t totalErrors;           ///< Total errors across all connections
        size_t idleConnections;       ///< Number of idle connections
    };

    /**
     * @brief Configuration for async operations
     */
    struct AsyncConfig {
        uint32_t operationTimeout = 30000;  ///< Default timeout for async operations (ms)
        size_t maxPendingOperations = 1000; ///< Maximum number of pending async operations
        bool enableBatching = false;        ///< Enable batching of async operations
        size_t batchSize = 10;              ///< Maximum operations per batch
        uint32_t priorityLevels = 3;        ///< Number of priority levels (1-10)
    };

    /**
     * @brief Priority levels for async operations
     */
    enum class AsyncPriority {
        LOW = 0,
        MEDIUM = 1,
        HIGH = 2
    };

    /**
     * @brief Result of an async operation that can be cancelled
     */
    template<typename T>
    class CancellableAsyncResult {
    public:
        bool cancel() { return promise_.valid() ? promise_.cancel() : false; }
        bool isCancelled() const { return cancelled_; }
        T get() { return promise_.get(); }
        bool isReady() const { return promise_.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    private:
        std::future<T> promise_;
        std::atomic<bool> cancelled_{false};
    };

    /**
     * @brief Constructs a new TCP transport instance.
     */
    TCPTransport();

    /**
     * @brief Destructor that ensures proper cleanup of resources.
     */
    ~TCPTransport() override;

    // Move operations
    TCPTransport(TCPTransport&& other) noexcept;
    TCPTransport& operator=(TCPTransport&& other) noexcept;

    // Delete copy operations
    TCPTransport(const TCPTransport&) = delete;
    TCPTransport& operator=(const TCPTransport&) = delete;

    /**
     * @brief Constructs a TCP transport with connection pool configuration
     */
    explicit TCPTransport(const PoolConfig& config);

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

    // TCP-specific methods
    std::shared_ptr<ConnectionInfo> acquireConnection(const std::string& endpoint);
    void releaseConnection(std::shared_ptr<ConnectionInfo> connection);
    std::string getPoolStats() const;
    PoolStats getDetailedPoolStats() const;
    bool warmupConnections(const std::string& endpoint, size_t numConnections);
    std::map<std::string, bool> checkPoolHealth() const;

    // Async operations
    std::future<bool> connectAsync(const std::string& endpoint, uint32_t socketTimeoutMs = 5000);
    std::future<bool> sendAsync(const uint8_t* data, size_t size);
    std::future<bool> sendAsync(const std::shared_ptr<ConnectionInfo>& connection, 
                               const uint8_t* data, size_t size);
    std::future<size_t> receiveAsync(uint8_t* buffer, size_t size);
    std::future<size_t> receiveAsync(const std::shared_ptr<ConnectionInfo>& connection,
                                    uint8_t* buffer, size_t size);

    void setAsyncConfig(const AsyncConfig& config);

private:
    /**
     * @brief Parse endpoint string into host and port
     */
    std::pair<std::string, uint16_t> parseEndpoint(const std::string& endpoint);

    /**
     * @brief Validate the current state for an operation
     */
    bool validateState(const std::string& operation) const;

    /**
     * @brief Set socket options for TCP-specific behavior
     */
    bool setSocketOptions(uint32_t socketTimeoutMs);

    /**
     * @brief Bind socket to local port if specified
     */
    bool bindSocket();

    /**
     * @brief Set non-blocking mode for socket
     */
    bool setNonBlocking(bool nonBlocking);

    /**
     * @brief Create a new connection to the specified endpoint
     */
    std::shared_ptr<ConnectionInfo> createConnection(const std::string& endpoint);

    /**
     * @brief Validate an existing connection
     */
    bool validateConnection(const std::shared_ptr<ConnectionInfo>& connection);

    /**
     * @brief Clean up idle connections
     */
    void cleanupIdleConnections();

    /**
     * @brief Set error state with code and message
     */
    void setError(TransportError code, const std::string& message);

    /**
     * @brief Get system error message
     */
    std::string getSystemError() const;

    /**
     * @brief Map system error to TransportError
     */
    TransportError mapSystemError() const;

    /**
     * @brief Update connection state and notify callback
     */
    void updateState(ConnectionState newState);

    /**
     * @brief Perform health check operations
     */
    bool performHealthCheck();

    /**
     * @brief Start health monitoring if enabled
     */
    void startHealthMonitoring();

    /**
     * @brief Stop health monitoring
     */
    void stopHealthMonitoring();

    /**
     * @brief Validate and repair a connection if needed
     */
    bool validateAndRepairConnection(std::shared_ptr<ConnectionInfo> connection);

    /**
     * @brief Process priority queues for async operations
     */
    void processPriorityQueues();

    /**
     * @brief Update response time statistics
     */
    void updateResponseStats(const std::string& endpoint, std::chrono::milliseconds responseTime);

    /**
     * @brief Close the underlying socket cleanly.
     */
    void closeSocket();

    /**
     * @brief Perform a graceful shutdown of the connection.
     */
    bool gracefulShutdown();

    // Member variables
#ifdef _WIN32
    SOCKET socket_{INVALID_SOCKET};
    bool wsaInitialized_{false};
#else
    int socket_{-1};
#endif

    std::atomic<bool> connected_{false};
    uint16_t localPort_{0};
    mutable std::string lastError_; ///< Made mutable for setError in const methods
    std::string currentEndpoint_;
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED};
    mutable std::atomic<TransportError> lastErrorCode_{TransportError::NONE}; ///< Made mutable for setError in const methods
    mutable std::string lastErrorDetails_; ///< Made mutable for setError in const methods
    std::function<void(ConnectionState)> stateCallback_;
    std::function<void(TransportError, const std::string&)> errorCallback_;
    mutable std::mutex callbackMutex_;
    std::unique_ptr<std::thread> healthMonitorThread_;
    std::atomic<bool> stopHealthMonitor_{false};
    std::chrono::steady_clock::time_point lastHealthCheck_;
    ConnectionConfig config_;

    // Connection pool members
    PoolConfig poolConfig_;
    std::map<std::string, std::vector<std::shared_ptr<ConnectionInfo>>> connectionPool_;
    std::queue<std::shared_ptr<ConnectionInfo>> availableConnections_;
    mutable std::mutex poolMutex_;
    std::atomic<size_t> activeConnections_{0};
    std::atomic<size_t> totalConnectionsCreated_{0};
    std::atomic<size_t> failedConnectionAttempts_{0};
    std::chrono::steady_clock::time_point lastCleanup_;

    // Async operation members
    AsyncWorkerPool asyncWorkerPool_;
    AsyncConfig asyncConfig_;
    std::map<AsyncPriority, std::queue<std::function<void()>>> priorityQueues_;
    std::atomic<size_t> pendingAsyncOperations_{0};
    std::map<std::string, std::chrono::milliseconds> avgResponseTimes_;
    mutable std::mutex asyncMutex_;
};

} // namespace core
} // namespace xenocomm

#endif // XENOCOMM_CORE_TCP_TRANSPORT_HPP 
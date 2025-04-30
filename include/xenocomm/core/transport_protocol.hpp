#ifndef XENOCOMM_CORE_TRANSPORT_PROTOCOL_HPP
#define XENOCOMM_CORE_TRANSPORT_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace xenocomm {
namespace core {

/**
 * @brief Error codes for transport operations
 */
enum class TransportError {
    NONE = 0,                    ///< No error
    CONNECTION_REFUSED,          ///< Connection actively refused by peer
    CONNECTION_TIMEOUT,          ///< Connection attempt timed out
    CONNECTION_RESET,            ///< Connection was reset by peer
    NETWORK_UNREACHABLE,        ///< Network is unreachable
    HOST_UNREACHABLE,           ///< Host is unreachable
    INVALID_ADDRESS,            ///< Invalid address or endpoint format
    SOCKET_ERROR,               ///< General socket error
    PERMISSION_DENIED,          ///< Permission denied (e.g., binding to privileged port)
    RESOURCE_ERROR,             ///< System resource error (e.g., out of file descriptors)
    ALREADY_CONNECTED,          ///< Transport is already connected
    NOT_CONNECTED,              ///< Transport is not connected
    SEND_ERROR,                 ///< Error during send operation
    RECEIVE_ERROR,              ///< Error during receive operation
    BUFFER_OVERFLOW,            ///< Buffer overflow during operation
    INVALID_ARGUMENT,           ///< Invalid argument provided
    SYSTEM_ERROR,               ///< Unspecified system error
    UNKNOWN_ERROR,              ///< Unknown error occurred
    INVALID_PARAMETER,          ///< Invalid parameter provided
    TIMEOUT,                    ///< Operation timed out
    ADDRESS_IN_USE,            ///< Address already in use
    BUFFER_FULL,               ///< Buffer is full
    MESSAGE_TOO_LARGE,         ///< Message size exceeds limit
    INVALID_STATE,             ///< Invalid state for operation
    RESOLUTION_FAILED,         ///< DNS resolution failed
    CONNECTION_FAILED,         ///< Connection attempt failed
    WOULD_BLOCK,               ///< Operation would block (non-blocking mode)
    CONNECTION_CLOSED,         ///< Connection closed by peer gracefully
    RECONNECTION_FAILED,       ///< Reconnection attempt failed
    BIND_FAILED,               ///< Failed to bind socket to address/port
    SHUTDOWN_FAILED,           ///< Failed to shutdown socket gracefully
    INITIALIZATION_FAILED,     ///< Initialization failed (e.g., WSAStartup)
    UNKNOWN                    ///< Unknown or unspecified error
};

/**
 * @brief Connection state for transport protocols
 */
enum class ConnectionState {
    DISCONNECTED,               ///< Not connected
    CONNECTING,                 ///< Connection in progress
    CONNECTED,                  ///< Successfully connected
    DISCONNECTING,             ///< Disconnection in progress
    ERROR,                      ///< Error state
    RECONNECTING               ///< Attempting to reconnect
};

/**
 * @brief Configuration for transport connections.
 */
struct ConnectionConfig {
    /**
     * @brief Timeout in milliseconds for connection operations.
     */
    uint32_t connectionTimeoutMs = 5000;

    /**
     * @brief Local port to bind to (optional, 0 means system-assigned).
     */
    uint16_t localPort = 0;

    /**
     * @brief Maximum number of reconnection attempts.
     */
    uint32_t maxReconnectAttempts = 3;

    /**
     * @brief Delay between reconnection attempts in milliseconds.
     */
    uint32_t reconnectDelayMs = 1000;

    /**
     * @brief Whether to enable automatic reconnection.
     */
    bool autoReconnect = true;

    /**
     * @brief Whether to enable connection health monitoring.
     */
    bool healthMonitoring = true;

    /**
     * @brief Interval for health checks in milliseconds.
     */
    uint32_t healthCheckIntervalMs = 5000;
};

/**
 * @brief Abstract interface for transport protocol implementations.
 * 
 * This interface defines the common operations that all transport protocols
 * must support, such as connecting, disconnecting, and transferring data.
 */
class TransportProtocol {
public:
    virtual ~TransportProtocol() = default;

    /**
     * @brief Connect to a remote endpoint.
     * 
     * @param endpoint The remote endpoint to connect to (format: "host:port").
     * @param config Configuration for the connection.
     * @return true if connection successful, false otherwise.
     * @throws std::runtime_error if connection fails.
     */
    virtual bool connect(const std::string& endpoint, const ConnectionConfig& config) = 0;

    /**
     * @brief Disconnect from the current endpoint.
     * 
     * @return true if disconnection successful, false otherwise.
     */
    virtual bool disconnect() = 0;

    /**
     * @brief Check if currently connected.
     * 
     * @return true if connected, false otherwise.
     */
    virtual bool isConnected() const = 0;

    /**
     * @brief Send data to the connected endpoint.
     * 
     * @param data The data to send.
     * @param size The size of the data in bytes.
     * @return Number of bytes sent, or -1 on error.
     * @throws std::runtime_error if send fails.
     */
    virtual ssize_t send(const uint8_t* data, size_t size) = 0;

    /**
     * @brief Receive data from the connected endpoint.
     * 
     * @param buffer The buffer to store received data.
     * @param size The maximum size to receive.
     * @return Number of bytes received, or -1 on error.
     * @throws std::runtime_error if receive fails.
     */
    virtual ssize_t receive(uint8_t* buffer, size_t size) = 0;

    /**
     * @brief Get the last error message.
     * 
     * @return The last error message.
     */
    virtual std::string getLastError() const = 0;

    /**
     * @brief Set the local port for the transport.
     * 
     * Must be called before connect().
     * 
     * @param port The local port to bind to (0 means system-assigned).
     * @return true if successful, false otherwise.
     */
    virtual bool setLocalPort(uint16_t port) = 0;

    /**
     * @brief Get the current connection state.
     * 
     * @return Current connection state
     */
    virtual ConnectionState getState() const = 0;

    /**
     * @brief Get the last error code.
     * 
     * @return Last error code
     */
    virtual TransportError getLastErrorCode() const = 0;

    /**
     * @brief Get detailed error information.
     * 
     * @return Struct containing error details
     */
    virtual std::string getErrorDetails() const = 0;

    /**
     * @brief Attempt to reconnect to the last endpoint.
     * 
     * @param maxAttempts Maximum number of reconnection attempts
     * @param delayMs Delay between attempts in milliseconds
     * @return true if reconnection successful, false otherwise
     */
    virtual bool reconnect(uint32_t maxAttempts = 3, uint32_t delayMs = 1000) = 0;

    /**
     * @brief Set callback for connection state changes.
     * 
     * @param callback Function to call on state changes
     */
    virtual void setStateCallback(std::function<void(ConnectionState)> callback) = 0;

    /**
     * @brief Set callback for error events.
     * 
     * @param callback Function to call on errors
     */
    virtual void setErrorCallback(std::function<void(TransportError, const std::string&)> callback) = 0;

    /**
     * @brief Check the health of the connection.
     * 
     * @return true if connection is healthy, false otherwise
     */
    virtual bool checkHealth() = 0;

protected:
    TransportProtocol() = default;
    TransportProtocol(const TransportProtocol&) = delete;
    TransportProtocol& operator=(const TransportProtocol&) = delete;
    TransportProtocol(TransportProtocol&&) = delete;
    TransportProtocol& operator=(TransportProtocol&&) = delete;
};

} // namespace core
} // namespace xenocomm

#endif // XENOCOMM_CORE_TRANSPORT_PROTOCOL_HPP 
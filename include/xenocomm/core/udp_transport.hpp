#ifndef XENOCOMM_CORE_UDP_TRANSPORT_HPP
#define XENOCOMM_CORE_UDP_TRANSPORT_HPP

#include "xenocomm/core/transport_protocol.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include <memory>
#include <atomic>
#include <system_error>
#include <functional>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

namespace xenocomm {
namespace core {

/**
 * @brief UDP transport implementation for XenoComm
 * 
 * Provides UDP-based communication with configurable timeouts and error handling.
 * Supports unicast, broadcast, and multicast communication modes.
 * Thread-safe for concurrent send/receive operations.
 */
class UDPTransport : public TransportProtocol {
public:
    /**
     * @brief Construct a new UDP transport instance
     */
    UDPTransport();

    /**
     * @brief Destroy the UDP transport instance and cleanup resources
     */
    ~UDPTransport() override;

    // Prevent copying
    UDPTransport(const UDPTransport&) = delete;
    UDPTransport& operator=(const UDPTransport&) = delete;

    // Allow moving
    UDPTransport(UDPTransport&& other) noexcept;
    UDPTransport& operator=(UDPTransport&& other) noexcept;

    /**
     * @brief Connect to a remote endpoint
     * 
     * For UDP, this sets up the remote endpoint information and validates
     * the connection can be established. Note that UDP is connectionless,
     * so this primarily configures the transport for communication.
     * 
     * @param endpoint Remote endpoint in format "host:port"
     * @param config Connection configuration including timeout
     * @return true if setup successful, false otherwise
     * @throws std::runtime_error if setup fails
     */
    bool connect(const std::string& endpoint, const ConnectionConfig& config) override;

    /**
     * @brief Disconnect from the remote endpoint
     * 
     * For UDP, this clears the remote endpoint information and resets
     * the transport state.
     * 
     * @return true if cleanup successful, false otherwise
     */
    bool disconnect() override;

    /**
     * @brief Check if configured with a remote endpoint
     * 
     * @return true if configured with remote endpoint, false otherwise
     */
    bool isConnected() const override;

    /**
     * @brief Send data to the remote endpoint
     * 
     * Sends a UDP datagram to the configured remote endpoint. If the data
     * exceeds the maximum UDP datagram size, it will be truncated.
     * 
     * @param data Pointer to the data to send
     * @param size Size of the data in bytes
     * @return Number of bytes sent if successful, negative value on error
     * @throws std::runtime_error if send fails or not connected
     */
    ssize_t send(const uint8_t* data, size_t size) override;

    /**
     * @brief Receive data from the remote endpoint
     * 
     * Receives a UDP datagram from the configured remote endpoint.
     * Blocks until data is received or timeout occurs.
     * 
     * @param buffer Buffer to store received data
     * @param size Maximum size to receive
     * @return Number of bytes received if successful, negative value on error
     * @throws std::runtime_error if receive fails or not connected
     */
    ssize_t receive(uint8_t* buffer, size_t size) override;

    /**
     * @brief Get the last error message
     * 
     * Thread-safe access to the last error message.
     * 
     * @return Error message string
     */
    std::string getLastError() const override;

    /**
     * @brief Set the local port for binding
     * 
     * Must be called before connect(). Thread-safe.
     * 
     * @param port Local port number (0 for system-assigned)
     * @return true if successful, false otherwise
     */
    bool setLocalPort(uint16_t port) override;

    /**
     * @brief Join a multicast group
     * 
     * @param groupAddr Multicast group address (e.g., "239.0.0.1")
     * @return true if successful, false otherwise
     */
    bool joinMulticastGroup(const std::string& groupAddr);

    /**
     * @brief Leave a multicast group
     * 
     * @param groupAddr Multicast group address (e.g., "239.0.0.1")
     * @return true if successful, false otherwise
     */
    bool leaveMulticastGroup(const std::string& groupAddr);

    /**
     * @brief Set the Time-To-Live (TTL) for multicast packets
     * 
     * @param ttl TTL value (1-255)
     * @return true if successful, false otherwise
     */
    bool setMulticastTTL(int ttl);

    /**
     * @brief Enable/disable multicast loopback
     * 
     * When enabled, multicast packets will be received by the sending host
     * if it is a member of the multicast group.
     * 
     * @param enable true to enable loopback, false to disable
     * @return true if successful, false otherwise
     */
    bool setMulticastLoopback(bool enable);

    // New methods from TransportProtocol interface
    ConnectionState getState() const override;
    TransportError getLastErrorCode() const override;
    std::string getErrorDetails() const override;
    bool reconnect(uint32_t maxAttempts = 3, uint32_t delayMs = 1000) override;
    void setStateCallback(std::function<void(ConnectionState)> callback) override;
    void setErrorCallback(std::function<void(TransportError, const std::string&)> callback) override;
    bool checkHealth() override;

    // Implementations for missing TransportProtocol pure virtuals
    bool getPeerAddress(std::string& address, uint16_t& port) override;
    int getSocketFd() const override;
    bool setNonBlocking(bool nonBlocking) override;
    bool setReceiveTimeout(const std::chrono::milliseconds& timeout) override;
    bool setSendTimeout(const std::chrono::milliseconds& timeout) override;
    bool setKeepAlive(bool enable) override;
    bool setTcpNoDelay(bool enable) override; // Typically no-op for UDP
    bool setReuseAddress(bool enable) override;
    bool setReceiveBufferSize(size_t size) override;
    bool setSendBufferSize(size_t size) override;

private:
    /**
     * @brief Parse endpoint string into host and port
     * 
     * @param endpoint Endpoint string in format "host:port"
     * @param[out] host Host string
     * @param[out] port Port number
     * @return true if parsing successful, false otherwise
     */
    bool parseEndpoint(const std::string& endpoint, std::string& host, uint16_t& port);

    /**
     * @brief Validate the current state for an operation
     * 
     * Checks if the current connection state allows the specified operation.
     * 
     * @param operation Name of the operation being validated
     * @return true if operation is allowed in current state, false otherwise
     */
    bool validateState(const std::string& operation);

    /**
     * @brief Bind socket to local port if specified
     * 
     * @return true if binding successful or no local port specified, false on error
     */
    bool bindSocket();

    /**
     * @brief Set socket options for UDP-specific behavior
     * 
     * Configures socket buffer sizes, timeouts, and other UDP-specific options
     * 
     * @param socketTimeoutMs Timeout in milliseconds for socket operations
     * @return true if successful, false otherwise
     */
    bool setSocketOptions(uint32_t socketTimeoutMs);

    /**
     * @brief Set the last error message
     * 
     * Thread-safe update of the last error message.
     * 
     * @param error Error message
     */
    void setLastError(const std::string& error);

    /**
     * @brief Close the socket and cleanup resources
     */
    void closeSocket();

    /**
     * @brief Get the system error message
     * 
     * @return System error message string
     */
    std::string getSystemError() const;

    /**
     * @brief Check if an address is a broadcast or multicast address
     * 
     * @param addr IP address to check
     * @return true if broadcast or multicast address, false otherwise
     */
    bool isBroadcastOrMulticast(const in_addr& addr) const;

    /**
     * @brief Convert system error to TransportError
     */
    TransportError mapSystemError() const;

    /**
     * @brief Update connection state and notify callback if set
     */
    void updateState(ConnectionState newState);

    /**
     * @brief Set the last error code and details
     * 
     * @param code Transport error code
     * @param message Error message details
     */
    void setError(TransportError code, const std::string& message);
    
    // Health monitoring functions
    void startHealthMonitor();
    void stopHealthMonitor();
    bool performHealthCheck(); // Ensure only one instance of this remains

    std::atomic<bool> connected_{false};
    mutable std::mutex mutex_;
    mutable std::string lastError_; ///< Made mutable for setError in const methods
    uint16_t localPort_{0};
    std::chrono::milliseconds timeout_{5000}; // Default 5 second timeout

#ifdef _WIN32
    SOCKET socket_{INVALID_SOCKET};
    struct sockaddr_in remoteAddr_;
    bool wsaInitialized_{false};
#else
    int socket_{-1};
    struct sockaddr_in remoteAddr_;
#endif

    // Additional private members for error handling
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED};
    mutable std::atomic<TransportError> lastErrorCode_{TransportError::NONE}; ///< Made mutable for setError in const methods
    mutable std::string lastErrorDetails_; ///< Made mutable for setError in const methods
    std::function<void(ConnectionState)> stateCallback_;
    std::function<void(TransportError, const std::string&)> errorCallback_;
    mutable std::mutex callbackMutex_;
    // Health monitoring members
    std::unique_ptr<std::thread> healthMonitorThread_;
    std::atomic<bool> stopHealthMonitor_{false};
    std::chrono::steady_clock::time_point lastHealthCheck_;
    // Config and endpoint
    ConnectionConfig config_;
    std::string currentEndpoint_;
};

} // namespace core
} // namespace xenocomm

#endif // XENOCOMM_CORE_UDP_TRANSPORT_HPP 
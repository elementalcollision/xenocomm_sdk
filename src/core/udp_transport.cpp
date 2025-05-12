#include "xenocomm/core/udp_transport.hpp"
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <random>

namespace xenocomm {
namespace core {

#ifdef _WIN32
static const SOCKET INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
static const int INVALID_SOCKET_VALUE = -1;
#endif

UDPTransport::UDPTransport() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        setError(TransportError::SYSTEM_ERROR, "Failed to initialize WinSock");
        return;
    }
    wsaInitialized_ = true;
#endif
}

UDPTransport::~UDPTransport() {
    disconnect();
#ifdef _WIN32
    if (wsaInitialized_) {
        WSACleanup();
    }
#endif
}

UDPTransport::UDPTransport(UDPTransport&& other) noexcept
    : connected_(other.connected_.load())
    , lastError_(std::move(other.lastError_))
    , localPort_(other.localPort_)
    , timeout_(other.timeout_)
    , socket_(other.socket_)
    , remoteAddr_(other.remoteAddr_)
#ifdef _WIN32
    , wsaInitialized_(other.wsaInitialized_)
#endif
{
    other.socket_ = 
#ifdef _WIN32
        INVALID_SOCKET;
    other.wsaInitialized_ = false;
#else
        -1;
#endif
    other.connected_ = false;
}

UDPTransport& UDPTransport::operator=(UDPTransport&& other) noexcept {
    if (this != &other) {
        disconnect();
#ifdef _WIN32
        if (wsaInitialized_) {
            WSACleanup();
        }
#endif

        connected_ = other.connected_.load();
        lastError_ = std::move(other.lastError_);
        localPort_ = other.localPort_;
        timeout_ = other.timeout_;
        socket_ = other.socket_;
        remoteAddr_ = other.remoteAddr_;
#ifdef _WIN32
        wsaInitialized_ = other.wsaInitialized_;
#endif

        other.socket_ = 
#ifdef _WIN32
            INVALID_SOCKET;
        other.wsaInitialized_ = false;
#else
            -1;
#endif
        other.connected_ = false;
    }
    return *this;
}

bool UDPTransport::connect(const std::string& endpoint, const ConnectionConfig& config) {
    if (!validateState("connect")) {
        return false;
    }

    if (connected_) {
        setError(TransportError::ALREADY_CONNECTED, "Transport is already connected");
        return false;
    }

    config_ = config;
    std::string host;
    uint16_t port;
    
    if (!parseEndpoint(endpoint, host, port)) {
        setError(TransportError::INVALID_ADDRESS, "Invalid endpoint format");
        return false;
    }

    // Create socket
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == INVALID_SOCKET_VALUE) {
        setError(mapSystemError(), "Failed to create socket");
        return false;
    }

    updateState(ConnectionState::CONNECTING);

    if (!setSocketOptions(config.connectionTimeoutMs)) {
        closeSocket();
        return false;
    }

    if (!bindSocket()) {
        closeSocket();
        return false;
    }

    // Store endpoint for reconnection
    currentEndpoint_ = endpoint;

    // Configure remote address
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &remoteAddr_.sin_addr) != 1) {
        setError(TransportError::INVALID_ADDRESS, "Invalid IP address");
        closeSocket();
        return false;
    }

    connected_ = true;
    updateState(ConnectionState::CONNECTED);

    if (config_.healthMonitoring) {
        startHealthMonitor();
    }

    return true;
}

bool UDPTransport::disconnect() {
    if (!validateState("disconnect")) {
        return false;
    }

    if (!connected_) {
        return true;
    }

    stopHealthMonitor();
    closeSocket();
    connected_ = false;
    updateState(ConnectionState::DISCONNECTED);
    return true;
}

bool UDPTransport::isConnected() const {
    return connected_.load();
}

ssize_t UDPTransport::send(const uint8_t* data, size_t size) {
    if (!validateState("send")) {
        return -1;
    }

    if (!data || size == 0) {
        setError(TransportError::INVALID_PARAMETER, "Invalid send parameters");
        return -1;
    }

    // Check if size exceeds maximum UDP datagram size
    static const size_t MAX_DATAGRAM_SIZE = 65507; // Maximum safe UDP payload size
    if (size > MAX_DATAGRAM_SIZE) {
        setError(TransportError::INVALID_PARAMETER, "Data size exceeds maximum UDP datagram size");
        return -1;
    }

    ssize_t result = sendto(socket_, reinterpret_cast<const char*>(data), size, 0,
                           reinterpret_cast<struct sockaddr*>(&remoteAddr_),
                           sizeof(remoteAddr_));

    if (result < 0) {
        setError(mapSystemError(), "Send operation failed");
        return -1;
    }

    return result;
}

ssize_t UDPTransport::receive(uint8_t* buffer, size_t size) {
    if (!validateState("receive")) {
        return -1;
    }

    if (!buffer || size == 0) {
        setError(TransportError::INVALID_PARAMETER, "Invalid receive parameters");
        return -1;
    }

    struct sockaddr_in sender;
    socklen_t senderLen = sizeof(sender);

    ssize_t result = recvfrom(socket_, reinterpret_cast<char*>(buffer), size, 0,
                             reinterpret_cast<struct sockaddr*>(&sender),
                             &senderLen);

    if (result < 0) {
        setError(mapSystemError(), "Receive operation failed");
        return -1;
    }

    // Check if datagram was truncated
    if (static_cast<size_t>(result) > size) {
        setError(TransportError::INVALID_PARAMETER, "Datagram truncated");
        return -1;
    }

    // Verify sender matches our remote endpoint unless it's a broadcast/multicast packet
    if (!isBroadcastOrMulticast(sender.sin_addr) &&
        sender.sin_addr.s_addr != remoteAddr_.sin_addr.s_addr &&
        sender.sin_port != remoteAddr_.sin_port) {
        setError(TransportError::INVALID_PARAMETER, "Received data from unexpected sender");
        return -1;
    }

    return result;
}

bool UDPTransport::joinMulticastGroup(const std::string& groupAddr) {
    if (!connected_) {
        setError(TransportError::NOT_CONNECTED, "Socket not connected");
        return false;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(groupAddr.c_str());
    
    // Check if it's a valid multicast address (224.0.0.0 to 239.255.255.255)
    uint32_t addr = ntohl(mreq.imr_multiaddr.s_addr);
    if (addr < 0xE0000000 || addr > 0xEFFFFFFF) {
        setError(TransportError::INVALID_PARAMETER, "Invalid multicast address");
        return false;
    }

    // Get the local interface address
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                   reinterpret_cast<char*>(&mreq), sizeof(mreq)) != 0) {
        setError(mapSystemError(), "Failed to join multicast group");
        return false;
    }
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                   &mreq, sizeof(mreq)) != 0) {
        setError(mapSystemError(), "Failed to join multicast group");
        return false;
    }
#endif

    return true;
}

bool UDPTransport::leaveMulticastGroup(const std::string& groupAddr) {
    if (!connected_) {
        setError(TransportError::NOT_CONNECTED, "Socket not connected");
        return false;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(groupAddr.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
                   reinterpret_cast<char*>(&mreq), sizeof(mreq)) != 0) {
        setError(mapSystemError(), "Failed to leave multicast group");
        return false;
    }
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
                   &mreq, sizeof(mreq)) != 0) {
        setError(mapSystemError(), "Failed to leave multicast group");
        return false;
    }
#endif

    return true;
}

bool UDPTransport::setMulticastTTL(int ttl) {
    if (!connected_) {
        setError(TransportError::NOT_CONNECTED, "Socket not connected");
        return false;
    }

    if (ttl < 1 || ttl > 255) {
        setError(TransportError::INVALID_PARAMETER, "Invalid TTL value (must be between 1 and 255)");
        return false;
    }

    unsigned char ttl_value = static_cast<unsigned char>(ttl);

#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_TTL, 
                   reinterpret_cast<char*>(&ttl_value), sizeof(ttl_value)) != 0) {
        setError(mapSystemError(), "Failed to set multicast TTL");
        return false;
    }
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_TTL, 
                   &ttl_value, sizeof(ttl_value)) != 0) {
        setError(mapSystemError(), "Failed to set multicast TTL");
        return false;
    }
#endif

    return true;
}

bool UDPTransport::setMulticastLoopback(bool enable) {
    if (!connected_) {
        setError(TransportError::NOT_CONNECTED, "Socket not connected");
        return false;
    }

    unsigned char loop = enable ? 1 : 0;

#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_LOOP, 
                   reinterpret_cast<char*>(&loop), sizeof(loop)) != 0) {
        setError(mapSystemError(), "Failed to set multicast loopback");
        return false;
    }
#else
    if (setsockopt(socket_, IPPROTO_IP, IP_MULTICAST_LOOP, 
                   &loop, sizeof(loop)) != 0) {
        setError(mapSystemError(), "Failed to set multicast loopback");
        return false;
    }
#endif

    return true;
}

bool UDPTransport::isBroadcastOrMulticast(const in_addr& addr) const {
    uint32_t ip = ntohl(addr.s_addr);
    
    // Check for broadcast address (255.255.255.255)
    if (ip == 0xFFFFFFFF) {
        return true;
    }
    
    // Check for multicast address range (224.0.0.0 to 239.255.255.255)
    if (ip >= 0xE0000000 && ip <= 0xEFFFFFFF) {
        return true;
    }
    
    // Check for subnet broadcast (all bits set in host portion)
    // This is a simplified check and may need to be adjusted based on the subnet mask
    if ((ip & 0xFF) == 0xFF) {
        return true;
    }
    
    return false;
}

std::string UDPTransport::getLastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

bool UDPTransport::setLocalPort(uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        setError(TransportError::ALREADY_CONNECTED, "Cannot set local port while connected");
        return false;
    }
    localPort_ = port;
    return true;
}

bool UDPTransport::parseEndpoint(const std::string& endpoint, std::string& host, uint16_t& port) {
    size_t colonPos = endpoint.find(':');
    if (colonPos == std::string::npos || colonPos == 0 || colonPos == endpoint.length() - 1) {
        setError(TransportError::INVALID_PARAMETER, "Invalid endpoint format. Expected 'host:port'");
        return false;
    }

    host = endpoint.substr(0, colonPos);
    try {
        port = static_cast<uint16_t>(std::stoi(endpoint.substr(colonPos + 1)));
    } catch (const std::exception& e) {
        setError(TransportError::INVALID_PARAMETER, "Invalid port number: " + std::string(e.what()));
        return false;
    }

    return true;
}

bool UDPTransport::bindSocket() {
    if (localPort_ == 0) {
        return true;  // System will assign port
    }

    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(localPort_);

    if (bind(socket_, reinterpret_cast<struct sockaddr*>(&localAddr), sizeof(localAddr)) != 0) {
        setError(mapSystemError(), "Failed to bind to local port");
        return false;
    }

    return true;
}

bool UDPTransport::setSocketOptions(uint32_t socketTimeoutMs) {
    bool success = true;
    int flag = 1;

    // Enable SO_REUSEADDR to allow quick rebinding
#ifdef _WIN32
    success = (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&flag), sizeof(flag)) == 0);
#else
    success = (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == 0);
#endif

    if (!success) {
        setError(mapSystemError(), "Failed to set SO_REUSEADDR");
        return false;
    }

    // Enable SO_BROADCAST for broadcast support
#ifdef _WIN32
    success = (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&flag), sizeof(flag)) == 0);
#else
    success = (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &flag, sizeof(flag)) == 0);
#endif

    if (!success) {
        setError(mapSystemError(), "Failed to set SO_BROADCAST");
        return false;
    }

    // Set send and receive buffer sizes (256KB for UDP to handle larger datagrams)
    int bufferSize = 256 * 1024;
#ifdef _WIN32
    success = (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&bufferSize), sizeof(bufferSize)) == 0) &&
              (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&bufferSize), sizeof(bufferSize)) == 0);
#else
    success = (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize)) == 0) &&
              (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &bufferSize, sizeof(bufferSize)) == 0);
#endif

    if (!success) {
        setError(mapSystemError(), "Failed to set buffer sizes");
        return false;
    }

    // Set send and receive timeouts
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(socketTimeoutMs);
    success = (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == 0) &&
              (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == 0);
#else
    struct timeval tv;
    tv.tv_sec = socketTimeoutMs / 1000;
    tv.tv_usec = (socketTimeoutMs % 1000) * 1000;
    success = (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0) &&
              (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0);
#endif

    if (!success) {
        setError(mapSystemError(), "Failed to set socket timeouts");
        return false;
    }

    timeout_ = std::chrono::milliseconds(socketTimeoutMs);
    return true;
}

void UDPTransport::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastError_ = error;
}

void UDPTransport::closeSocket() {
#ifdef _WIN32
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
#else
    if (socket_ != -1) {
        close(socket_);
        socket_ = -1;
    }
#endif
}

std::string UDPTransport::getSystemError() const {
#ifdef _WIN32
    DWORD error = WSAGetLastError();
    char* msgBuf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&msgBuf), 0, NULL);
    std::string errorMsg = msgBuf ? msgBuf : "Unknown error";
    LocalFree(msgBuf);
    return errorMsg;
#else
    return std::string(strerror(errno));
#endif
}

ConnectionState UDPTransport::getState() const {
    return state_.load();
}

TransportError UDPTransport::getLastErrorCode() const {
    return lastErrorCode_.load();
}

std::string UDPTransport::getErrorDetails() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string details = lastErrorDetails_;
    
    // Add system-specific error information
    if (lastErrorCode_ != TransportError::NONE) {
        details += "\nSystem Error: " + getSystemError();
        details += "\nError Code: " + std::to_string(static_cast<int>(lastErrorCode_.load()));
        
        // Add connection state information
        details += "\nConnection State: ";
        switch (state_) {
            case ConnectionState::CONNECTED:
                details += "CONNECTED";
                break;
            case ConnectionState::CONNECTING:
                details += "CONNECTING";
                break;
            case ConnectionState::DISCONNECTED:
                details += "DISCONNECTED";
                break;
            case ConnectionState::ERROR:
                details += "ERROR";
                break;
            case ConnectionState::RECONNECTING:
                details += "RECONNECTING";
                break;
        }
        
        // Add endpoint information if available
        if (!currentEndpoint_.empty()) {
            details += "\nEndpoint: " + currentEndpoint_;
        }
    }
    
    return details;
}

bool UDPTransport::reconnect(uint32_t maxAttempts, uint32_t delayMs) {
    if (currentEndpoint_.empty()) {
        setError(TransportError::NOT_CONNECTED, "No previous connection to reconnect to");
        return false;
    }

    updateState(ConnectionState::RECONNECTING);

    // Implement exponential backoff for reconnection attempts
    uint32_t baseDelay = delayMs;
    uint32_t maxDelay = 30000; // Maximum delay of 30 seconds
    
    for (uint32_t attempt = 1; attempt <= maxAttempts; ++attempt) {
        if (connect(currentEndpoint_, config_)) {
            return true;
        }

        if (attempt < maxAttempts) {
            // Calculate exponential backoff delay with jitter
            uint32_t backoffDelay = std::min(baseDelay * (1 << (attempt - 1)), maxDelay);
            
            // Add random jitter (±20% of delay)
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uint32_t> jitter(-backoffDelay/5, backoffDelay/5);
            backoffDelay += jitter(gen);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(backoffDelay));
        }
    }

    setError(TransportError::CONNECTION_TIMEOUT, "Reconnection attempts exhausted");
    return false;
}

void UDPTransport::setStateCallback(std::function<void(ConnectionState)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stateCallback_ = std::move(callback);
}

void UDPTransport::setErrorCallback(std::function<void(TransportError, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    errorCallback_ = std::move(callback);
}

bool UDPTransport::checkHealth() {
    return performHealthCheck();
}

TransportError UDPTransport::mapSystemError() const {
    #ifdef _WIN32
    int error = WSAGetLastError();
    switch (error) {
        case WSAECONNRESET:
            return TransportError::CONNECTION_RESET;
        case WSAETIMEDOUT:
            return TransportError::TIMEOUT;
        case WSAECONNREFUSED:
            return TransportError::CONNECTION_REFUSED;
        case WSAEHOSTUNREACH:
            return TransportError::HOST_UNREACHABLE;
        case WSAENETUNREACH:
            return TransportError::NETWORK_UNREACHABLE;
        case WSAEADDRINUSE:
            return TransportError::ADDRESS_IN_USE;
        case WSAEINVAL:
            return TransportError::INVALID_PARAMETER;
        case WSAENOBUFS:
            return TransportError::BUFFER_FULL;
        case WSAEMSGSIZE:
            return TransportError::MESSAGE_TOO_LARGE;
        case WSAEACCES:
            return TransportError::PERMISSION_DENIED;
        default:
            return TransportError::UNKNOWN;
    }
    #else
    switch (errno) {
        case ECONNRESET:
            return TransportError::CONNECTION_RESET;
        case ETIMEDOUT:
            return TransportError::TIMEOUT;
        case ECONNREFUSED:
            return TransportError::CONNECTION_REFUSED;
        case EHOSTUNREACH:
            return TransportError::HOST_UNREACHABLE;
        case ENETUNREACH:
            return TransportError::NETWORK_UNREACHABLE;
        case EADDRINUSE:
            return TransportError::ADDRESS_IN_USE;
        case EINVAL:
            return TransportError::INVALID_PARAMETER;
        case ENOBUFS:
            return TransportError::BUFFER_FULL;
        case EMSGSIZE:
            return TransportError::MESSAGE_TOO_LARGE;
        case EACCES:
            return TransportError::PERMISSION_DENIED;
        default:
            return TransportError::UNKNOWN;
    }
    #endif
}

void UDPTransport::updateState(ConnectionState newState) {
    ConnectionState oldState = state_.load();
    
    // Validate state transition
    bool validTransition = true;
    switch (oldState) {
        case ConnectionState::DISCONNECTED:
            validTransition = (newState == ConnectionState::CONNECTING);
            break;
        case ConnectionState::CONNECTING:
            validTransition = (newState == ConnectionState::CONNECTED ||
                             newState == ConnectionState::ERROR ||
                             newState == ConnectionState::DISCONNECTED);
            break;
        case ConnectionState::CONNECTED:
            validTransition = (newState == ConnectionState::DISCONNECTED ||
                             newState == ConnectionState::ERROR ||
                             newState == ConnectionState::RECONNECTING);
            break;
        case ConnectionState::ERROR:
            validTransition = (newState == ConnectionState::RECONNECTING ||
                             newState == ConnectionState::DISCONNECTED);
            break;
        case ConnectionState::RECONNECTING:
            validTransition = (newState == ConnectionState::CONNECTED ||
                             newState == ConnectionState::ERROR ||
                             newState == ConnectionState::DISCONNECTED);
            break;
    }

    if (!validTransition) {
        std::string message = "Invalid state transition from " + 
                            std::to_string(static_cast<int>(oldState)) +
                            " to " + std::to_string(static_cast<int>(newState));
        setError(TransportError::INVALID_STATE, message);
        return;
    }

    // Update state and notify callback
    state_.store(newState);
    
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (stateCallback_) {
        stateCallback_(newState);
    }

    // Additional actions based on state change
    switch (newState) {
        case ConnectionState::CONNECTED:
            if (config_.healthMonitoring) {
                startHealthMonitor();
            }
            break;
        case ConnectionState::DISCONNECTED:
            stopHealthMonitor();
            break;
        case ConnectionState::ERROR:
            stopHealthMonitor();
            if (config_.autoReconnect && oldState == ConnectionState::CONNECTED) {
                // Schedule reconnection attempt
                std::thread([this]() {
                    reconnect(config_.maxReconnectAttempts, config_.reconnectDelayMs);
                }).detach();
            }
            break;
        default:
            break;
    }
}

bool UDPTransport::validateState(const std::string& operation) {
    ConnectionState currentState = state_.load();
    
    switch (currentState) {
        case ConnectionState::DISCONNECTED:
            if (operation != "connect") {
                setError(TransportError::NOT_CONNECTED, 
                        operation + " failed: Transport is disconnected");
                return false;
            }
            break;
            
        case ConnectionState::ERROR:
            setError(TransportError::INVALID_STATE, 
                    operation + " failed: Transport is in error state");
            return false;
            
        case ConnectionState::CONNECTING:
        case ConnectionState::RECONNECTING:
            if (operation != "disconnect") {
                setError(TransportError::INVALID_STATE, 
                        operation + " failed: Transport is " + 
                        (currentState == ConnectionState::CONNECTING ? "connecting" : "reconnecting"));
                return false;
            }
            break;
            
        case ConnectionState::CONNECTED:
            if (operation == "connect") {
                setError(TransportError::ALREADY_CONNECTED, 
                        "Transport is already connected");
                return false;
            }
            break;
    }
    
    return true;
}

bool UDPTransport::performHealthCheck() {
    if (!connected_) {
        return false;
    }

    // For UDP, we'll check if the socket is still valid and can be used
    uint8_t buffer[1];
    struct sockaddr_in addr = remoteAddr_;
    socklen_t addrLen = sizeof(addr);

    // Try to peek at incoming data without removing it from the queue
    int result = recvfrom(socket_, reinterpret_cast<char*>(buffer), 1, MSG_PEEK,
                         reinterpret_cast<struct sockaddr*>(&addr), &addrLen);

    if (result < 0) {
#ifdef _WIN32
        int error = WSAGetLastError();
        // WSAEWOULDBLOCK is expected for non-blocking sockets with no data
        if (error != WSAEWOULDBLOCK) {
            setError(mapSystemError(), "Health check failed");
            return false;
        }
#else
        // EAGAIN/EWOULDBLOCK is expected for non-blocking sockets with no data
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            setError(mapSystemError(), "Health check failed");
            return false;
        }
#endif
    }

    // Update last successful health check time
    lastHealthCheck_ = std::chrono::steady_clock::now();
    return true;
}

void UDPTransport::startHealthMonitor() {
    if (!config_.healthMonitoring || healthMonitorThread_) {
        return;
    }

    stopHealthMonitor_ = false;
    healthMonitorThread_ = std::make_unique<std::thread>([this]() {
        while (!stopHealthMonitor_) {
            if (!performHealthCheck()) {
                // If health check fails and auto-reconnect is enabled
                if (config_.autoReconnect) {
                    // Try to reconnect with exponential backoff
                    reconnect(config_.maxReconnectAttempts, config_.reconnectDelayMs);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.healthCheckIntervalMs));
        }
    });
}

void UDPTransport::stopHealthMonitor() {
    if (healthMonitorThread_) {
        stopHealthMonitor_ = true;
        if (healthMonitorThread_->joinable()) {
            healthMonitorThread_->join();
        }
        healthMonitorThread_.reset();
    }
}

bool UDPTransport::getPeerAddress(std::string& address, uint16_t& port) {
    // For UDP, if connected, remoteAddr_ holds the peer info.
    // This basic implementation doesn't store it persistently if not 'connected'.
    if (!isConnected() || socket_ == INVALID_SOCKET_VALUE) {
        setError(TransportError::NOT_CONNECTED, "Not connected or socket invalid.");
        return false;
    }
    char ipstr[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &remoteAddr_.sin_addr, ipstr, sizeof(ipstr)) == nullptr) {
        setError(TransportError::SYSTEM_ERROR, "Failed to convert peer IP address.");
        return false;
    }
    address = ipstr;
    port = ntohs(remoteAddr_.sin_port);
    return true;
}

int UDPTransport::getSocketFd() const {
    return socket_;
}

#ifndef _WIN32 // SO_REUSEADDR and other socket options are typically POSIX

bool UDPTransport::setNonBlocking(bool nonBlocking) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) return false;
    flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return (fcntl(socket_, F_SETFL, flags) == 0);
}

bool UDPTransport::setReuseAddress(bool enable) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    int optval = enable ? 1 : 0;
    return (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == 0);
}

bool UDPTransport::setKeepAlive(bool enable) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    // UDP doesn't have keep-alive in the same way TCP does.
    // This could be implemented with application-level pings if necessary.
    // For now, this is a no-op but returns true to indicate the call was 'successful'.
    (void)enable; // Suppress unused parameter warning
    return true; 
}

bool UDPTransport::setTcpNoDelay(bool enable) {
    // Nagle's algorithm is TCP-specific.
    (void)enable; // Suppress unused parameter warning
    return true; // No-op for UDP
}

#else // Windows specific implementations or stubs

bool UDPTransport::setNonBlocking(bool nonBlocking) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    u_long mode = nonBlocking ? 1 : 0;
    return (ioctlsocket(socket_, FIONBIO, &mode) == 0);
}

bool UDPTransport::setReuseAddress(bool enable) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    int optval = enable ? 1 : 0;
    return (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&optval), sizeof(optval)) == 0);
}

bool UDPTransport::setKeepAlive(bool enable) {
    (void)enable;
    return true; // No-op for UDP on Windows as well, typically
}

bool UDPTransport::setTcpNoDelay(bool enable) {
    (void)enable;
    return true; // No-op for UDP
}

#endif

bool UDPTransport::setReceiveTimeout(const std::chrono::milliseconds& timeout) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    timeout_ = timeout; // Store for internal use if needed
#ifdef _WIN32
    DWORD timeoutMs = static_cast<DWORD>(timeout.count());
    return (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) == 0);
#else
    struct timeval tv;
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    return (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0);
#endif
}

bool UDPTransport::setSendTimeout(const std::chrono::milliseconds& timeout) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    // timeout_ member is used for general purposes, can be updated here if logic requires it for sending
#ifdef _WIN32
    DWORD timeoutMs = static_cast<DWORD>(timeout.count());
    return (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) == 0);
#else
    struct timeval tv;
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    return (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0);
#endif
}

bool UDPTransport::setReceiveBufferSize(size_t size) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    int optval = static_cast<int>(size);
#ifdef _WIN32
    return (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&optval), sizeof(optval)) == 0);
#else
    return (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(optval)) == 0);
#endif
}

bool UDPTransport::setSendBufferSize(size_t size) {
    if (socket_ == INVALID_SOCKET_VALUE) return false;
    int optval = static_cast<int>(size);
#ifdef _WIN32
    return (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&optval), sizeof(optval)) == 0);
#else
    return (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval)) == 0);
#endif
}

void UDPTransport::setError(TransportError code, const std::string& message) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    lastErrorCode_ = code;
    lastError_ = message;
    lastErrorDetails_ = message;
    
    if (errorCallback_) {
        errorCallback_(code, message);
    }
}

} // namespace core
} // namespace xenocomm 
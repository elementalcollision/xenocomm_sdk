#include "xenocomm/core/tcp_transport.hpp"
#include <stdexcept>
#include <system_error>
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <future>
#include <cstring>
#include <iostream>
#include <random>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif

// Platform-specific TCP keepalive definitions
#if defined(__APPLE__)
    #define TCP_KEEPALIVE_TIME TCP_KEEPALIVE  // On macOS, TCP_KEEPALIVE is used instead of TCP_KEEPIDLE
#elif defined(__linux__)
    #define TCP_KEEPALIVE_TIME TCP_KEEPIDLE   // On Linux, TCP_KEEPIDLE is the standard name
#endif

namespace xenocomm {
namespace core {

#ifdef _WIN32
static const SOCKET INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
static const int INVALID_SOCKET_VALUE = -1;
#endif

TCPTransport::TCPTransport() : 
    socket_(INVALID_SOCKET_VALUE),
    connected_(false),
    localPort_(0)
#ifdef _WIN32
    , wsaInitialized_(false)
#endif
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        setError(TransportError::INITIALIZATION_FAILED, "Failed to initialize WinSock: " + getSystemError());
        throw std::runtime_error(getLastError());
    }
    wsaInitialized_ = true;
#endif
}

TCPTransport::~TCPTransport() {
    stopHealthMonitoring();
    disconnect();
#ifdef _WIN32
    if (wsaInitialized_) {
        WSACleanup();
    }
#endif
}

TCPTransport::TCPTransport(TCPTransport&& other) noexcept
    : socket_(other.socket_),
      connected_(other.connected_.load()),
      localPort_(other.localPort_),
      lastError_(std::move(other.lastError_)),
      currentEndpoint_(std::move(other.currentEndpoint_)),
      state_(other.state_.load()),
      lastErrorCode_(other.lastErrorCode_.load()),
      lastErrorDetails_(std::move(other.lastErrorDetails_)),
      stateCallback_(std::move(other.stateCallback_)),
      errorCallback_(std::move(other.errorCallback_)),
      config_(std::move(other.config_)),
      poolConfig_(std::move(other.poolConfig_)),
      connectionPool_(std::move(other.connectionPool_)),
      availableConnections_(std::move(other.availableConnections_)),
      activeConnections_(other.activeConnections_.load()),
      totalConnectionsCreated_(other.totalConnectionsCreated_.load()),
      failedConnectionAttempts_(other.failedConnectionAttempts_.load()),
      lastCleanup_(other.lastCleanup_),
      asyncWorkerPool_(other.asyncWorkerPool_),
      asyncConfig_(std::move(other.asyncConfig_)),
      priorityQueues_(std::move(other.priorityQueues_)),
      pendingAsyncOperations_(other.pendingAsyncOperations_.load()),
      avgResponseTimes_(std::move(other.avgResponseTimes_)) {
#ifdef _WIN32
    wsaInitialized_ = other.wsaInitialized_;
    other.wsaInitialized_ = false;
#endif
    other.socket_ = 
#ifdef _WIN32
        INVALID_SOCKET;
#else
        -1;
#endif
    other.connected_ = false;
    other.state_ = ConnectionState::DISCONNECTED;
}

TCPTransport& TCPTransport::operator=(TCPTransport&& other) noexcept {
    if (this != &other) {
        stopHealthMonitoring();
        disconnect();

        socket_ = other.socket_;
        connected_ = other.connected_.load();
        localPort_ = other.localPort_;
        lastError_ = std::move(other.lastError_);
        currentEndpoint_ = std::move(other.currentEndpoint_);
        state_ = other.state_.load();
        lastErrorCode_ = other.lastErrorCode_.load();
        lastErrorDetails_ = std::move(other.lastErrorDetails_);
        stateCallback_ = std::move(other.stateCallback_);
        errorCallback_ = std::move(other.errorCallback_);
        config_ = std::move(other.config_);
        poolConfig_ = std::move(other.poolConfig_);
        connectionPool_ = std::move(other.connectionPool_);
        availableConnections_ = std::move(other.availableConnections_);
        activeConnections_ = other.activeConnections_.load();
        totalConnectionsCreated_ = other.totalConnectionsCreated_.load();
        failedConnectionAttempts_ = other.failedConnectionAttempts_.load();
        lastCleanup_ = other.lastCleanup_;
        asyncWorkerPool_ = other.asyncWorkerPool_;
        asyncConfig_ = std::move(other.asyncConfig_);
        priorityQueues_ = std::move(other.priorityQueues_);
        pendingAsyncOperations_ = other.pendingAsyncOperations_.load();
        avgResponseTimes_ = std::move(other.avgResponseTimes_);

#ifdef _WIN32
        wsaInitialized_ = other.wsaInitialized_;
        other.wsaInitialized_ = false;
#endif
        other.socket_ = 
#ifdef _WIN32
            INVALID_SOCKET;
#else
            -1;
#endif
        other.connected_ = false;
        other.state_ = ConnectionState::DISCONNECTED;
    }
    return *this;
}

TCPTransport::TCPTransport(const PoolConfig& config) : TCPTransport() {
    poolConfig_ = config;
    lastCleanup_ = std::chrono::steady_clock::now();
}

bool TCPTransport::connect(const std::string& endpoint, const ConnectionConfig& config) {
    if (!validateState("connect")) {
        return false;
    }

    config_ = config;
    currentEndpoint_ = endpoint;
    updateState(ConnectionState::CONNECTING);

    auto [host, port] = parseEndpoint(endpoint);
    if (host.empty() || port == 0) {
        setError(TransportError::INVALID_PARAMETER, "Invalid endpoint format");
        return false;
    }

    struct addrinfo hints = {}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    int status = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (status != 0) {
        setError(TransportError::RESOLUTION_FAILED, "Failed to resolve hostname");
        return false;
    }

    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resultPtr(result, freeaddrinfo);

    socket_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
#ifdef _WIN32
    if (socket_ == INVALID_SOCKET)
#else
    if (socket_ == -1)
#endif
    {
        setError(TransportError::SOCKET_ERROR, "Failed to create socket");
        return false;
    }

    if (!setSocketOptions(config.connectionTimeoutMs)) {
        closeSocket();
        return false;
    }

    if (localPort_ > 0 && !bindSocket()) {
        closeSocket();
        return false;
    }

    status = ::connect(socket_, result->ai_addr, static_cast<int>(result->ai_addrlen));
#ifdef _WIN32
    if (status == SOCKET_ERROR)
#else
    if (status == -1)
#endif
    {
        setError(TransportError::CONNECTION_FAILED, "Failed to connect to endpoint");
        closeSocket();
        return false;
    }

    connected_ = true;
    updateState(ConnectionState::CONNECTED);

    if (config.healthMonitoring) {
        startHealthMonitoring();
    }

    return true;
}

bool TCPTransport::disconnect() {
    if (!validateState("disconnect")) {
        return false;
    }

    stopHealthMonitoring();

    if (connected_) {
        updateState(ConnectionState::DISCONNECTING);
        gracefulShutdown();
        closeSocket();
        connected_ = false;
        updateState(ConnectionState::DISCONNECTED);
    }

    return true;
}

bool TCPTransport::isConnected() const {
    return connected_;
}

ssize_t TCPTransport::send(const uint8_t* data, size_t size) {
    if (!validateState("send")) {
        return -1;
    }

    if (!data || size == 0) {
        setError(TransportError::INVALID_PARAMETER, "Invalid send parameters");
        return -1;
    }

    size_t totalSent = 0;
    while (totalSent < size) {
        ssize_t sent = ::send(socket_, 
            reinterpret_cast<const char*>(data + totalSent),
            static_cast<int>(size - totalSent),
#ifdef _WIN32
            0
#else
            MSG_NOSIGNAL
#endif
        );

#ifdef _WIN32
        if (sent == SOCKET_ERROR)
#else
        if (sent == -1)
#endif
        {
            TransportError error = mapSystemError();
            if (error == TransportError::WOULD_BLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            setError(error, "Send operation failed");
            return -1;
        }
        totalSent += sent;
    }

    return static_cast<ssize_t>(totalSent);
}

ssize_t TCPTransport::receive(uint8_t* buffer, size_t size) {
    if (!validateState("receive")) {
        return -1;
    }

    if (!buffer || size == 0) {
        setError(TransportError::INVALID_PARAMETER, "Invalid receive parameters");
        return -1;
    }

    ssize_t received = ::recv(socket_,
        reinterpret_cast<char*>(buffer),
        static_cast<int>(size),
        0);

#ifdef _WIN32
    if (received == SOCKET_ERROR)
#else
    if (received == -1)
#endif
    {
        TransportError error = mapSystemError();
        if (error == TransportError::WOULD_BLOCK) {
            return 0;
        }
        setError(error, "Receive operation failed");
        return -1;
    }

    if (received == 0) {
        setError(TransportError::CONNECTION_CLOSED, "Connection closed by peer");
        connected_ = false;
        updateState(ConnectionState::DISCONNECTED);
        return -1;
    }

    return received;
}

std::string TCPTransport::getLastError() const {
    return lastError_;
}

bool TCPTransport::setLocalPort(uint16_t port) {
    if (connected_) {
        setError(TransportError::INVALID_STATE, "Cannot set local port while connected");
        return false;
    }
    localPort_ = port;
    return true;
}

ConnectionState TCPTransport::getState() const {
    return state_.load();
}

TransportError TCPTransport::getLastErrorCode() const {
    return lastErrorCode_;
}

std::string TCPTransport::getErrorDetails() const {
    return lastErrorDetails_;
}

bool TCPTransport::reconnect(uint32_t maxAttempts, uint32_t delayMs) {
    if (currentEndpoint_.empty()) {
        setError(TransportError::INVALID_STATE, "No previous endpoint to reconnect to");
        return false;
    }

    disconnect();

    uint32_t attempt = 0;
    uint32_t currentDelay = delayMs;
    bool success = false;

    while (attempt < maxAttempts && !success) {
        updateState(ConnectionState::RECONNECTING);
        
        if (attempt > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(currentDelay));
            // Exponential backoff with jitter
            currentDelay = std::min<uint32_t>(currentDelay * 2, config_.reconnectDelayMs * 10);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(-100, 100);
            currentDelay += dis(gen);
        }

        success = connect(currentEndpoint_, config_);
        if (!success) {
            attempt++;
            std::stringstream ss;
            ss << "Reconnection attempt " << attempt << " of " << maxAttempts << " failed";
            setError(TransportError::RECONNECTION_FAILED, ss.str());
        }
    }

    if (!success) {
        updateState(ConnectionState::DISCONNECTED);
    }

    return success;
}

void TCPTransport::setStateCallback(std::function<void(ConnectionState)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stateCallback_ = std::move(callback);
}

void TCPTransport::setErrorCallback(std::function<void(TransportError, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    errorCallback_ = std::move(callback);
}

bool TCPTransport::checkHealth() {
    return performHealthCheck();
}

// Private methods

std::pair<std::string, uint16_t> TCPTransport::parseEndpoint(const std::string& endpoint) {
    size_t colonPos = endpoint.find(':');
    if (colonPos == std::string::npos || colonPos == 0 || colonPos == endpoint.length() - 1) {
        return {"", 0};
    }

    std::string host = endpoint.substr(0, colonPos);
    std::string portStr = endpoint.substr(colonPos + 1);
    
    try {
        uint16_t port = static_cast<uint16_t>(std::stoi(portStr));
        return {host, port};
    } catch (...) {
        return {"", 0};
    }
}

bool TCPTransport::validateState(const std::string& operation) const {
    if (operation == "connect") {
        if (connected_) {
            setError(TransportError::INVALID_STATE, "Already connected");
            return false;
        }
    } else if (operation != "disconnect" && !connected_) {
        setError(TransportError::NOT_CONNECTED, "Not connected");
        return false;
    }
    return true;
}

bool TCPTransport::setSocketOptions(uint32_t socketTimeoutMs) {
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(socketTimeoutMs);
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR ||
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
#else
    struct timeval timeout;
    timeout.tv_sec = socketTimeoutMs / 1000;
    timeout.tv_usec = (socketTimeoutMs % 1000) * 1000;
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
#endif
        setError(TransportError::SOCKET_ERROR, "Failed to set socket timeout");
        return false;
    }

    // Enable TCP keepalive
    int keepalive = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE,
        reinterpret_cast<const char*>(&keepalive), sizeof(keepalive)) != 0) {
        setError(TransportError::SOCKET_ERROR, "Failed to enable keepalive");
        return false;
    }

#ifdef _WIN32
    tcp_keepalive keepalive_settings;
    keepalive_settings.onoff = 1;
    keepalive_settings.keepalivetime = 30000; // 30 seconds
    keepalive_settings.keepaliveinterval = 1000; // 1 second

    DWORD bytes_returned;
    if (WSAIoctl(socket_, SIO_KEEPALIVE_VALS, &keepalive_settings,
        sizeof(keepalive_settings), nullptr, 0, &bytes_returned, nullptr, nullptr) != 0) {
        setError(TransportError::SOCKET_ERROR, "Failed to set keepalive parameters");
        return false;
    }
#else
    int keepcnt = 3;
    int keepidle = 30;
    int keepintvl = 1;

    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPCNT,
        &keepcnt, sizeof(int)) != 0) {
        setError(TransportError::SOCKET_ERROR, "Failed to set keepalive count");
        return false;
    }

    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPALIVE_TIME,
        &keepidle, sizeof(int)) != 0) {
        setError(TransportError::SOCKET_ERROR, "Failed to set keepalive idle time");
        return false;
    }

    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPINTVL,
        &keepintvl, sizeof(int)) != 0) {
        setError(TransportError::SOCKET_ERROR, "Failed to set keepalive interval");
        return false;
    }
#endif

    // Set TCP_NODELAY (disable Nagle's algorithm)
    int nodelay = 1;
    if (setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&nodelay), sizeof(nodelay)) != 0) {
        setError(TransportError::SOCKET_ERROR, "Failed to set TCP_NODELAY");
        return false;
    }

    return true;
}

bool TCPTransport::bindSocket() {
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(localPort_);

    if (bind(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        setError(TransportError::BIND_FAILED, "Failed to bind to local port");
        return false;
    }

    return true;
}

bool TCPTransport::setNonBlocking(bool nonBlocking) {
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(socket_, FIONBIO, &mode) != 0) {
        setError(TransportError::SOCKET_ERROR, "Failed to set non-blocking mode");
        return false;
    }
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        setError(TransportError::SOCKET_ERROR, "Failed to get socket flags");
        return false;
    }

    flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(socket_, F_SETFL, flags) == -1) {
        setError(TransportError::SOCKET_ERROR, "Failed to set non-blocking mode");
        return false;
    }
#endif
    return true;
}

void TCPTransport::setError(TransportError code, const std::string& message) {
    lastErrorCode_ = code;
    lastError_ = message;
    lastErrorDetails_ = getSystemError();

    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (errorCallback_) {
        errorCallback_(code, message + ": " + lastErrorDetails_);
    }
}

std::string TCPTransport::getSystemError() const {
#ifdef _WIN32
    DWORD error = WSAGetLastError();
    char* errorMsg = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&errorMsg),
        0,
        nullptr
    );
    std::string result = errorMsg ? errorMsg : "Unknown error";
    LocalFree(errorMsg);
    return result;
#else
    return std::string(strerror(errno));
#endif
}

TransportError TCPTransport::mapSystemError() const {
#ifdef _WIN32
    int error = WSAGetLastError();
    switch (error) {
        case WSAEWOULDBLOCK:
            return TransportError::WOULD_BLOCK;
        case WSAECONNRESET:
        case WSAECONNABORTED:
            return TransportError::CONNECTION_RESET;
        case WSAETIMEDOUT:
            return TransportError::TIMEOUT;
        case WSAEHOSTUNREACH:
            return TransportError::HOST_UNREACHABLE;
        case WSAENETUNREACH:
            return TransportError::NETWORK_UNREACHABLE;
        case WSAECONNREFUSED:
            return TransportError::CONNECTION_REFUSED;
        default:
            return TransportError::UNKNOWN;
    }
#else
    switch (errno) {
        case EWOULDBLOCK:
        case EAGAIN:
            return TransportError::WOULD_BLOCK;
        case ECONNRESET:
            return TransportError::CONNECTION_RESET;
        case ETIMEDOUT:
            return TransportError::TIMEOUT;
        case EHOSTUNREACH:
            return TransportError::HOST_UNREACHABLE;
        case ENETUNREACH:
            return TransportError::NETWORK_UNREACHABLE;
        case ECONNREFUSED:
            return TransportError::CONNECTION_REFUSED;
        default:
            return TransportError::UNKNOWN;
    }
#endif
}

void TCPTransport::updateState(ConnectionState newState) {
    state_ = newState;
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (stateCallback_) {
        stateCallback_(newState);
    }
}

bool TCPTransport::performHealthCheck() {
    if (!connected_) {
        return false;
    }

    // Send a TCP keepalive probe
    char probe = 0;
    ssize_t result = ::send(socket_, &probe, 0,
#ifdef _WIN32
        0
#else
        MSG_NOSIGNAL
#endif
    );

#ifdef _WIN32
    if (result == SOCKET_ERROR)
#else
    if (result == -1)
#endif
    {
        TransportError error = mapSystemError();
        if (error != TransportError::WOULD_BLOCK) {
            setError(error, "Health check failed");
            connected_ = false;
            updateState(ConnectionState::DISCONNECTED);
            return false;
        }
    }

    return true;
}

void TCPTransport::startHealthMonitoring() {
    stopHealthMonitoring();
    
    if (!config_.healthMonitoring) {
        return;
    }

    stopHealthMonitor_ = false;
    healthMonitorThread_ = std::make_unique<std::thread>([this]() {
        while (!stopHealthMonitor_) {
            if (connected_) {
                if (!performHealthCheck()) {
                    if (config_.autoReconnect) {
                        reconnect(config_.maxReconnectAttempts, config_.reconnectDelayMs);
                    }
                }
            }
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.healthCheckIntervalMs));
        }
    });
}

void TCPTransport::stopHealthMonitoring() {
    if (healthMonitorThread_) {
        stopHealthMonitor_ = true;
        if (healthMonitorThread_->joinable()) {
            healthMonitorThread_->join();
        }
        healthMonitorThread_.reset();
    }
}

bool TCPTransport::validateAndRepairConnection(std::shared_ptr<ConnectionInfo> connection) {
    if (!connection) {
        return false;
    }

    if (!validateConnection(connection)) {
        if (config_.autoReconnect) {
            return reconnect(config_.maxReconnectAttempts, config_.reconnectDelayMs);
        }
        return false;
    }

    return true;
}

void TCPTransport::processPriorityQueues() {
    std::lock_guard<std::mutex> lock(asyncMutex_);
    
    for (auto& [priority, queue] : priorityQueues_) {
        while (!queue.empty() && pendingAsyncOperations_ < asyncConfig_.maxPendingOperations) {
            auto task = std::move(queue.front());
            queue.pop();
            pendingAsyncOperations_++;
            
            asyncWorkerPool_.enqueue([this, task = std::move(task)]() {
                task();
                pendingAsyncOperations_--;
            });
        }
    }
}

void TCPTransport::updateResponseStats(const std::string& endpoint, 
                                     std::chrono::milliseconds responseTime) {
    std::lock_guard<std::mutex> lock(asyncMutex_);
    
    auto it = avgResponseTimes_.find(endpoint);
    if (it == avgResponseTimes_.end()) {
        avgResponseTimes_[endpoint] = responseTime;
    } else {
        // Exponential moving average with alpha = 0.2
        it->second = std::chrono::milliseconds(
            static_cast<int64_t>(0.8 * it->second.count() + 0.2 * responseTime.count())
        );
    }
}

void TCPTransport::closeSocket() {
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

bool TCPTransport::gracefulShutdown() {
    if (!connected_) {
        return true;
    }

#ifdef _WIN32
    if (shutdown(socket_, SD_BOTH) == SOCKET_ERROR)
#else
    if (shutdown(socket_, SHUT_RDWR) == -1)
#endif
    {
        setError(TransportError::SHUTDOWN_FAILED, "Failed to shutdown socket");
        return false;
    }

    // Wait for pending data to be sent/received
    char buffer[1024];
    while (true) {
        ssize_t received = ::recv(socket_, buffer, sizeof(buffer), 0);
#ifdef _WIN32
        if (received == SOCKET_ERROR || received == 0)
#else
        if (received <= 0)
#endif
        {
            break;
        }
    }

    return true;
}

} // namespace core
} // namespace xenocomm 
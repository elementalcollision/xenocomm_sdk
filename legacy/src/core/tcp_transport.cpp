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
#include <poll.h>
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
    localPort_(0),
    asyncWorkerPool_(4)
#ifdef _WIN32
    , wsaInitialized_(false)
#endif
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        setError(xenocomm::core::TransportError::INITIALIZATION_FAILED, "Failed to initialize WinSock: " + getSystemError());
        throw std::runtime_error(getLastError());
    }
    wsaInitialized_ = true;
#endif
    
    lastCleanup_ = std::chrono::steady_clock::now();
    lastHealthCheck_ = std::chrono::steady_clock::now();
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
      asyncWorkerPool_(other.asyncWorkerPool_.getWorkerCount()),
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
    other.state_ = xenocomm::core::ConnectionState::DISCONNECTED;
}

TCPTransport& TCPTransport::operator=(TCPTransport&& other) noexcept {
    if (this != &other) {
        disconnect();
        stopHealthMonitoring();
        
        socket_ = other.socket_;
        connected_.store(other.connected_.load());
        localPort_ = other.localPort_;
        lastError_ = std::move(other.lastError_);
        currentEndpoint_ = std::move(other.currentEndpoint_);
        state_.store(other.state_.load());
        lastErrorCode_.store(other.lastErrorCode_.load());
        lastErrorDetails_ = std::move(other.lastErrorDetails_);
        stateCallback_ = std::move(other.stateCallback_);
        errorCallback_ = std::move(other.errorCallback_);
        config_ = std::move(other.config_);
        poolConfig_ = std::move(other.poolConfig_);
        
        // Create a new AsyncWorkerPool with the same number of threads
        // Since we can't assign to asyncWorkerPool_, we need to manually destroy and create a new one
        size_t workerCount = other.asyncWorkerPool_.getWorkerCount();
        asyncWorkerPool_.~AsyncWorkerPool();
        new (&asyncWorkerPool_) AsyncWorkerPool(workerCount);
        
        // Move connection pool
        {
            std::lock_guard<std::mutex> lock(poolMutex_);
            std::lock_guard<std::mutex> otherLock(other.poolMutex_);
            connectionPool_ = std::move(other.connectionPool_);
            while (!availableConnections_.empty()) {
                availableConnections_.pop();
            }
            while (!other.availableConnections_.empty()) {
                availableConnections_.push(other.availableConnections_.front());
                other.availableConnections_.pop();
            }
        }
        
        // Move metrics
        activeConnections_.store(other.activeConnections_.load());
        totalConnectionsCreated_.store(other.totalConnectionsCreated_.load());
        failedConnectionAttempts_.store(other.failedConnectionAttempts_.load());
        lastCleanup_ = other.lastCleanup_;
        lastHealthCheck_ = other.lastHealthCheck_;
        
        // Reset the moved-from object
        other.socket_ = 
#ifdef _WIN32
            INVALID_SOCKET;
#else
            -1;
#endif
        other.connected_.store(false);
        other.state_.store(xenocomm::core::ConnectionState::DISCONNECTED);
        other.lastErrorCode_.store(xenocomm::core::TransportError::NONE);
        other.activeConnections_.store(0);
        other.totalConnectionsCreated_.store(0);
        other.failedConnectionAttempts_.store(0);
        
        if (poolConfig_.enableHealthMonitoring) {
            startHealthMonitoring();
        }
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
    updateState(xenocomm::core::ConnectionState::CONNECTING);

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
    updateState(xenocomm::core::ConnectionState::CONNECTED);

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
        updateState(xenocomm::core::ConnectionState::DISCONNECTING);
        gracefulShutdown();
        closeSocket();
        connected_ = false;
        updateState(xenocomm::core::ConnectionState::DISCONNECTED);
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
        setError(xenocomm::core::TransportError::INVALID_PARAMETER, "Invalid send parameters");
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
            xenocomm::core::TransportError error = mapSystemError();
            if (error == xenocomm::core::TransportError::WOULD_BLOCK) {
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
        setError(xenocomm::core::TransportError::INVALID_PARAMETER, "Invalid receive parameters");
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
        xenocomm::core::TransportError error = mapSystemError();
        if (error == xenocomm::core::TransportError::WOULD_BLOCK) {
            return 0;
        }
        setError(error, "Receive operation failed");
        return -1;
    }

    if (received == 0) {
        setError(xenocomm::core::TransportError::CONNECTION_CLOSED, "Connection closed by peer");
        connected_ = false;
        updateState(xenocomm::core::ConnectionState::DISCONNECTED);
        return -1;
    }

    return received;
}

std::string TCPTransport::getLastError() const {
    return lastError_;
}

bool TCPTransport::setLocalPort(uint16_t port) {
    if (connected_) {
        setError(xenocomm::core::TransportError::INVALID_STATE, "Cannot set local port while connected");
        return false;
    }
    localPort_ = port;
    return true;
}

xenocomm::core::ConnectionState TCPTransport::getState() const {
    return state_.load();
}

xenocomm::core::TransportError TCPTransport::getLastErrorCode() const {
    return lastErrorCode_;
}

std::string TCPTransport::getErrorDetails() const {
    return lastErrorDetails_;
}

bool TCPTransport::reconnect(uint32_t maxAttempts, uint32_t delayMs) {
    if (currentEndpoint_.empty()) {
        setError(xenocomm::core::TransportError::INVALID_STATE, "No previous endpoint to reconnect to");
        return false;
    }

    disconnect();

    uint32_t attempt = 0;
    uint32_t currentDelay = delayMs;
    bool success = false;

    while (attempt < maxAttempts && !success) {
        updateState(xenocomm::core::ConnectionState::RECONNECTING);
        
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
            setError(xenocomm::core::TransportError::RECONNECTION_FAILED, ss.str());
        }
    }

    if (!success) {
        updateState(xenocomm::core::ConnectionState::DISCONNECTED);
    }

    return success;
}

void TCPTransport::setStateCallback(std::function<void(xenocomm::core::ConnectionState)> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    stateCallback_ = std::move(callback);
}

void TCPTransport::setErrorCallback(std::function<void(xenocomm::core::TransportError, const std::string&)> callback) {
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
            setError(xenocomm::core::TransportError::INVALID_STATE, "Already connected");
            return false;
        }
    } else if (operation != "disconnect" && !connected_) {
        setError(xenocomm::core::TransportError::NOT_CONNECTED, "Not connected");
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
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set socket timeout");
        return false;
    }

    // Enable TCP keepalive
    int keepalive = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE,
        reinterpret_cast<const char*>(&keepalive), sizeof(keepalive)) != 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to enable keepalive");
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
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set keepalive parameters");
        return false;
    }
#else
    int keepcnt = 3;
    int keepidle = 30;
    int keepintvl = 1;

    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPCNT,
        &keepcnt, sizeof(int)) != 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set keepalive count");
        return false;
    }

    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPALIVE_TIME,
        &keepidle, sizeof(int)) != 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set keepalive idle time");
        return false;
    }

    if (setsockopt(socket_, IPPROTO_TCP, TCP_KEEPINTVL,
        &keepintvl, sizeof(int)) != 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set keepalive interval");
        return false;
    }
#endif

    // Set TCP_NODELAY (disable Nagle's algorithm)
    int nodelay = 1;
    if (setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&nodelay), sizeof(nodelay)) != 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set TCP_NODELAY");
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
        setError(xenocomm::core::TransportError::BIND_FAILED, "Failed to bind to local port");
        return false;
    }

    return true;
}

bool TCPTransport::setNonBlocking(bool nonBlocking) {
#ifdef _WIN32
    u_long mode = nonBlocking ? 1 : 0;
    if (ioctlsocket(socket_, FIONBIO, &mode) != 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set non-blocking mode");
        return false;
    }
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to get socket flags");
        return false;
    }

    flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (fcntl(socket_, F_SETFL, flags) == -1) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set non-blocking mode");
        return false;
    }
#endif
    return true;
}

void TCPTransport::setError(xenocomm::core::TransportError code, const std::string& message) const {
    lastErrorCode_ = code;
    lastError_ = message;
    lastErrorDetails_ = getSystemError();
    
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (errorCallback_) {
            errorCallback_(code, message + ": " + lastErrorDetails_);
        }
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

xenocomm::core::TransportError TCPTransport::mapSystemError() const {
#ifdef _WIN32
    DWORD err = WSAGetLastError();
    switch (err) {
        case WSAETIMEDOUT:
            return xenocomm::core::TransportError::TIMEOUT;
        case WSAECONNREFUSED:
            return xenocomm::core::TransportError::CONNECTION_REFUSED;
        case WSAEHOSTUNREACH:
        case WSAENETUNREACH:
            return xenocomm::core::TransportError::HOST_UNREACHABLE;
        case WSAECONNRESET:
        case WSAECONNABORTED:
            return xenocomm::core::TransportError::CONNECTION_RESET;
        case WSAEFAULT:
        case WSAEINVAL:
            return xenocomm::core::TransportError::INVALID_PARAMETER;
        case WSAENOBUFS:
        case WSAEMSGSIZE:
            return xenocomm::core::TransportError::BUFFER_OVERFLOW;
        case WSAEWOULDBLOCK:
            return xenocomm::core::TransportError::WOULD_BLOCK;
        default:
            return xenocomm::core::TransportError::UNKNOWN;
    }
#else
    switch (errno) {
        case ETIMEDOUT:
            return xenocomm::core::TransportError::TIMEOUT;
        case ECONNREFUSED:
            return xenocomm::core::TransportError::CONNECTION_REFUSED;
        case EHOSTUNREACH:
        case ENETUNREACH:
            return xenocomm::core::TransportError::HOST_UNREACHABLE;
        case ECONNRESET:
            return xenocomm::core::TransportError::CONNECTION_RESET;
        case EINVAL:
            return xenocomm::core::TransportError::INVALID_PARAMETER;
        case ENOBUFS:
        case EMSGSIZE:
            return xenocomm::core::TransportError::BUFFER_OVERFLOW;
#if EAGAIN == EWOULDBLOCK
        case EAGAIN: // EWOULDBLOCK is the same as EAGAIN on most systems
#else
        case EAGAIN:
        case EWOULDBLOCK:
#endif
            return xenocomm::core::TransportError::WOULD_BLOCK;
        default:
            return xenocomm::core::TransportError::UNKNOWN;
    }
#endif
}

void TCPTransport::updateState(xenocomm::core::ConnectionState newState) {
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
        xenocomm::core::TransportError error = mapSystemError();
        if (error != xenocomm::core::TransportError::WOULD_BLOCK) {
            setError(error, "Health check failed");
            connected_ = false;
            updateState(xenocomm::core::ConnectionState::DISCONNECTED);
            return false;
        }
    }

    return true;
}

void TCPTransport::startHealthMonitoring() {
    stopHealthMonitoring();
    stopHealthMonitor_.store(false);
    
    healthMonitorThread_ = std::make_unique<std::thread>([this]() {
        while (!stopHealthMonitor_.load()) {
            if (connected_.load()) {
                // Perform health check
                if (!performHealthCheck()) {
                    // Connection is unhealthy
                    setError(xenocomm::core::TransportError::CONNECTION_RESET, "Connection lost detected by health monitor");
                    updateState(xenocomm::core::ConnectionState::ERROR);
                    connected_.store(false);
                }
            }
            
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime - lastCleanup_).count();
            
            // Clean up idle connections periodically
            if (elapsed > poolConfig_.idleTimeout) {
                cleanupIdleConnections();
                lastCleanup_ = currentTime;
            }
            
            // Sleep to avoid high CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(poolConfig_.healthCheckInterval));
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
        setError(xenocomm::core::TransportError::SHUTDOWN_FAILED, "Failed to shutdown socket");
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

void TCPTransport::cleanupIdleConnections() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    // For each endpoint in the connection pool
    for (auto& [endpoint, connections] : connectionPool_) {
        auto it = connections.begin();
        while (it != connections.end()) {
            auto& connection = *it;
            
            // Skip connections that are in use
            if (connection->inUse) {
                ++it;
                continue;
            }
            
            // Check if the connection is idle for too long
            auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - connection->lastUsed).count();
                
            if (idleTime > poolConfig_.idleTimeout) {
                // Close idle connection
                if (connection->socket != 
#ifdef _WIN32
                    INVALID_SOCKET
#else
                    -1
#endif
                ) {
#ifdef _WIN32
                    closesocket(connection->socket);
                    connection->socket = INVALID_SOCKET;
#else
                    close(connection->socket);
                    connection->socket = -1;
#endif
                }
                
                // Remove from the pool
                it = connections.erase(it);
                activeConnections_.fetch_sub(1);
            } else {
                ++it;
            }
        }
    }
}

bool TCPTransport::getPeerAddress(std::string& address, uint16_t& port) {
    if (!isConnected()) {
        setError(xenocomm::core::TransportError::NOT_CONNECTED, "Not connected");
        return false;
    }
    
    struct sockaddr_in peerAddr;
    socklen_t addrLen = sizeof(peerAddr);
    
#ifdef _WIN32
    if (getpeername(socket_, (struct sockaddr*)&peerAddr, &addrLen) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to get peer name: " + getSystemError());
        return false;
    }
#else
    if (getpeername(socket_, (struct sockaddr*)&peerAddr, &addrLen) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to get peer name: " + getSystemError());
        return false;
    }
#endif
    
    char addrStr[INET_ADDRSTRLEN];
#ifdef _WIN32
    if (inet_ntop(AF_INET, &(peerAddr.sin_addr), addrStr, INET_ADDRSTRLEN) == NULL) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to convert address: " + getSystemError());
        return false;
    }
#else
    if (inet_ntop(AF_INET, &(peerAddr.sin_addr), addrStr, INET_ADDRSTRLEN) == NULL) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to convert address: " + getSystemError());
        return false;
    }
#endif
    
    address = addrStr;
    port = ntohs(peerAddr.sin_port);
    
    return true;
}

int TCPTransport::getSocketFd() const {
#ifdef _WIN32
    // On Windows, we can't directly return the socket since it's not a file descriptor
    return -1;
#else
    return socket_;
#endif
}

bool TCPTransport::setReceiveTimeout(const std::chrono::milliseconds& timeout) {
    if (!isConnected()) {
        setError(xenocomm::core::TransportError::NOT_CONNECTED, "Not connected");
        return false;
    }
    
#ifdef _WIN32
    DWORD timeoutMs = static_cast<DWORD>(timeout.count());
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeoutMs, sizeof(timeoutMs)) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set receive timeout: " + getSystemError());
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set receive timeout: " + getSystemError());
        return false;
    }
#endif
    
    return true;
}

bool TCPTransport::setSendTimeout(const std::chrono::milliseconds& timeout) {
    if (!isConnected()) {
        setError(xenocomm::core::TransportError::NOT_CONNECTED, "Not connected");
        return false;
    }
    
#ifdef _WIN32
    DWORD timeoutMs = static_cast<DWORD>(timeout.count());
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeoutMs, sizeof(timeoutMs)) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set send timeout: " + getSystemError());
        return false;
    }
#else
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set send timeout: " + getSystemError());
        return false;
    }
#endif
    
    return true;
}

bool TCPTransport::setKeepAlive(bool enable) {
    if (!isConnected()) {
        setError(xenocomm::core::TransportError::NOT_CONNECTED, "Not connected");
        return false;
    }
    
    int flag = enable ? 1 : 0;
#ifdef _WIN32
    if (setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE, (char*)&flag, sizeof(flag)) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set keep alive: " + getSystemError());
        return false;
    }
#else
    if (setsockopt(socket_, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag)) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set keep alive: " + getSystemError());
        return false;
    }
#endif
    
    return true;
}

bool TCPTransport::setTcpNoDelay(bool enable) {
    if (!isConnected()) {
        setError(xenocomm::core::TransportError::NOT_CONNECTED, "Not connected");
        return false;
    }
    
    int flag = enable ? 1 : 0;
#ifdef _WIN32
    if (setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag)) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set TCP_NODELAY: " + getSystemError());
        return false;
    }
#else
    if (setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set TCP_NODELAY: " + getSystemError());
        return false;
    }
#endif
    
    return true;
}

bool TCPTransport::setReuseAddress(bool enable) {
    if (isConnected()) {
        setError(xenocomm::core::TransportError::ALREADY_CONNECTED, "Socket already connected");
        return false;
    }
    
    int flag = enable ? 1 : 0;
#ifdef _WIN32
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag)) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set SO_REUSEADDR: " + getSystemError());
        return false;
    }
#else
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set SO_REUSEADDR: " + getSystemError());
        return false;
    }
#endif
    
    return true;
}

bool TCPTransport::setReceiveBufferSize(size_t size) {
    int bufSize = static_cast<int>(size);
#ifdef _WIN32
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, sizeof(bufSize)) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set receive buffer size: " + getSystemError());
        return false;
    }
#else
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize)) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set receive buffer size: " + getSystemError());
        return false;
    }
#endif
    
    return true;
}

bool TCPTransport::setSendBufferSize(size_t size) {
    int bufSize = static_cast<int>(size);
#ifdef _WIN32
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize)) == SOCKET_ERROR) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set send buffer size: " + getSystemError());
        return false;
    }
#else
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize)) < 0) {
        setError(xenocomm::core::TransportError::SOCKET_ERROR, "Failed to set send buffer size: " + getSystemError());
        return false;
    }
#endif
    
    return true;
}

bool TCPTransport::validateConnection(std::shared_ptr<ConnectionInfo> const& connection) {
    if (!connection) {
        return false;
    }
    
    // Check if the socket is valid
    if (connection->socket == 
#ifdef _WIN32
        INVALID_SOCKET
#else
        -1
#endif
    ) {
        return false;
    }
    
    // Check if the connection is still alive
    char probe = 0;
    ssize_t result = ::send(connection->socket, &probe, 0,
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
        // Check if the error is just because the socket would block
        int error = 
#ifdef _WIN32
            WSAGetLastError();
        if (error != WSAEWOULDBLOCK)
#else
            errno;
        if (error != EAGAIN && error != EWOULDBLOCK)
#endif
        {
            return false;
        }
    }
    
    return true;
}

} // namespace core
} // namespace xenocomm 
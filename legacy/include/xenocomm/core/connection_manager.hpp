#ifndef XENOCOMM_CORE_CONNECTION_MANAGER_HPP
#define XENOCOMM_CORE_CONNECTION_MANAGER_HPP

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "xenocomm/core/transport_protocol.hpp"

namespace xenocomm {
namespace core {

/**
 * @brief Status of a network connection
 */
enum class ConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Error
};

/**
 * @brief Represents a network connection with its associated metadata
 */
class Connection {
public:
    using ConnectionId = std::string;
    
    Connection(ConnectionId id, const ConnectionConfig& config = ConnectionConfig{})
        : id_(std::move(id)), config_(config) {}
    
    const ConnectionId& getId() const { return id_; }
    ConnectionStatus getStatus() const { return status_; }
    const ConnectionConfig& getConfig() const { return config_; }
    
protected:
    ConnectionId id_;
    ConnectionStatus status_{ConnectionStatus::Disconnected};
    ConnectionConfig config_;
};

/**
 * @brief Core connection management functionality for establishing and managing network connections
 */
class ConnectionManager {
public:
    using ConnectionPtr = std::shared_ptr<Connection>;
    using ConnectionMap = std::unordered_map<Connection::ConnectionId, ConnectionPtr>;
    
    ConnectionManager();
    virtual ~ConnectionManager() = default;
    
    /**
     * @brief Establish a new connection with the given ID and configuration
     * @param connectionId Unique identifier for the connection
     * @param config Configuration options for the connection
     * @return Shared pointer to the established connection
     * @throws ConnectionError if connection fails
     */
    virtual ConnectionPtr establish(const Connection::ConnectionId& connectionId,
                                 const ConnectionConfig& config = ConnectionConfig{});
    
    /**
     * @brief Close an existing connection
     * @param connectionId ID of the connection to close
     * @return true if connection was closed successfully, false if not found
     */
    virtual bool close(const Connection::ConnectionId& connectionId);
    
    /**
     * @brief Check the status of a connection
     * @param connectionId ID of the connection to check
     * @return Current status of the connection
     * @throws ConnectionError if connection not found
     */
    virtual ConnectionStatus checkStatus(const Connection::ConnectionId& connectionId) const;
    
    /**
     * @brief Get an existing connection by ID
     * @param connectionId ID of the connection to retrieve
     * @return Shared pointer to the connection if found
     * @throws ConnectionError if connection not found
     */
    virtual ConnectionPtr getConnection(const Connection::ConnectionId& connectionId) const;
    
    /**
     * @brief Get all active connections
     * @return Vector of connection pointers
     */
    virtual std::vector<ConnectionPtr> getActiveConnections() const;
    
protected:
    ConnectionMap connections_;
};

} // namespace core
} // namespace xenocomm

#endif // XENOCOMM_CORE_CONNECTION_MANAGER_HPP 
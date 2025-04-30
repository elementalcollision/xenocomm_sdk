#include "xenocomm/core/connection_manager.hpp"
#include <stdexcept>

namespace xenocomm {
namespace core {

class ConnectionError : public std::runtime_error {
public:
    explicit ConnectionError(const std::string& message) : std::runtime_error(message) {}
};

ConnectionManager::ConnectionManager() = default;

ConnectionManager::ConnectionPtr ConnectionManager::establish(
    const Connection::ConnectionId& connectionId,
    const ConnectionConfig& config) {
    // Check if connection already exists
    auto it = connections_.find(connectionId);
    if (it != connections_.end()) {
        throw ConnectionError("Connection with ID " + connectionId + " already exists");
    }
    
    // Create new connection
    auto connection = std::make_shared<Connection>(connectionId, config);
    connections_[connectionId] = connection;
    
    return connection;
}

bool ConnectionManager::close(const Connection::ConnectionId& connectionId) {
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) {
        return false;
    }
    
    connections_.erase(it);
    return true;
}

ConnectionStatus ConnectionManager::checkStatus(const Connection::ConnectionId& connectionId) const {
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) {
        throw ConnectionError("Connection with ID " + connectionId + " not found");
    }
    
    return it->second->getStatus();
}

ConnectionManager::ConnectionPtr ConnectionManager::getConnection(
    const Connection::ConnectionId& connectionId) const {
    auto it = connections_.find(connectionId);
    if (it == connections_.end()) {
        throw ConnectionError("Connection with ID " + connectionId + " not found");
    }
    
    return it->second;
}

std::vector<ConnectionManager::ConnectionPtr> ConnectionManager::getActiveConnections() const {
    std::vector<ConnectionPtr> activeConnections;
    activeConnections.reserve(connections_.size());
    
    for (const auto& [_, connection] : connections_) {
        if (connection->getStatus() == ConnectionStatus::Connected) {
            activeConnections.push_back(connection);
        }
    }
    
    return activeConnections;
}

// Placeholder implementation
// TODO: Implement actual connection management functionality

} // namespace core
} // namespace xenocomm 
#include <gtest/gtest.h>
#include "xenocomm/core/connection_manager.hpp"

using namespace xenocomm::core;

class ConnectionManagerTest : public ::testing::Test {
protected:
    ConnectionManager manager;
    const std::string testConnectionId = "test_connection";
    ConnectionConfig defaultConfig;
};

TEST_F(ConnectionManagerTest, EstablishConnection) {
    auto connection = manager.establish(testConnectionId, defaultConfig);
    ASSERT_NE(connection, nullptr);
    EXPECT_EQ(connection->getId(), testConnectionId);
    EXPECT_EQ(connection->getStatus(), ConnectionStatus::Disconnected);
}

TEST_F(ConnectionManagerTest, EstablishDuplicateConnection) {
    manager.establish(testConnectionId, defaultConfig);
    EXPECT_THROW(manager.establish(testConnectionId, defaultConfig), std::runtime_error);
}

TEST_F(ConnectionManagerTest, CloseConnection) {
    manager.establish(testConnectionId, defaultConfig);
    EXPECT_TRUE(manager.close(testConnectionId));
    EXPECT_FALSE(manager.close(testConnectionId)); // Already closed
}

TEST_F(ConnectionManagerTest, CheckStatus) {
    auto connection = manager.establish(testConnectionId, defaultConfig);
    EXPECT_EQ(manager.checkStatus(testConnectionId), ConnectionStatus::Disconnected);
    
    EXPECT_THROW(manager.checkStatus("nonexistent"), std::runtime_error);
}

TEST_F(ConnectionManagerTest, GetConnection) {
    auto connection1 = manager.establish(testConnectionId, defaultConfig);
    auto connection2 = manager.getConnection(testConnectionId);
    
    EXPECT_EQ(connection1, connection2);
    EXPECT_THROW(manager.getConnection("nonexistent"), std::runtime_error);
}

TEST_F(ConnectionManagerTest, GetActiveConnections) {
    // Initially no active connections
    EXPECT_TRUE(manager.getActiveConnections().empty());
    
    // Add a connection (initially disconnected)
    manager.establish(testConnectionId, defaultConfig);
    EXPECT_TRUE(manager.getActiveConnections().empty());
    
    // TODO: Add tests for active connections once connection state management is implemented
} 
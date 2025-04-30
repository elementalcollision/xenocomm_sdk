#include <gtest/gtest.h>
#include "xenocomm/core/udp_transport.hpp"
#include <thread>
#include <future>
#include <chrono>

namespace xenocomm {
namespace core {
namespace test {

class UDPTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport_ = std::make_unique<UDPTransport>();
        config_.timeout = std::chrono::milliseconds(100);
        ASSERT_TRUE(transport_->initialize());
    }

    void TearDown() override {
        if (transport_->isConnected()) {
            transport_->disconnect();
        }
        transport_.reset();
        transport_->cleanup();
    }

    std::unique_ptr<UDPTransport> transport_;
    ConnectionConfig config_;
    const std::string testEndpoint_ = "127.0.0.1:12345";
};

TEST_F(UDPTransportTest, InvalidEndpoint) {
    EXPECT_FALSE(transport_->connect("invalid_endpoint", config_));
    EXPECT_FALSE(transport_->connect("localhost:", config_));
    EXPECT_FALSE(transport_->connect(":8080", config_));
    EXPECT_FALSE(transport_->connect("localhost:invalid", config_));
}

TEST_F(UDPTransportTest, SetLocalPort) {
    EXPECT_TRUE(transport_->setLocalPort(54321));
    EXPECT_TRUE(transport_->connect(testEndpoint_, config_));
    EXPECT_FALSE(transport_->setLocalPort(12345)); // Should fail when connected
}

TEST_F(UDPTransportTest, ConnectDisconnect) {
    EXPECT_TRUE(transport_->connect(testEndpoint_, config_));
    EXPECT_TRUE(transport_->isConnected());
    EXPECT_TRUE(transport_->disconnect());
    EXPECT_FALSE(transport_->isConnected());
}

TEST_F(UDPTransportTest, DoubleConnect) {
    EXPECT_TRUE(transport_->connect(testEndpoint_, config_));
    EXPECT_FALSE(transport_->connect(testEndpoint_, config_));
}

TEST_F(UDPTransportTest, SendReceiveTimeout) {
    EXPECT_TRUE(transport_->connect(testEndpoint_, config_));
    
    // Send should succeed even without a receiver
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    EXPECT_GE(transport_->send(data), 0);
    
    // Receive should timeout
    auto received = transport_->receive(1024);
    EXPECT_TRUE(received.empty());
    EXPECT_EQ(transport_->getLastError(), "Receive timeout");
}

TEST_F(UDPTransportTest, SendReceiveEchoServer) {
    // Start echo server in a separate thread
    auto serverFuture = std::async(std::launch::async, [this]() {
        UDPTransport server;
        server.setLocalPort(12345);
        EXPECT_TRUE(server.connect("0.0.0.0:0", config_));
        
        auto received = server.receive(1024);
        if (!received.empty()) {
            server.send(received);
        }
        return received;
    });
    
    // Give the server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Send data from client
    EXPECT_TRUE(transport_->connect(testEndpoint_, config_));
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    EXPECT_GE(transport_->send(data), 0);
    
    // Receive echo response
    auto received = transport_->receive(1024);
    EXPECT_FALSE(received.empty());
    EXPECT_EQ(received, data);
    
    // Verify server received correct data
    auto serverReceived = serverFuture.get();
    EXPECT_EQ(serverReceived, data);
}

TEST_F(UDPTransportTest, GetProtocolType) {
    EXPECT_EQ(transport_->getProtocolType(), "UDP");
}

TEST_F(UDPTransportTest, SendWithoutConnect) {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    EXPECT_LT(transport_->send(data), 0);
    EXPECT_EQ(transport_->getLastError(), "Not connected");
}

TEST_F(UDPTransportTest, ReceiveWithoutConnect) {
    auto received = transport_->receive(1024);
    EXPECT_TRUE(received.empty());
    EXPECT_EQ(transport_->getLastError(), "Not connected");
}

TEST_F(UDPTransportTest, JoinMulticastGroup_InvalidAddress) {
    // Test with invalid addresses
    EXPECT_FALSE(transport_->joinMulticastGroup("192.168.1.1")); // Regular IP
    EXPECT_FALSE(transport_->joinMulticastGroup("256.256.256.256")); // Invalid IP
    EXPECT_FALSE(transport_->joinMulticastGroup("invalid")); // Not an IP
}

TEST_F(UDPTransportTest, JoinMulticastGroup_ValidAddress) {
    // Test with valid multicast addresses
    EXPECT_TRUE(transport_->joinMulticastGroup("224.0.0.1")); // All hosts group
    EXPECT_TRUE(transport_->leaveMulticastGroup("224.0.0.1"));
    
    EXPECT_TRUE(transport_->joinMulticastGroup("239.255.255.255")); // Local scope
    EXPECT_TRUE(transport_->leaveMulticastGroup("239.255.255.255"));
}

TEST_F(UDPTransportTest, MulticastTTL) {
    // Test invalid TTL values
    EXPECT_FALSE(transport_->setMulticastTTL(0));
    EXPECT_FALSE(transport_->setMulticastTTL(256));
    EXPECT_FALSE(transport_->setMulticastTTL(-1));
    
    // Test valid TTL values
    EXPECT_TRUE(transport_->setMulticastTTL(1));
    EXPECT_TRUE(transport_->setMulticastTTL(32));
    EXPECT_TRUE(transport_->setMulticastTTL(255));
}

TEST_F(UDPTransportTest, MulticastLoopback) {
    EXPECT_TRUE(transport_->setMulticastLoopback(true));
    EXPECT_TRUE(transport_->setMulticastLoopback(false));
}

TEST_F(UDPTransportTest, MulticastCommunication) {
    const std::string TEST_GROUP = "224.0.0.250";
    const int TEST_PORT = 12345;
    const std::string TEST_MESSAGE = "Hello Multicast";
    
    // Create sender and receiver transports
    UDPTransport sender, receiver;
    ASSERT_TRUE(sender.initialize());
    ASSERT_TRUE(receiver.initialize());
    
    // Configure receiver
    ASSERT_TRUE(receiver.bind(TEST_PORT));
    ASSERT_TRUE(receiver.joinMulticastGroup(TEST_GROUP));
    ASSERT_TRUE(receiver.setMulticastLoopback(true));
    
    // Configure sender
    ASSERT_TRUE(sender.setMulticastTTL(1));
    
    // Start a thread to receive data
    std::string received_data;
    bool data_received = false;
    std::thread receive_thread([&]() {
        char buffer[1024];
        size_t received = 0;
        std::string sender_addr;
        uint16_t sender_port;
        
        if (receiver.receiveFrom(buffer, sizeof(buffer), received, sender_addr, sender_port)) {
            received_data = std::string(buffer, received);
            data_received = true;
        }
    });
    
    // Wait a bit for the receiver to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Send the test message
    EXPECT_TRUE(sender.sendTo(TEST_MESSAGE.c_str(), TEST_MESSAGE.length(), 
                             TEST_GROUP, TEST_PORT));
    
    // Wait for the receive thread
    receive_thread.join();
    
    // Verify the received data
    EXPECT_TRUE(data_received);
    EXPECT_EQ(received_data, TEST_MESSAGE);
    
    // Cleanup
    EXPECT_TRUE(receiver.leaveMulticastGroup(TEST_GROUP));
    sender.cleanup();
    receiver.cleanup();
}

} // namespace test
} // namespace core
} // namespace xenocomm

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 
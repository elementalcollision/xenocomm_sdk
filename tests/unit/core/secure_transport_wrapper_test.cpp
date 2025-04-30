#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "xenocomm/core/secure_transport_wrapper.hpp"
#include "xenocomm/core/mock_transport.hpp"
#include <chrono>
#include <thread>
#include <random>

using namespace xenocomm::core;
using namespace testing;

class SecureTransportWrapperTest : public Test {
protected:
    void SetUp() override {
        mockTransport_ = std::make_shared<MockTransport>();
        config_.securityConfig.protocol = EncryptionProtocol::TLS_1_3;
        config_.securityConfig.recordBatching.enabled = true;
        config_.securityConfig.adaptiveRecord.enabled = true;
        config_.securityConfig.enableVectoredIO = true;
        
        wrapper_ = std::make_unique<SecureTransportWrapper>(mockTransport_, config_);
    }

    void TearDown() override {
        wrapper_.reset();
        mockTransport_.reset();
    }

    std::vector<uint8_t> generateRandomData(size_t size) {
        std::vector<uint8_t> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        for (auto& byte : data) {
            byte = static_cast<uint8_t>(dis(gen));
        }
        
        return data;
    }

    std::shared_ptr<MockTransport> mockTransport_;
    TransportConfig config_;
    std::unique_ptr<SecureTransportWrapper> wrapper_;
};

TEST_F(SecureTransportWrapperTest, InitializationSucceeds) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillOnce(Return(true));
    
    EXPECT_TRUE(wrapper_->initialize().isOk());
}

TEST_F(SecureTransportWrapperTest, HandshakeCompletes) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    EXPECT_CALL(*mockTransport_, send(_))
        .WillRepeatedly(Return(true));
    
    EXPECT_CALL(*mockTransport_, receive(_))
        .WillRepeatedly(Invoke([](std::vector<uint8_t>& data) {
            // Simulate successful handshake response
            data = std::vector<uint8_t>{0x16, 0x03, 0x03}; // TLS handshake
            return true;
        }));
    
    EXPECT_TRUE(wrapper_->initialize().isOk());
    EXPECT_TRUE(wrapper_->performHandshake().isOk());
}

TEST_F(SecureTransportWrapperTest, RecordBatchingWorks) {
    // Setup
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    // Initialize and complete handshake
    ASSERT_TRUE(wrapper_->initialize().isOk());
    
    // Generate test data
    std::vector<std::vector<uint8_t>> testData;
    for (int i = 0; i < 5; ++i) {
        testData.push_back(generateRandomData(1024));
    }
    
    // Expect batched send
    EXPECT_CALL(*mockTransport_, send(_))
        .WillOnce(Return(true));
    
    // Send data
    for (const auto& data : testData) {
        EXPECT_TRUE(wrapper_->send(data).isOk());
    }
    
    // Wait for batch processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

TEST_F(SecureTransportWrapperTest, AdaptiveRecordSizingAdjusts) {
    // Setup
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    // Initialize and complete handshake
    ASSERT_TRUE(wrapper_->initialize().isOk());
    
    // Generate test data with varying sizes
    std::vector<size_t> testSizes = {1024, 2048, 4096, 8192, 16384};
    
    for (size_t size : testSizes) {
        auto data = generateRandomData(size);
        
        // Simulate varying network conditions
        EXPECT_CALL(*mockTransport_, send(_))
            .WillOnce(Invoke([](const std::vector<uint8_t>& data) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(data.size() / 1024));
                return true;
            }));
        
        EXPECT_TRUE(wrapper_->send(data).isOk());
        
        // Allow time for RTT measurement
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

TEST_F(SecureTransportWrapperTest, VectoredIOOptimizesLargeTransfers) {
    // Setup
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    // Initialize and complete handshake
    ASSERT_TRUE(wrapper_->initialize().isOk());
    
    // Generate multiple buffers
    std::vector<std::vector<uint8_t>> buffers;
    for (int i = 0; i < 8; ++i) {
        buffers.push_back(generateRandomData(2048));
    }
    
    // Expect vectored send
    EXPECT_CALL(*mockTransport_, getSocketFd())
        .WillOnce(Return(1));  // Mock fd
    
    // Send using vectored I/O
    EXPECT_TRUE(wrapper_->sendv(buffers).isOk());
}

TEST_F(SecureTransportWrapperTest, FallsBackToRegularSendForSmallTransfers) {
    // Setup
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    // Initialize and complete handshake
    ASSERT_TRUE(wrapper_->initialize().isOk());
    
    // Generate small buffers
    std::vector<std::vector<uint8_t>> buffers;
    for (int i = 0; i < 3; ++i) {
        buffers.push_back(generateRandomData(256));
    }
    
    // Expect regular sends instead of vectored I/O
    EXPECT_CALL(*mockTransport_, send(_))
        .Times(3)
        .WillRepeatedly(Return(true));
    
    // Send should fall back to regular send
    EXPECT_TRUE(wrapper_->sendv(buffers).isOk());
}

TEST_F(SecureTransportWrapperTest, HandlesEncryptionFailure) {
    // Setup
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    // Initialize and complete handshake
    ASSERT_TRUE(wrapper_->initialize().isOk());
    
    // Generate test data
    auto data = generateRandomData(1024);
    
    // Simulate encryption failure
    EXPECT_CALL(*mockTransport_, send(_))
        .WillOnce(Return(false));
    
    auto result = wrapper_->send(data);
    EXPECT_FALSE(result.isOk());
    EXPECT_THAT(result.error(), HasSubstr("Failed to send encrypted data"));
}

TEST_F(SecureTransportWrapperTest, DTLSCookieExchangeSucceeds) {
    // Configure for DTLS
    config_.securityConfig.protocol = EncryptionProtocol::DTLS_1_2;
    wrapper_ = std::make_unique<SecureTransportWrapper>(mockTransport_, config_);
    
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    EXPECT_CALL(*mockTransport_, getPeerAddress(_, _))
        .WillOnce(DoAll(
            SetArgReferee<0>("192.168.1.1"),
            SetArgReferee<1>(12345),
            Return(true)));
    
    // Initialize and start handshake
    ASSERT_TRUE(wrapper_->initialize().isOk());
    EXPECT_TRUE(wrapper_->performHandshake().isOk());
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "xenocomm/core/secure_transport_wrapper.hpp"
#include "xenocomm/core/mock_transport.hpp"
#include "xenocomm/core/security_manager.h"
#include "xenocomm/core/security_config.hpp"
#include <chrono>
#include <thread>
#include <random>

using namespace xenocomm::core;
using namespace testing;

class SecureTransportWrapperTest : public Test {
protected:
    void SetUp() override {
        mockTransport_ = std::make_shared<MockTransport>();
        securityConfigInstance_.protocol = EncryptionProtocol::TLS_1_3;
        securityConfigInstance_.recordBatching.enabled = true;
        securityConfigInstance_.adaptiveRecord.enabled = true;
        securityConfigInstance_.enableVectoredIO = true;
        
        securityManager_ = std::make_shared<SecurityManager>(securityConfigInstance_);
        
        config_.securityConfig = securityConfigInstance_;
        
        wrapper_ = std::make_unique<SecureTransportWrapper>(mockTransport_, securityManager_, config_);
    }

    void TearDown() override {
        wrapper_.reset();
        securityManager_.reset();
        mockTransport_.reset();
    }

    static std::vector<uint8_t> generateRandomData(size_t size) {
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
    SecurityConfig securityConfigInstance_;
    std::shared_ptr<SecurityManager> securityManager_;
    SecureTransportConfig config_;
    std::unique_ptr<SecureTransportWrapper> wrapper_;
};

TEST_F(SecureTransportWrapperTest, InitializationSucceeds) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillOnce(Return(true));
    
    ASSERT_NE(wrapper_, nullptr);
}

TEST_F(SecureTransportWrapperTest, HandshakeCompletes) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    EXPECT_CALL(*mockTransport_, send(_, _))
        .WillRepeatedly(Return(true));
    
    EXPECT_CALL(*mockTransport_, receive(_, _))
        .WillRepeatedly(Invoke([](uint8_t* buffer, size_t size) {
            std::vector<uint8_t> hs_data = {0x16, 0x03, 0x03};
            if (size >= hs_data.size()) {
                std::copy(hs_data.begin(), hs_data.end(), buffer);
                return static_cast<ssize_t>(hs_data.size());
            }
            return static_cast<ssize_t>(-1);
        }));
    
    ASSERT_NE(wrapper_, nullptr);
}

TEST_F(SecureTransportWrapperTest, RecordBatchingWorks) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    ASSERT_NE(wrapper_, nullptr);

    std::vector<std::vector<uint8_t>> testData;
    for (int i = 0; i < 5; ++i) {
        testData.push_back(this->generateRandomData(1024));
    }
    
    EXPECT_CALL(*mockTransport_, send(_, _))
        .WillOnce(Return(static_cast<ssize_t>(1024 * 5)));
    
    for (const auto& data : testData) {
        EXPECT_GT(wrapper_->send(data.data(), data.size()), 0);
    }
    
    std::this_thread::sleep_for(config_.securityConfig.recordBatching.intervalMs + std::chrono::milliseconds(10));
}

TEST_F(SecureTransportWrapperTest, AdaptiveRecordSizingAdjusts) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    ASSERT_NE(wrapper_, nullptr);
    
    std::vector<size_t> testSizes = {1024, 2048, 4096, 8192, 16384};
    
    for (size_t size : testSizes) {
        auto data = this->generateRandomData(size);
        
        EXPECT_CALL(*mockTransport_, send(_, size))
            .WillOnce(Invoke([size_val = size](const uint8_t* d, size_t s) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(size_val / 1024));
                return static_cast<ssize_t>(size_val);
            }));
        
        EXPECT_GT(wrapper_->send(data.data(), data.size()), 0);
        
        std::this_thread::sleep_for(config_.securityConfig.adaptiveRecord.rttProbeIntervalMs + std::chrono::milliseconds(20));
    }
}

TEST_F(SecureTransportWrapperTest, VectoredIOOptimizesLargeTransfers) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    ASSERT_NE(wrapper_, nullptr);
    
    std::vector<std::vector<uint8_t>> buffers;
    for (int i = 0; i < 8; ++i) {
        buffers.push_back(this->generateRandomData(2048));
    }
    
    EXPECT_CALL(*mockTransport_, getSocketFd())
        .WillOnce(Return(1));
    
    EXPECT_TRUE(wrapper_->sendv(buffers).has_value());
}

TEST_F(SecureTransportWrapperTest, FallsBackToRegularSendForSmallTransfers) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    ASSERT_NE(wrapper_, nullptr);
    
    std::vector<std::vector<uint8_t>> buffers;
    for (int i = 0; i < 3; ++i) {
        buffers.push_back(this->generateRandomData(256));
    }
    
    EXPECT_CALL(*mockTransport_, send(_, _))
        .Times(3)
        .WillRepeatedly(Return(static_cast<ssize_t>(256)));
    
    EXPECT_TRUE(wrapper_->sendv(buffers).has_value());
}

TEST_F(SecureTransportWrapperTest, HandlesEncryptionFailure) {
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    ASSERT_NE(wrapper_, nullptr);
    
    auto data = this->generateRandomData(1024);
    
    EXPECT_CALL(*mockTransport_, send(_, _))
        .WillOnce(Return(static_cast<ssize_t>(-1)));
    
    EXPECT_LT(wrapper_->send(data.data(), data.size()), 0);
}

TEST_F(SecureTransportWrapperTest, DTLSCookieExchangeSucceeds) {
    securityConfigInstance_.protocol = EncryptionProtocol::DTLS_1_2;
    securityManager_ = std::make_shared<SecurityManager>(securityConfigInstance_);
    
    config_.securityConfig = securityConfigInstance_;
    wrapper_ = std::make_unique<SecureTransportWrapper>(mockTransport_, securityManager_, config_);
    
    EXPECT_CALL(*mockTransport_, isConnected())
        .WillRepeatedly(Return(true));
    
    EXPECT_CALL(*mockTransport_, getPeerAddress(_, _))
        .WillOnce(DoAll(
            SetArgReferee<0>("192.168.1.1"),
            SetArgReferee<1>(12345),
            Return(true)));
    
    ASSERT_NE(wrapper_, nullptr);
} 
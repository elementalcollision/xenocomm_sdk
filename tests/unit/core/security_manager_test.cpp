#include "xenocomm/core/security_manager.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>

namespace xenocomm {
namespace core {
namespace {

class SecurityManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory for test certificates
        testDir_ = std::filesystem::temp_directory_path() / "security_test";
        std::filesystem::create_directories(testDir_);
        
        // Set up paths for test certificates
        certPath_ = testDir_ / "test.crt";
        keyPath_ = testDir_ / "test.key";
        caPath_ = testDir_ / "ca.crt";
        
        // Create default configuration
        config_.certificatePath = certPath_.string();
        config_.privateKeyPath = keyPath_.string();
        config_.trustedCAsPath = caPath_.string();
        config_.protocol = EncryptionProtocol::TLS_1_3;
        config_.verifyPeer = true;
        config_.allowSelfSigned = true;
    }

    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(testDir_);
    }

    // Helper to create a test message
    std::vector<uint8_t> createTestMessage(size_t size = 1024) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i & 0xFF);
        }
        return data;
    }

    // Helper to verify a message was correctly encrypted/decrypted
    void verifyMessage(const std::vector<uint8_t>& original,
                      const std::vector<uint8_t>& processed) {
        ASSERT_EQ(original.size(), processed.size());
        for (size_t i = 0; i < original.size(); ++i) {
            EXPECT_EQ(original[i], processed[i])
                << "Mismatch at position " << i;
        }
    }

    SecurityConfig config_;
    std::filesystem::path testDir_;
    std::filesystem::path certPath_;
    std::filesystem::path keyPath_;
    std::filesystem::path caPath_;
};

TEST_F(SecurityManagerTest, InitializationWithDefaultConfig) {
    EXPECT_NO_THROW({
        SecurityManager manager(config_);
    });
}

TEST_F(SecurityManagerTest, GenerateSelfSignedCertificate) {
    SecurityManager manager(config_);
    
    auto result = manager.generateSelfSignedCert("test.xenocomm.local");
    ASSERT_TRUE(result.has_value()) << result.error();
    
    // Verify files were created
    EXPECT_TRUE(std::filesystem::exists(certPath_));
    EXPECT_TRUE(std::filesystem::exists(keyPath_));
}

TEST_F(SecurityManagerTest, CreateSecureContext) {
    SecurityManager manager(config_);
    
    // Generate test certificate
    auto result = manager.generateSelfSignedCert("test.xenocomm.local");
    ASSERT_TRUE(result.has_value()) << result.error();
    
    // Create server and client contexts
    auto serverContext = manager.createContext(true);
    ASSERT_TRUE(serverContext.has_value()) << serverContext.error();
    
    auto clientContext = manager.createContext(false);
    ASSERT_TRUE(clientContext.has_value()) << clientContext.error();
}

TEST_F(SecurityManagerTest, BasicEncryptionDecryption) {
    SecurityManager manager(config_);
    
    // Generate test certificate
    auto result = manager.generateSelfSignedCert("test.xenocomm.local");
    ASSERT_TRUE(result.has_value()) << result.error();
    
    // Create server and client contexts
    auto serverContextResult = manager.createContext(true);
    ASSERT_TRUE(serverContextResult.has_value()) << serverContextResult.error();
    auto serverContext = serverContextResult.value();
    
    auto clientContextResult = manager.createContext(false);
    ASSERT_TRUE(clientContextResult.has_value()) << clientContextResult.error();
    auto clientContext = clientContextResult.value();
    
    // Create test message
    auto originalMessage = createTestMessage();
    
    // Encrypt with server context
    auto encryptedResult = serverContext->encrypt(originalMessage);
    ASSERT_TRUE(encryptedResult.has_value()) << encryptedResult.error();
    
    // Decrypt with client context
    auto decryptedResult = clientContext->decrypt(encryptedResult.value());
    ASSERT_TRUE(decryptedResult.has_value()) << decryptedResult.error();
    
    // Verify message
    verifyMessage(originalMessage, decryptedResult.value());
}

TEST_F(SecurityManagerTest, UpdateConfiguration) {
    SecurityManager manager(config_);
    
    // Update configuration
    SecurityConfig newConfig = config_;
    newConfig.protocol = EncryptionProtocol::TLS_1_2;
    newConfig.verifyPeer = false;
    
    auto result = manager.updateConfig(newConfig);
    ASSERT_TRUE(result.has_value()) << result.error();
    
    // Verify configuration was updated
    const auto& currentConfig = manager.getConfig();
    EXPECT_EQ(currentConfig.protocol, EncryptionProtocol::TLS_1_2);
    EXPECT_FALSE(currentConfig.verifyPeer);
}

TEST_F(SecurityManagerTest, ValidatePeerCertificate) {
    SecurityManager manager(config_);
    
    // Generate test certificate
    auto result = manager.generateSelfSignedCert("test.xenocomm.local");
    ASSERT_TRUE(result.has_value()) << result.error();
    
    // Read certificate file
    std::ifstream certFile(certPath_, std::ios::binary);
    ASSERT_TRUE(certFile.is_open());
    
    std::vector<uint8_t> certData(
        (std::istreambuf_iterator<char>(certFile)),
        std::istreambuf_iterator<char>());
    
    // Validate certificate
    result = manager.validatePeerCertificate(certData);
    ASSERT_TRUE(result.has_value()) << result.error();
}

TEST_F(SecurityManagerTest, HandshakeCompletion) {
    SecurityManager manager(config_);
    
    // Generate test certificate
    auto result = manager.generateSelfSignedCert("test.xenocomm.local");
    ASSERT_TRUE(result.has_value()) << result.error();
    
    // Create server and client contexts
    auto serverContextResult = manager.createContext(true);
    ASSERT_TRUE(serverContextResult.has_value()) << serverContextResult.error();
    auto serverContext = serverContextResult.value();
    
    auto clientContextResult = manager.createContext(false);
    ASSERT_TRUE(clientContextResult.has_value()) << clientContextResult.error();
    auto clientContext = clientContextResult.value();
    
    // Perform handshake
    auto serverHandshake = serverContext->handshake();
    ASSERT_TRUE(serverHandshake.has_value()) << serverHandshake.error();
    
    auto clientHandshake = clientContext->handshake();
    ASSERT_TRUE(clientHandshake.has_value()) << clientHandshake.error();
    
    // Verify handshake completion
    EXPECT_TRUE(serverContext->isHandshakeComplete());
    EXPECT_TRUE(clientContext->isHandshakeComplete());
}

TEST_F(SecurityManagerTest, CipherSuiteNegotiation) {
    SecurityManager manager(config_);
    
    // Generate test certificate
    auto result = manager.generateSelfSignedCert("test.xenocomm.local");
    ASSERT_TRUE(result.has_value()) << result.error();
    
    // Create server and client contexts
    auto serverContextResult = manager.createContext(true);
    ASSERT_TRUE(serverContextResult.has_value()) << serverContextResult.error();
    auto serverContext = serverContextResult.value();
    
    auto clientContextResult = manager.createContext(false);
    ASSERT_TRUE(clientContextResult.has_value()) << clientContextResult.error();
    auto clientContext = clientContextResult.value();
    
    // Perform handshake
    auto serverHandshake = serverContext->handshake();
    ASSERT_TRUE(serverHandshake.has_value()) << serverHandshake.error();
    
    auto clientHandshake = clientContext->handshake();
    ASSERT_TRUE(clientHandshake.has_value()) << clientHandshake.error();
    
    // Verify negotiated cipher suite
    auto serverCipher = serverContext->getNegotiatedCipherSuite();
    auto clientCipher = clientContext->getNegotiatedCipherSuite();
    
    EXPECT_EQ(serverCipher, clientCipher);
    EXPECT_TRUE(serverCipher == CipherSuite::AES_256_GCM_SHA384 ||
                serverCipher == CipherSuite::CHACHA20_POLY1305_SHA256);
}

} // namespace
} // namespace core
} // namespace xenocomm 
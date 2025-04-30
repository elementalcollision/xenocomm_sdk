#include <gtest/gtest.h>
#include "xenocomm/core/authentication_manager.hpp"
#include "xenocomm/core/certificate_auth_provider.hpp"
#include "xenocomm/core/token_auth_provider.hpp"
#include "xenocomm/core/mock_transport.hpp"
#include <filesystem>
#include <fstream>

namespace xenocomm {
namespace core {
namespace testing {

class AuthenticationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test certificates
        createTestCertificates();
        
        // Set up authentication manager
        authManager = std::make_unique<AuthenticationManager>();
    }

    void TearDown() override {
        // Clean up test certificates
        std::filesystem::remove_all("test_certs");
    }

    void createTestCertificates() {
        std::filesystem::create_directory("test_certs");
        
        // Create a test CA certificate
        system("openssl req -x509 -newkey rsa:2048 -keyout test_certs/ca.key -out test_certs/ca.crt "
               "-days 365 -nodes -subj '/CN=Test CA' 2>/dev/null");
        
        // Create a test client certificate
        system("openssl req -newkey rsa:2048 -keyout test_certs/client.key -out test_certs/client.csr "
               "-nodes -subj '/CN=TestAgent' 2>/dev/null");
        system("openssl x509 -req -in test_certs/client.csr -CA test_certs/ca.crt "
               "-CAkey test_certs/ca.key -CAcreateserial -out test_certs/client.crt "
               "-days 365 2>/dev/null");
        
        // Create a test CRL
        system("openssl ca -gencrl -keyfile test_certs/ca.key -cert test_certs/ca.crt "
               "-out test_certs/test.crl -config /dev/null 2>/dev/null");
    }

    std::vector<uint8_t> loadCertificate(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>());
    }

    std::unique_ptr<AuthenticationManager> authManager;
};

TEST_F(AuthenticationTest, CertificateAuthenticationSuccess) {
    // Configure certificate provider
    CertificateAuthConfig certConfig;
    certConfig.caPath = "test_certs/ca.crt";
    certConfig.crlPath = "test_certs/test.crl";
    certConfig.allowSelfSigned = false;
    certConfig.maxValidityDays = 365;
    
    auto certProvider = std::make_shared<CertificateAuthProvider>(certConfig);
    ASSERT_TRUE(authManager->registerProvider(certProvider));

    // Create authentication context
    AuthenticationContext context;
    context.credentials = loadCertificate("test_certs/client.crt");
    context.transport = std::make_shared<MockTransport>();

    // Attempt authentication
    auto result = authManager->authenticate("certificate", context);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.agentId, "TestAgent");
}

TEST_F(AuthenticationTest, CertificateAuthenticationFailure) {
    // Configure certificate provider with wrong CA
    CertificateAuthConfig certConfig;
    certConfig.caPath = "test_certs/client.crt";  // Wrong CA cert
    certConfig.allowSelfSigned = false;
    
    auto certProvider = std::make_shared<CertificateAuthProvider>(certConfig);
    ASSERT_TRUE(authManager->registerProvider(certProvider));

    // Create authentication context
    AuthenticationContext context;
    context.credentials = loadCertificate("test_certs/client.crt");
    context.transport = std::make_shared<MockTransport>();

    // Attempt authentication
    auto result = authManager->authenticate("certificate", context);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(AuthenticationTest, TokenAuthenticationSuccess) {
    // Configure token provider
    TokenAuthConfig tokenConfig;
    tokenConfig.validator = [](const std::string& token, 
                             std::string& agentId,
                             std::string& error) {
        if (token == "valid_token") {
            agentId = "TestAgent";
            return true;
        }
        error = "Invalid token";
        return false;
    };
    
    auto tokenProvider = std::make_shared<TokenAuthProvider>(tokenConfig);
    ASSERT_TRUE(authManager->registerProvider(tokenProvider));

    // Create authentication context
    AuthenticationContext context;
    std::string token = "valid_token";
    context.credentials = std::vector<uint8_t>(token.begin(), token.end());
    context.transport = std::make_shared<MockTransport>();

    // Attempt authentication
    auto result = authManager->authenticate("token", context);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.agentId, "TestAgent");
}

TEST_F(AuthenticationTest, TokenAuthenticationFailure) {
    // Configure token provider
    TokenAuthConfig tokenConfig;
    tokenConfig.validator = [](const std::string& token, 
                             std::string& agentId,
                             std::string& error) {
        error = "Invalid token";
        return false;
    };
    
    auto tokenProvider = std::make_shared<TokenAuthProvider>(tokenConfig);
    ASSERT_TRUE(authManager->registerProvider(tokenProvider));

    // Create authentication context
    AuthenticationContext context;
    std::string token = "invalid_token";
    context.credentials = std::vector<uint8_t>(token.begin(), token.end());
    context.transport = std::make_shared<MockTransport>();

    // Attempt authentication
    auto result = authManager->authenticate("token", context);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(AuthenticationTest, TokenReuse) {
    // Configure token provider with no reuse allowed
    TokenAuthConfig tokenConfig;
    tokenConfig.allowReuse = false;
    tokenConfig.validator = [](const std::string& token, 
                             std::string& agentId,
                             std::string& error) {
        if (token == "valid_token") {
            agentId = "TestAgent";
            return true;
        }
        error = "Invalid token";
        return false;
    };
    
    auto tokenProvider = std::make_shared<TokenAuthProvider>(tokenConfig);
    ASSERT_TRUE(authManager->registerProvider(tokenProvider));

    // Create authentication context
    AuthenticationContext context;
    std::string token = "valid_token";
    context.credentials = std::vector<uint8_t>(token.begin(), token.end());
    context.transport = std::make_shared<MockTransport>();

    // First authentication should succeed
    auto result1 = authManager->authenticate("token", context);
    EXPECT_TRUE(result1.success);

    // Second authentication with same token should fail
    auto result2 = authManager->authenticate("token", context);
    EXPECT_FALSE(result2.success);
}

TEST_F(AuthenticationTest, TokenRevocation) {
    // Configure token provider
    TokenAuthConfig tokenConfig;
    tokenConfig.validator = [](const std::string& token, 
                             std::string& agentId,
                             std::string& error) {
        if (token == "valid_token") {
            agentId = "TestAgent";
            return true;
        }
        error = "Invalid token";
        return false;
    };
    
    auto tokenProvider = std::make_shared<TokenAuthProvider>(tokenConfig);
    ASSERT_TRUE(authManager->registerProvider(tokenProvider));

    // Create authentication context
    AuthenticationContext context;
    std::string token = "valid_token";
    context.credentials = std::vector<uint8_t>(token.begin(), token.end());
    context.transport = std::make_shared<MockTransport>();

    // First authentication should succeed
    auto result1 = authManager->authenticate("token", context);
    EXPECT_TRUE(result1.success);

    // Revoke the token
    tokenProvider->revokeToken(token);

    // Authentication with revoked token should fail
    auto result2 = authManager->authenticate("token", context);
    EXPECT_FALSE(result2.success);
}

TEST_F(AuthenticationTest, AuthenticationCallback) {
    bool callbackCalled = false;
    std::string callbackAgentId;
    bool callbackSuccess;

    // Set authentication callback
    authManager->setAuthenticationCallback(
        [&](const AuthResult& result) {
            callbackCalled = true;
            callbackAgentId = result.agentId;
            callbackSuccess = result.success;
        });

    // Configure token provider
    TokenAuthConfig tokenConfig;
    tokenConfig.validator = [](const std::string& token, 
                             std::string& agentId,
                             std::string& error) {
        if (token == "valid_token") {
            agentId = "TestAgent";
            return true;
        }
        error = "Invalid token";
        return false;
    };
    
    auto tokenProvider = std::make_shared<TokenAuthProvider>(tokenConfig);
    ASSERT_TRUE(authManager->registerProvider(tokenProvider));

    // Create authentication context
    AuthenticationContext context;
    std::string token = "valid_token";
    context.credentials = std::vector<uint8_t>(token.begin(), token.end());
    context.transport = std::make_shared<MockTransport>();

    // Attempt authentication
    auto result = authManager->authenticate("token", context);
    
    // Verify callback was called with correct information
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(callbackAgentId, "TestAgent");
    EXPECT_TRUE(callbackSuccess);
}

} // namespace testing
} // namespace core
} // namespace xenocomm 
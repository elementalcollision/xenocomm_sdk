#pragma once

#include "xenocomm/core/authentication_manager.hpp"
#include <string>
#include <memory>
#include <functional>
#include <chrono>

namespace xenocomm {
namespace core {

/**
 * @brief Configuration for token-based authentication
 */
struct TokenAuthConfig {
    // Token validation function type
    using TokenValidator = std::function<bool(const std::string& token, 
                                            std::string& agentId,
                                            std::string& error)>;
    
    TokenValidator validator;           // Custom token validation function
    std::chrono::seconds tokenTTL{3600}; // Token time-to-live (default 1 hour)
    bool allowReuse{false};            // Whether to allow token reuse
    size_t minTokenLength{32};         // Minimum token length
    size_t maxTokenLength{512};        // Maximum token length
};

/**
 * @brief Provider for token-based authentication
 */
class TokenAuthProvider : public AuthenticationProvider {
public:
    explicit TokenAuthProvider(TokenAuthConfig config);
    ~TokenAuthProvider() override;

    // AuthenticationProvider interface
    bool initialize() override;
    AuthResult authenticate(const AuthenticationContext& context) override;
    std::string getMethodName() const override;

    /**
     * @brief Revoke a specific token
     * @param token Token to revoke
     */
    void revokeToken(const std::string& token);

    /**
     * @brief Clear expired tokens from storage
     */
    void cleanupExpiredTokens();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace core
} // namespace xenocomm 
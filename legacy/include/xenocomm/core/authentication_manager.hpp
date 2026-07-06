#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "xenocomm/core/security_manager.h"
#include "xenocomm/core/transport_protocol.hpp"

namespace xenocomm {
namespace core {

// Forward declarations
class AuthenticationProvider;
class AuthenticationContext;

/**
 * @brief Authentication result with status and optional error message
 */
struct AuthResult {
    bool success{false};
    std::string errorMessage;
    std::string agentId;  // Unique identifier for the authenticated agent

    static AuthResult Success(const std::string& agentId) {
        return AuthResult{true, "", agentId};
    }

    static AuthResult Failure(const std::string& error) {
        return AuthResult{false, error, ""};
    }
};

/**
 * @brief Interface for authentication providers
 */
class AuthenticationProvider {
public:
    virtual ~AuthenticationProvider() = default;
    
    /**
     * @brief Initialize the authentication provider
     * @return true if initialization successful
     */
    virtual bool initialize() = 0;
    
    /**
     * @brief Authenticate an agent
     * @param context Authentication context containing credentials
     * @return Authentication result
     */
    virtual AuthResult authenticate(const AuthenticationContext& context) = 0;
    
    /**
     * @brief Get the authentication method name
     * @return String identifier for this authentication method
     */
    virtual std::string getMethodName() const = 0;
};

/**
 * @brief Context containing authentication credentials and metadata
 */
class AuthenticationContext {
public:
    // Common authentication data
    std::string agentId;
    std::vector<uint8_t> credentials;
    std::unordered_map<std::string, std::string> metadata;
    
    // Connection-specific data
    std::shared_ptr<TransportProtocol> transport;
    std::shared_ptr<SecurityManager> securityManager;
    
    // Timing configuration
    std::chrono::milliseconds timeout{5000};  // Default 5 second timeout
    int maxRetries{3};                        // Default 3 retries
};

/**
 * @brief Callback type for authentication events
 */
using AuthenticationCallback = std::function<void(const AuthResult&)>;

/**
 * @brief Manager class for handling agent authentication
 */
class AuthenticationManager {
public:
    AuthenticationManager();
    ~AuthenticationManager();

    /**
     * @brief Register an authentication provider
     * @param provider Authentication provider implementation
     * @return true if registration successful
     */
    bool registerProvider(std::shared_ptr<AuthenticationProvider> provider);

    /**
     * @brief Remove a registered authentication provider
     * @param methodName Name of the authentication method to remove
     */
    void unregisterProvider(const std::string& methodName);

    /**
     * @brief Authenticate an agent using the specified method
     * @param methodName Authentication method to use
     * @param context Authentication context with credentials
     * @return Authentication result
     */
    AuthResult authenticate(const std::string& methodName, 
                          const AuthenticationContext& context);

    /**
     * @brief Set callback for authentication events
     * @param callback Function to call on authentication events
     */
    void setAuthenticationCallback(AuthenticationCallback callback);

    /**
     * @brief Check if an agent is authenticated
     * @param agentId Unique identifier for the agent
     * @return true if the agent is authenticated
     */
    bool isAuthenticated(const std::string& agentId) const;

    /**
     * @brief Revoke authentication for an agent
     * @param agentId Unique identifier for the agent
     */
    void revokeAuthentication(const std::string& agentId);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace core
} // namespace xenocomm 
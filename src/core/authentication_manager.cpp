#include "xenocomm/core/authentication_manager.hpp"
#include <mutex>
#include <chrono>
#include <thread>

namespace xenocomm {
namespace core {

class AuthenticationManager::Impl {
public:
    Impl() = default;
    ~Impl() = default;

    bool registerProvider(std::shared_ptr<AuthenticationProvider> provider) {
        if (!provider) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        const auto& methodName = provider->getMethodName();
        
        // Don't allow duplicate providers
        if (providers_.find(methodName) != providers_.end()) {
            return false;
        }

        // Initialize the provider
        if (!provider->initialize()) {
            return false;
        }

        providers_[methodName] = provider;
        return true;
    }

    void unregisterProvider(const std::string& methodName) {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_.erase(methodName);
    }

    AuthResult authenticate(const std::string& methodName, 
                          const AuthenticationContext& context) {
        std::shared_ptr<AuthenticationProvider> provider;
        
        // Get the provider
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = providers_.find(methodName);
            if (it == providers_.end()) {
                return AuthResult::Failure("Authentication method not found");
            }
            provider = it->second;
        }

        // Attempt authentication with retries
        AuthResult result;
        int attempts = 0;
        auto startTime = std::chrono::steady_clock::now();

        do {
            result = provider->authenticate(context);
            if (result.success) {
                // Store successful authentication
                std::lock_guard<std::mutex> lock(mutex_);
                authenticatedAgents_[result.agentId] = std::chrono::steady_clock::now();
                
                // Notify callback if set
                if (callback_) {
                    callback_(result);
                }
                return result;
            }

            // Check timeout
            auto currentTime = std::chrono::steady_clock::now();
            if (currentTime - startTime > context.timeout) {
                return AuthResult::Failure("Authentication timeout");
            }

            // Wait before retry
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            attempts++;
        } while (attempts < context.maxRetries);

        // Notify callback of failure
        if (callback_) {
            callback_(result);
        }

        return result;
    }

    void setAuthenticationCallback(AuthenticationCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        callback_ = std::move(callback);
    }

    bool isAuthenticated(const std::string& agentId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return authenticatedAgents_.find(agentId) != authenticatedAgents_.end();
    }

    void revokeAuthentication(const std::string& agentId) {
        std::lock_guard<std::mutex> lock(mutex_);
        authenticatedAgents_.erase(agentId);
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<AuthenticationProvider>> providers_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> authenticatedAgents_;
    AuthenticationCallback callback_;
};

// AuthenticationManager implementation
AuthenticationManager::AuthenticationManager() : impl_(std::make_unique<Impl>()) {}
AuthenticationManager::~AuthenticationManager() = default;

bool AuthenticationManager::registerProvider(std::shared_ptr<AuthenticationProvider> provider) {
    return impl_->registerProvider(std::move(provider));
}

void AuthenticationManager::unregisterProvider(const std::string& methodName) {
    impl_->unregisterProvider(methodName);
}

AuthResult AuthenticationManager::authenticate(const std::string& methodName, 
                                            const AuthenticationContext& context) {
    return impl_->authenticate(methodName, context);
}

void AuthenticationManager::setAuthenticationCallback(AuthenticationCallback callback) {
    impl_->setAuthenticationCallback(std::move(callback));
}

bool AuthenticationManager::isAuthenticated(const std::string& agentId) const {
    return impl_->isAuthenticated(agentId);
}

void AuthenticationManager::revokeAuthentication(const std::string& agentId) {
    impl_->revokeAuthentication(agentId);
}

} // namespace core
} // namespace xenocomm 
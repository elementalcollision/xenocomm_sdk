#include "xenocomm/core/token_auth_provider.hpp"
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace xenocomm {
namespace core {

struct TokenInfo {
    std::string agentId;
    std::chrono::steady_clock::time_point expiryTime;
};

class TokenAuthProvider::Impl {
public:
    explicit Impl(TokenAuthConfig config) : config_(std::move(config)) {}

    bool initialize() {
        if (!config_.validator) {
            return false;  // Validator function is required
        }
        return true;
    }

    AuthResult authenticate(const AuthenticationContext& context) {
        // Convert credentials to string token
        std::string token(reinterpret_cast<const char*>(context.credentials.data()),
                         context.credentials.size());

        // Basic token validation
        if (token.length() < config_.minTokenLength || 
            token.length() > config_.maxTokenLength) {
            return AuthResult::Failure("Invalid token length");
        }

        // Check if token is revoked
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (revokedTokens_.find(token) != revokedTokens_.end()) {
                return AuthResult::Failure("Token has been revoked");
            }
        }

        // Check if token is already in use (if reuse is not allowed)
        if (!config_.allowReuse) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeTokens_.find(token) != activeTokens_.end()) {
                return AuthResult::Failure("Token is already in use");
            }
        }

        // Validate token using provided validator
        std::string agentId, error;
        if (!config_.validator(token, agentId, error)) {
            return AuthResult::Failure(error);
        }

        // Store token information
        auto now = std::chrono::steady_clock::now();
        auto expiryTime = now + config_.tokenTTL;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeTokens_[token] = TokenInfo{
                agentId,
                expiryTime
            };
        }

        return AuthResult::Success(agentId);
    }

    std::string getMethodName() const {
        return "token";
    }

    void revokeToken(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        activeTokens_.erase(token);
        revokedTokens_.insert(token);
    }

    void cleanupExpiredTokens() {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);

        // Remove expired tokens
        for (auto it = activeTokens_.begin(); it != activeTokens_.end();) {
            if (it->second.expiryTime <= now) {
                it = activeTokens_.erase(it);
            } else {
                ++it;
            }
        }

        // Optionally, we could also clean up old revoked tokens here
        // but keeping them indefinitely provides better security against replay attacks
    }

private:
    TokenAuthConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TokenInfo> activeTokens_;
    std::unordered_set<std::string> revokedTokens_;
};

// TokenAuthProvider implementation
TokenAuthProvider::TokenAuthProvider(TokenAuthConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

TokenAuthProvider::~TokenAuthProvider() = default;

bool TokenAuthProvider::initialize() {
    return impl_->initialize();
}

AuthResult TokenAuthProvider::authenticate(const AuthenticationContext& context) {
    return impl_->authenticate(context);
}

std::string TokenAuthProvider::getMethodName() const {
    return impl_->getMethodName();
}

void TokenAuthProvider::revokeToken(const std::string& token) {
    impl_->revokeToken(token);
}

void TokenAuthProvider::cleanupExpiredTokens() {
    impl_->cleanupExpiredTokens();
}

} // namespace core
} // namespace xenocomm 
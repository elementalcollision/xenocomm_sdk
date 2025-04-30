#include "xenocomm/core/timeout_negotiation_protocol.h"
#include <sstream>
#include <iostream>
#include <cmath>

namespace xenocomm {
namespace core {

TimeoutNegotiationProtocol::TimeoutNegotiationProtocol(const TimeoutConfig& config, bool enableLogging)
    : config_(config)
    , enableLogging_(enableLogging) {
    // Start the cleanup thread
    cleanupThread_ = std::thread(&TimeoutNegotiationProtocol::cleanupTimedOutSessions, this);
}

TimeoutNegotiationProtocol::~TimeoutNegotiationProtocol() noexcept {
    try {
        // Signal the cleanup thread to stop and wait for it
        shouldStopCleanup_ = true;
        if (cleanupThread_.joinable()) {
            cleanupThread_.join();
        }

        // Clean up any remaining active sessions
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.clear();
    } catch (const std::exception& e) {
        if (enableLogging_) {
            std::cerr << "Error during TimeoutNegotiationProtocol cleanup: " << e.what() << std::endl;
        }
    }
}

SessionId TimeoutNegotiationProtocol::initiateSession(const std::string& targetAgentId,
                                                     const NegotiableParams& proposedParams) {
    SessionId sessionId = nextSessionId_++;
    
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto now = std::chrono::steady_clock::now();
        
        SessionData sessionData{
            .startTime = now,
            .lastActivityTime = now,
            .state = NegotiationState::INITIATING,
            .proposedParams = proposedParams
        };
        
        sessions_.emplace(sessionId, std::move(sessionData));
    }
    
    if (!attemptWithRetry(sessionId, [&]() {
        // Actual network communication would happen here
        // For now, just simulate success
        return true;
    })) {
        throw std::runtime_error("Failed to initiate session after maximum retries");
    }
    
    updateActivityTime(sessionId);
    return sessionId;
}

bool TimeoutNegotiationProtocol::respondToNegotiation(const SessionId sessionId,
                                                     const NegotiationResponse responseType,
                                                     const std::optional<NegotiableParams>& responseParams) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end() || hasSessionTimedOut(sessionId)) {
        throw std::runtime_error("Invalid or timed out session ID");
    }
    
    auto& session = it->second;
    if (session.state != NegotiationState::PROPOSAL_RECEIVED) {
        throw std::runtime_error("Session not in correct state for response");
    }
    
    bool success = attemptWithRetry(sessionId, [&]() {
        // Actual response sending logic would go here
        session.state = NegotiationState::RESPONDING;
        
        if (responseType == NegotiationResponse::ACCEPTED) {
            session.agreedParams = session.proposedParams;
            session.state = NegotiationState::AWAITING_FINALIZATION;
        } else if (responseType == NegotiationResponse::COUNTER_PROPOSAL) {
            if (!responseParams) {
                throw std::runtime_error("Counter proposal requires parameters");
            }
            session.proposedParams = *responseParams;
            session.state = NegotiationState::AWAITING_FINALIZATION;
        } else {
            session.state = NegotiationState::FAILED;
        }
        
        return true;
    });
    
    updateActivityTime(sessionId);
    return success;
}

NegotiableParams TimeoutNegotiationProtocol::finalizeSession(const SessionId sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end() || hasSessionTimedOut(sessionId)) {
        throw std::runtime_error("Invalid or timed out session ID");
    }
    
    auto& session = it->second;
    if (session.state != NegotiationState::AWAITING_FINALIZATION) {
        throw std::runtime_error("Session not ready for finalization");
    }
    
    bool success = attemptWithRetry(sessionId, [&]() {
        // Actual finalization logic would go here
        session.state = NegotiationState::FINALIZED;
        return true;
    });
    
    if (!success) {
        throw std::runtime_error("Failed to finalize session");
    }
    
    updateActivityTime(sessionId);
    return session.agreedParams.value_or(session.proposedParams);
}

NegotiationState TimeoutNegotiationProtocol::getSessionState(const SessionId sessionId) const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        throw std::runtime_error("Invalid session ID");
    }
    
    if (hasSessionTimedOut(sessionId)) {
        return NegotiationState::FAILED;
    }
    
    return it->second.state;
}

std::optional<NegotiableParams> TimeoutNegotiationProtocol::getNegotiatedParams(const SessionId sessionId) const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        throw std::runtime_error("Invalid session ID");
    }
    
    const auto& session = it->second;
    if (session.state != NegotiationState::FINALIZED || hasSessionTimedOut(sessionId)) {
        return std::nullopt;
    }
    
    return session.agreedParams;
}

bool TimeoutNegotiationProtocol::acceptCounterProposal(const SessionId sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end() || hasSessionTimedOut(sessionId)) {
        throw std::runtime_error("Invalid or timed out session ID");
    }
    
    auto& session = it->second;
    if (session.state != NegotiationState::COUNTER_RECEIVED) {
        throw std::runtime_error("No counter proposal to accept");
    }
    
    bool success = attemptWithRetry(sessionId, [&]() {
        session.state = NegotiationState::FINALIZING;
        session.agreedParams = session.proposedParams;
        return true;
    });
    
    updateActivityTime(sessionId);
    return success;
}

bool TimeoutNegotiationProtocol::rejectCounterProposal(const SessionId sessionId,
                                                      const std::optional<std::string>& reason) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end() || hasSessionTimedOut(sessionId)) {
        throw std::runtime_error("Invalid or timed out session ID");
    }
    
    auto& session = it->second;
    if (session.state != NegotiationState::COUNTER_RECEIVED) {
        throw std::runtime_error("No counter proposal to reject");
    }
    
    bool success = attemptWithRetry(sessionId, [&]() {
        session.state = NegotiationState::FAILED;
        return true;
    });
    
    updateActivityTime(sessionId);
    return success;
}

bool TimeoutNegotiationProtocol::closeSession(const SessionId sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return false;
    }
    
    auto& session = it->second;
    session.state = NegotiationState::CLOSED;
    session.isActive = false;
    
    return true;
}

bool TimeoutNegotiationProtocol::attemptWithRetry(const SessionId sessionId,
                                                 const std::function<bool()>& operation) {
    auto& session = sessions_.at(sessionId);
    
    for (uint8_t attempt = 0; attempt <= config_.maxRetries; ++attempt) {
        if (hasSessionTimedOut(sessionId)) {
            if (enableLogging_) {
                std::cerr << "Session " << sessionId << " timed out during retry attempt "
                         << static_cast<int>(attempt) << std::endl;
            }
            return false;
        }
        
        try {
            if (operation()) {
                if (enableLogging_ && attempt > 0) {
                    std::cout << "Operation succeeded after " << static_cast<int>(attempt)
                             << " retries for session " << sessionId << std::endl;
                }
                return true;
            }
        } catch (const std::exception& e) {
            if (enableLogging_) {
                std::cerr << "Operation failed, attempt " << static_cast<int>(attempt) + 1 
                         << " of " << static_cast<int>(config_.maxRetries)
                         << " for session " << sessionId << ": " << e.what() << std::endl;
            }
        }
        
        if (attempt < config_.maxRetries) {
            // Calculate exponential backoff with jitter
            auto baseDelay = config_.retryInterval.count();
            auto maxJitter = baseDelay * 0.1; // 10% jitter
            auto backoffDelay = baseDelay * std::pow(2, attempt); // Exponential backoff
            auto jitter = static_cast<long>(std::rand() % static_cast<int>(maxJitter));
            auto totalDelay = std::min(backoffDelay + jitter,
                                     static_cast<double>(config_.negotiationTimeout.count()));
            
            if (enableLogging_) {
                std::cout << "Retrying operation for session " << sessionId
                         << " in " << totalDelay << "ms (attempt "
                         << static_cast<int>(attempt) + 1 << ")" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long>(totalDelay)));
            session.retryCount++;
        }
    }
    
    if (enableLogging_) {
        std::cerr << "Operation failed after " << static_cast<int>(config_.maxRetries)
                  << " retries for session " << sessionId << std::endl;
    }
    return false;
}

bool TimeoutNegotiationProtocol::hasSessionTimedOut(const SessionId sessionId) const {
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return true;
    }

    const auto& session = it->second;
    auto now = std::chrono::steady_clock::now();

    // Check overall negotiation timeout
    if (now - session.startTime > config_.negotiationTimeout) {
        if (enableLogging_) {
            std::cerr << "Session " << sessionId << " exceeded negotiation timeout of "
                     << config_.negotiationTimeout.count() << "ms" << std::endl;
        }
        return true;
    }

    // Check response timeout
    if (now - session.lastActivityTime > config_.responseTimeout) {
        if (enableLogging_) {
            std::cerr << "Session " << sessionId << " exceeded response timeout of "
                     << config_.responseTimeout.count() << "ms" << std::endl;
        }
        return true;
    }

    return false;
}

void TimeoutNegotiationProtocol::updateActivityTime(const SessionId sessionId) {
    auto it = sessions_.find(sessionId);
    if (it != sessions_.end()) {
        it->second.lastActivityTime = std::chrono::steady_clock::now();
        it->second.retryCount = 0; // Reset retry count on successful activity
    }
}

void TimeoutNegotiationProtocol::cleanupTimedOutSessions() {
    while (!shouldStopCleanup_) {
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            auto it = sessions_.begin();
            while (it != sessions_.end()) {
                if (!it->second.isActive || hasSessionTimedOut(it->first)) {
                    if (enableLogging_) {
                        std::cerr << "Cleaning up " << (it->second.isActive ? "timed out" : "inactive")
                                 << " session " << it->first << std::endl;
                    }
                    it = sessions_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Sleep for half the response timeout duration before next cleanup cycle
        std::this_thread::sleep_for(config_.responseTimeout / 2);
    }
}

} // namespace core
} // namespace xenocomm 
#pragma once

#include "xenocomm/core/negotiation_protocol.h"
#include <chrono>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>

namespace xenocomm {
namespace core {

/**
 * @brief Configuration for timeout and retry behavior in negotiation sessions.
 */
struct TimeoutConfig {
    std::chrono::milliseconds negotiationTimeout{3000};  // Default 3 seconds for overall negotiation
    std::chrono::milliseconds responseTimeout{1000};     // Default 1 second for individual responses
    std::chrono::milliseconds retryInterval{500};        // Default 500ms between retries
    uint8_t maxRetries{3};                              // Maximum number of retry attempts
};

/**
 * @brief Implementation of NegotiationProtocol with timeout and retry capabilities.
 * 
 * This class extends the base NegotiationProtocol interface with:
 * - Configurable timeouts for different negotiation phases
 * - Automatic retry logic with exponential backoff
 * - Session cleanup for timed-out negotiations
 * - Thread-safe session state management
 */
class TimeoutNegotiationProtocol : public NegotiationProtocol {
public:
    /**
     * @brief Constructs a TimeoutNegotiationProtocol with the specified configuration.
     * 
     * @param config Timeout and retry configuration
     * @param enableLogging Whether to enable logging (default: true)
     */
    explicit TimeoutNegotiationProtocol(const TimeoutConfig& config = TimeoutConfig(),
                                      bool enableLogging = true);

    /**
     * @brief Destructor that ensures cleanup of background threads and resources.
     */
    ~TimeoutNegotiationProtocol() noexcept override;

    // Implementation of base class virtual methods
    SessionId initiateSession(const std::string& targetAgentId,
                            const NegotiableParams& proposedParams) override;

    bool respondToNegotiation(const SessionId sessionId,
                            const NegotiationResponse responseType,
                            const std::optional<NegotiableParams>& responseParams) override;

    NegotiableParams finalizeSession(const SessionId sessionId) override;

    NegotiationState getSessionState(const SessionId sessionId) const override;

    std::optional<NegotiableParams> getNegotiatedParams(const SessionId sessionId) const override;

    bool acceptCounterProposal(const SessionId sessionId) override;

    bool rejectCounterProposal(const SessionId sessionId,
                              const std::optional<std::string>& reason) override;

    bool closeSession(const SessionId sessionId) override;

protected:
    /**
     * @brief Internal session data structure for tracking timeouts and retries.
     */
    struct SessionData {
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastActivityTime;
        uint8_t retryCount{0};
        NegotiationState state{NegotiationState::IDLE};
        NegotiableParams proposedParams;
        std::optional<NegotiableParams> agreedParams;
        std::atomic<bool> isActive{true};
    };

    /**
     * @brief Attempts an operation with retry logic.
     * 
     * @param sessionId The session ID for the operation
     * @param operation The operation to attempt
     * @return True if the operation succeeded within retry limits, false otherwise
     */
    bool attemptWithRetry(const SessionId sessionId,
                         const std::function<bool()>& operation);

    /**
     * @brief Checks if a session has timed out.
     * 
     * @param sessionId The session to check
     * @return True if the session has timed out, false otherwise
     */
    bool hasSessionTimedOut(const SessionId sessionId) const;

    /**
     * @brief Updates the last activity time for a session.
     * 
     * @param sessionId The session to update
     */
    void updateActivityTime(const SessionId sessionId);

    /**
     * @brief Background thread function for cleaning up timed-out sessions.
     */
    void cleanupTimedOutSessions();

private:
    TimeoutConfig config_;
    bool enableLogging_;
    
    // Thread-safe session management
    mutable std::mutex sessionsMutex_;
    std::unordered_map<SessionId, SessionData> sessions_;
    
    // Cleanup thread management
    std::thread cleanupThread_;
    std::atomic<bool> shouldStopCleanup_{false};

    // Session ID generation
    std::atomic<SessionId> nextSessionId_{1};
};

} // namespace core
} // namespace xenocomm 
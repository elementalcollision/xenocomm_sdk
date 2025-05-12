#include "xenocomm/core/negotiation_protocol.h" // Include the header for definitions
#include "xenocomm/core/security_manager.h" 
#include "xenocomm/utils/serialization.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>
#include <algorithm>
#include <stdexcept>
#include <iomanip> 
#include <sstream> 
#include <unordered_set> // Added missing include
#include <variant> // For std::variant
#include <optional> // Added missing include
#include <memory> // Added missing include

namespace xenocomm {
namespace core {

// --- Constants for timeouts and retries ---
constexpr std::chrono::milliseconds DEFAULT_NEGOTIATION_TIMEOUT{3000};  // 3 seconds
constexpr std::chrono::milliseconds RETRY_INTERVAL{500};  // 500ms between retries
constexpr uint8_t MAX_NEGOTIATION_RETRIES = 3;
constexpr uint16_t MAX_PARAMETER_COUNT = 64;  // Maximum parameters in a single negotiation

// --- Validation logic ---
namespace validation {
    enum class ValidationResult {
        VALID,
        INVALID_DATA_FORMAT,
        INVALID_COMPRESSION_ALGORITHM,
        INVALID_ERROR_CORRECTION_SCHEME,
        INCOMPATIBLE_FORMAT_COMPRESSION,
        INCOMPATIBLE_FORMAT_ERROR_CORRECTION,
        MISSING_REQUIRED_PARAMETER,
        INVALID_ENCRYPTION_ALGORITHM,
        INVALID_KEY_EXCHANGE_METHOD,
        INVALID_AUTHENTICATION_METHOD,
        INVALID_KEY_SIZE,
        INCOMPATIBLE_ENCRYPTION_KEY_SIZE,
        INCOMPATIBLE_KEY_EXCHANGE_ENCRYPTION
    };

    bool isValidDataFormat(DataFormat format) {
        switch(format) {
            case DataFormat::VECTOR_FLOAT32:
            case DataFormat::VECTOR_INT8:
            case DataFormat::COMPRESSED_STATE:
            case DataFormat::BINARY_CUSTOM:
            case DataFormat::GGWAVE_FSK:
                return true;
            default:
                return false;
        }
    }

    bool isValidCompressionAlgorithm(CompressionAlgorithm algo) {
        switch(algo) {
            case CompressionAlgorithm::NONE:
            case CompressionAlgorithm::ZLIB:
            case CompressionAlgorithm::LZ4:
            case CompressionAlgorithm::ZSTD:
                return true;
            default:
                return false;
        }
    }

    bool isValidErrorCorrectionScheme(ErrorCorrectionScheme scheme) {
        switch(scheme) {
            case ErrorCorrectionScheme::NONE:
            case ErrorCorrectionScheme::CHECKSUM_ONLY:
            case ErrorCorrectionScheme::REED_SOLOMON:
                return true;
            default:
                return false;
        }
    }

    bool isValidEncryptionAlgorithm(EncryptionAlgorithm algo) {
        switch(algo) {
            case EncryptionAlgorithm::NONE:
            case EncryptionAlgorithm::AES_GCM:
            case EncryptionAlgorithm::AES_CBC:
            case EncryptionAlgorithm::CHACHA20_POLY1305:
            case EncryptionAlgorithm::XCHACHA20_POLY1305:
                return true;
            default:
                return false;
        }
    }

    bool isValidKeyExchangeMethod(KeyExchangeMethod method) {
        switch(method) {
            case KeyExchangeMethod::NONE:
            case KeyExchangeMethod::RSA:
            case KeyExchangeMethod::DH:
            case KeyExchangeMethod::ECDH_P256:
            case KeyExchangeMethod::ECDH_P384:
            case KeyExchangeMethod::ECDH_P521:
            case KeyExchangeMethod::ECDH_X25519:
                return true;
            default:
                return false;
        }
    }

    bool isValidAuthenticationMethod(AuthenticationMethod method) {
        switch(method) {
            case AuthenticationMethod::NONE:
            case AuthenticationMethod::HMAC_SHA256:
            case AuthenticationMethod::HMAC_SHA384:
            case AuthenticationMethod::HMAC_SHA512:
            case AuthenticationMethod::RSA_PSS:
            case AuthenticationMethod::ECDSA_P256:
            case AuthenticationMethod::ECDSA_P384:
            case AuthenticationMethod::ED25519_SIGNATURE:
                return true;
            default:
                return false;
        }
    }

    bool isValidKeySize(KeySize size) {
        switch(size) {
            case KeySize::NONE:
            case KeySize::BITS_128:
            case KeySize::BITS_192:
            case KeySize::BITS_256:
            case KeySize::BITS_384:
            case KeySize::BITS_512:
                return true;
            default:
                return false;
        }
    }

    bool areCompatible(DataFormat format, CompressionAlgorithm algo) {
        // Certain combinations may not be compatible
        // For example, already compressed data might not benefit from additional compression
        if (format == DataFormat::COMPRESSED_STATE && algo != CompressionAlgorithm::NONE) {
            return false;
        }
        return true;
    }

    bool areCompatible(DataFormat format, ErrorCorrectionScheme scheme) {
        // Some data formats might have built-in error correction or be incompatible with certain schemes
        // For example, GGWAVE_FSK might include its own error correction
        if (format == DataFormat::GGWAVE_FSK && scheme != ErrorCorrectionScheme::NONE) {
            return false;
        }
        return true;
    }

    bool areCompatible(EncryptionAlgorithm algo, KeySize size) {
        switch(algo) {
            case EncryptionAlgorithm::NONE:
                return size == KeySize::NONE;
            case EncryptionAlgorithm::AES_GCM:
            case EncryptionAlgorithm::AES_CBC:
                return size == KeySize::BITS_128 || size == KeySize::BITS_192 || size == KeySize::BITS_256;
            case EncryptionAlgorithm::CHACHA20_POLY1305:
            case EncryptionAlgorithm::XCHACHA20_POLY1305:
                return size == KeySize::BITS_256;
            default:
                return false;
        }
    }

    bool areCompatible(KeyExchangeMethod method, EncryptionAlgorithm algo) {
        // If no encryption, only NONE key exchange is valid
        if (algo == EncryptionAlgorithm::NONE) {
            return method == KeyExchangeMethod::NONE;
        }
        
        // If using encryption, key exchange must be specified
        if (method == KeyExchangeMethod::NONE) {
            return algo == EncryptionAlgorithm::NONE;
        }

        // All other combinations are valid as long as both are specified
        return true;
    }

    ValidationResult validateSecurityParameters(const NegotiableParams& params) {
        // Check individual parameters first
        if (!isValidEncryptionAlgorithm(params.encryptionAlgorithm)) {
            return ValidationResult::INVALID_ENCRYPTION_ALGORITHM;
        }
        if (!isValidKeyExchangeMethod(params.keyExchangeMethod)) {
            return ValidationResult::INVALID_KEY_EXCHANGE_METHOD;
        }
        if (!isValidAuthenticationMethod(params.authenticationMethod)) {
            return ValidationResult::INVALID_AUTHENTICATION_METHOD;
        }
        if (!isValidKeySize(params.keySize)) {
            return ValidationResult::INVALID_KEY_SIZE;
        }

        // Check parameter compatibility
        if (!areCompatible(params.encryptionAlgorithm, params.keySize)) {
            return ValidationResult::INCOMPATIBLE_ENCRYPTION_KEY_SIZE;
        }
        if (!areCompatible(params.keyExchangeMethod, params.encryptionAlgorithm)) {
            return ValidationResult::INCOMPATIBLE_KEY_EXCHANGE_ENCRYPTION;
        }

        return ValidationResult::VALID;
    }

    ValidationResult validateParameterSet(const NegotiableParams& params) {
        // Check data handling parameters first
        if (!isValidDataFormat(params.dataFormat)) {
            return ValidationResult::INVALID_DATA_FORMAT;
        }
        if (!isValidCompressionAlgorithm(params.compressionAlgorithm)) {
            return ValidationResult::INVALID_COMPRESSION_ALGORITHM;
        }
        if (!isValidErrorCorrectionScheme(params.errorCorrection)) {
            return ValidationResult::INVALID_ERROR_CORRECTION_SCHEME;
        }

        // Check parameter compatibility
        if (!areCompatible(params.dataFormat, params.compressionAlgorithm)) {
            return ValidationResult::INCOMPATIBLE_FORMAT_COMPRESSION;
        }
        if (!areCompatible(params.dataFormat, params.errorCorrection)) {
            return ValidationResult::INCOMPATIBLE_FORMAT_ERROR_CORRECTION;
        }

        // Check security parameters
        auto securityResult = validateSecurityParameters(params);
        if (securityResult != ValidationResult::VALID) {
            return securityResult;
        }

        return ValidationResult::VALID;
    }

    std::string validationResultToString(ValidationResult result) {
        switch(result) {
            case ValidationResult::VALID:
                return "Parameters are valid";
            case ValidationResult::INVALID_DATA_FORMAT:
                return "Invalid data format";
            case ValidationResult::INVALID_COMPRESSION_ALGORITHM:
                return "Invalid compression algorithm";
            case ValidationResult::INVALID_ERROR_CORRECTION_SCHEME:
                return "Invalid error correction scheme";
            case ValidationResult::INCOMPATIBLE_FORMAT_COMPRESSION:
                return "Data format incompatible with compression algorithm";
            case ValidationResult::INCOMPATIBLE_FORMAT_ERROR_CORRECTION:
                return "Data format incompatible with error correction scheme";
            case ValidationResult::MISSING_REQUIRED_PARAMETER:
                return "Missing required parameter";
            case ValidationResult::INVALID_ENCRYPTION_ALGORITHM:
                return "Invalid encryption algorithm";
            case ValidationResult::INVALID_KEY_EXCHANGE_METHOD:
                return "Invalid key exchange method";
            case ValidationResult::INVALID_AUTHENTICATION_METHOD:
                return "Invalid authentication method";
            case ValidationResult::INVALID_KEY_SIZE:
                return "Invalid key size";
            case ValidationResult::INCOMPATIBLE_ENCRYPTION_KEY_SIZE:
                return "Encryption algorithm incompatible with key size";
            case ValidationResult::INCOMPATIBLE_KEY_EXCHANGE_ENCRYPTION:
                return "Key exchange method incompatible with encryption algorithm";
            default:
                return "Unknown validation error";
        }
    }
} // namespace validation

// --- Forward declarations ---
class StateTransitionValidator;

// --- Simple logging class (replace with proper logging system in production) ---
class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    void log(Level level, const std::string& message) const {
        if (level < minLevel_) return;

        const char* levelStr = "UNKNOWN";
        switch (level) {
            case Level::DEBUG: levelStr = "DEBUG"; break;
            case Level::INFO: levelStr = "INFO"; break;
            case Level::WARNING: levelStr = "WARNING"; break;
            case Level::ERROR: levelStr = "ERROR"; break;
        }

        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        std::cout << "[" << std::put_time(std::localtime(&now_time_t), "%F %T") << "] "
                  << "[" << levelStr << "] "
                  << message << std::endl;
    }

    void debug(const std::string& message) { log(Level::DEBUG, message); }
    void info(const std::string& message) { log(Level::INFO, message); }
    void warning(const std::string& message) const { log(Level::WARNING, message); }
    void error(const std::string& message) { log(Level::ERROR, message); }

    void setLevel(Level level) { minLevel_ = level; }

private:
    Level minLevel_ = Level::INFO;
};

// --- State Transition Validator ---
// This class enforces the state machine rules by validating state transitions
class StateTransitionValidator {
public:
    // Check if a transition from currentState to newState is valid
    static bool isValidTransition(NegotiationState currentState, NegotiationState newState) {
        const auto& validTransitions = getValidTransitions(currentState);
        return validTransitions.find(newState) != validTransitions.end();
    }

    // Get a string description of a state
    static std::string stateToString(NegotiationState state) {
        switch (state) {
            case NegotiationState::IDLE: return "IDLE";
            case NegotiationState::INITIATING: return "INITIATING";
            case NegotiationState::AWAITING_RESPONSE: return "AWAITING_RESPONSE";
            case NegotiationState::PROPOSAL_RECEIVED: return "PROPOSAL_RECEIVED";
            case NegotiationState::RESPONDING: return "RESPONDING";
            case NegotiationState::COUNTER_RECEIVED: return "COUNTER_RECEIVED";
            case NegotiationState::AWAITING_FINALIZATION: return "AWAITING_FINALIZATION";
            case NegotiationState::FINALIZING: return "FINALIZING";
            case NegotiationState::FINALIZED: return "FINALIZED";
            case NegotiationState::FAILED: return "FAILED";
            case NegotiationState::CLOSED: return "CLOSED";
            default: return "UNKNOWN";
        }
    }

private:
    // Get valid transitions for a given state
    static const std::unordered_set<NegotiationState>& getValidTransitions(NegotiationState state) {
        static const std::unordered_map<NegotiationState, std::unordered_set<NegotiationState>> validTransitions = {
            // From IDLE state
            {NegotiationState::IDLE, {
                NegotiationState::INITIATING,         // Local agent initiates
                NegotiationState::PROPOSAL_RECEIVED,  // Remote agent initiates
                NegotiationState::CLOSED              // Close without starting
            }},
            // From INITIATING state
            {NegotiationState::INITIATING, {
                NegotiationState::AWAITING_RESPONSE,  // Proposal sent successfully
                NegotiationState::FAILED,             // Failed to send proposal
                NegotiationState::CLOSED              // Closed during initiation
            }},
            // From AWAITING_RESPONSE state
            {NegotiationState::AWAITING_RESPONSE, {
                NegotiationState::COUNTER_RECEIVED,   // Received counter-proposal
                NegotiationState::FINALIZING,         // Received acceptance
                NegotiationState::FAILED,             // Received rejection or timeout
                NegotiationState::CLOSED              // Explicit close
            }},
            // From PROPOSAL_RECEIVED state
            {NegotiationState::PROPOSAL_RECEIVED, {
                NegotiationState::RESPONDING,         // Local agent is responding
                NegotiationState::FAILED,             // Local agent couldn't process
                NegotiationState::CLOSED              // Explicit close
            }},
            // From RESPONDING state
            {NegotiationState::RESPONDING, {
                NegotiationState::AWAITING_FINALIZATION,  // Response sent, waiting for finalization
                NegotiationState::FAILED,                 // Failed to send response
                NegotiationState::CLOSED                  // Explicit close
            }},
            // From COUNTER_RECEIVED state
            {NegotiationState::COUNTER_RECEIVED, {
                NegotiationState::FINALIZING,         // Local agent accepts counter
                NegotiationState::FAILED,             // Local agent rejects counter
                NegotiationState::CLOSED              // Explicit close
            }},
            // From AWAITING_FINALIZATION state
            {NegotiationState::AWAITING_FINALIZATION, {
                NegotiationState::FINALIZED,          // Received finalization
                NegotiationState::FAILED,             // Received rejection or timeout
                NegotiationState::CLOSED              // Explicit close
            }},
            // From FINALIZING state
            {NegotiationState::FINALIZING, {
                NegotiationState::FINALIZED,          // Finalization sent successfully
                NegotiationState::FAILED,             // Failed to send finalization
                NegotiationState::CLOSED              // Explicit close
            }},
            // Terminal states (can only transition to CLOSED)
            {NegotiationState::FINALIZED, {NegotiationState::CLOSED}},
            {NegotiationState::FAILED, {NegotiationState::CLOSED}},
            // From CLOSED (no valid transitions)
            {NegotiationState::CLOSED, {}}
        };

        static const std::unordered_set<NegotiationState> emptySet;
        auto it = validTransitions.find(state);
        return (it != validTransitions.end()) ? it->second : emptySet;
    }
};

// --- Message Types and Payloads (Moved definitions before usage) ---

enum class MessageType {
    PROPOSE,
    ACCEPT,
    COUNTER,
    REJECT,
    FINALIZE,
    CLOSE
};

struct ProposePayload { NegotiableParams params; };
struct AcceptPayload { std::optional<NegotiableParams> params; }; // Optional echo
struct CounterPayload { NegotiableParams params; };
struct RejectPayload { std::optional<std::string> reason; };
struct FinalizePayload { NegotiableParams params; };
struct ClosePayload { std::optional<std::string> reason; };

// --- Internal Message Representation (Placeholder) ---
// This simulates what the network layer might provide.
using MessagePayload = std::variant<std::monostate, ProposePayload, AcceptPayload, CounterPayload, RejectPayload, FinalizePayload, ClosePayload>;

// --- Negotiation Message Definition ---

struct NegotiationMessage {
    MessageType type;
    NegotiationProtocol::SessionId sessionId;
    uint32_t sequenceId;
    std::variant<
        std::monostate,
        ProposePayload,
        AcceptPayload,
        CounterPayload,
        RejectPayload,
        FinalizePayload,
        ClosePayload
    > payload;
    
    // Timestamp for timeout tracking
    std::chrono::steady_clock::time_point timestamp;
    
    // Constructor
    NegotiationMessage(MessageType t, NegotiationProtocol::SessionId sid, uint32_t seqId)
        : type(t), sessionId(sid), sequenceId(seqId), 
          timestamp(std::chrono::steady_clock::now()) {}
};

// --- Event Handler Types ---
// Define the types of event handlers that can be registered
using StateChangeHandler = std::function<void(NegotiationState, NegotiationState, const std::string&)>;

class ConcreteNegotiationProtocol : public NegotiationProtocol {
public:
    // Bring SessionId into class scope
    using SessionId = NegotiationProtocol::SessionId;

    // Constructor with optional parameters
    ConcreteNegotiationProtocol(bool enableLogging = true) 
        : nextSessionId_(1) {
        if (enableLogging) {
            logger_.setLevel(Logger::Level::INFO);
        } else {
            logger_.setLevel(Logger::Level::ERROR);
        }
        logger_.info("NegotiationProtocol initialized");
    }
    
    ~ConcreteNegotiationProtocol() noexcept override = default;

    // Implementation of public API
    SessionId initiateSession(const std::string& targetAgentId, const NegotiableParams& proposedParams) override;
    bool respondToNegotiation(SessionId sessionId, NegotiationResponse responseType, const std::optional<NegotiableParams>& responseParams) override;
    NegotiableParams finalizeSession(SessionId sessionId) override;
    NegotiationState getSessionState(SessionId sessionId) const override;
    std::optional<NegotiableParams> getNegotiatedParams(SessionId sessionId) const override;
    bool acceptCounterProposal(SessionId sessionId) override;
    bool closeSession(SessionId sessionId) override;
    bool rejectCounterProposal(SessionId sessionId, const std::optional<std::string>& reason) override;

    // Event registration methods
    void registerStateChangeHandler(StateChangeHandler handler) {
        stateChangeHandlers_.push_back(std::move(handler));
    }

    /**
     * @brief Automatically process a proposal based on the local preferences
     * 
     * @param sessionId The ID of the session with a received proposal
     * @param preferences The local preferences to use for evaluation
     * @return true if the proposal was processed successfully (accepted, counter, or rejected)
     * @return false if there was an error processing the proposal
     */
    bool autoProcessProposal(SessionId sessionId, const ParameterPreference& preferences);

private:
    // Member variables
    mutable std::mutex sessionsMutex_;
    std::unordered_map<NegotiationProtocol::SessionId, NegotiationSessionData> sessions_;
    std::atomic<NegotiationProtocol::SessionId> nextSessionId_;
    Logger logger_;
    std::vector<StateChangeHandler> stateChangeHandlers_;

    // --- Private helper method Declarations --- 
    NegotiationSessionData& getSessionData(NegotiationProtocol::SessionId sessionId);
    const NegotiationSessionData& getSessionData(NegotiationProtocol::SessionId sessionId) const;
    bool transitionState(NegotiationProtocol::SessionId sessionId, NegotiationState newState, const std::string& reason = "");
    bool isStateTimedOut(const NegotiationSessionData& session, std::chrono::milliseconds timeout = DEFAULT_NEGOTIATION_TIMEOUT) const;
    bool sendProposal(const std::string& targetAgentId, NegotiationProtocol::SessionId sessionId, const NegotiableParams& params);
    bool sendResponse(NegotiationProtocol::SessionId sessionId, NegotiationResponse responseType, const std::optional<NegotiableParams>& params);
    bool sendFinalization(NegotiationProtocol::SessionId sessionId, const NegotiableParams& finalParams);
    bool sendReject(NegotiationProtocol::SessionId sessionId, const std::optional<std::string>& reason);
    void handleIncomingMessage(NegotiationProtocol::SessionId sessionId, MessageType type, const MessagePayload& payload);
    NegotiableParams createProposal(const ParameterPreference& preferences);
    std::optional<NegotiableParams> createCounterProposal(
        const NegotiableParams& receivedParams,
        const ParameterPreference& preferences,
        NegotiationSessionData& session
    );
    NegotiationResponse evaluateProposal(
        const NegotiableParams& proposedParams,
        const ParameterPreference& preferences,
        NegotiationSessionData& session
    );
    bool handleProposal(NegotiationProtocol::SessionId sessionId, const NegotiableParams& proposedParams);
    bool acceptProposal(NegotiationProtocol::SessionId sessionId, const std::optional<NegotiableParams>& finalParams);
    bool rejectProposal(NegotiationProtocol::SessionId sessionId, const std::optional<std::string>& reason);

}; // End of ConcreteNegotiationProtocol class definition

// --- Definitions of PUBLIC methods --- 
// (These remain outside the class definition)
NegotiationProtocol::SessionId ConcreteNegotiationProtocol::initiateSession(const std::string& targetAgentId, const NegotiableParams& proposedParams) {
    // Validate params before proceeding
    auto validationResult = validation::validateParameterSet(proposedParams);
    if (validationResult != validation::ValidationResult::VALID) {
        throw std::runtime_error("Invalid negotiation parameters: " + 
                                validation::validationResultToString(validationResult));
    }
    
    SessionId sessionId = nextSessionId_.fetch_add(1); // Use class SessionId alias
    
    // Create and initialize session data (Use nested struct name)
    NegotiationSessionData sessionData;
    sessionData.initiatorAgentId = ""; // TODO: Get current agent ID
    sessionData.targetAgentId = targetAgentId;
    sessionData.initialProposal = proposedParams;
    
    // Add to sessions map
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_[sessionId] = std::move(sessionData);
    }
    
    // Transition to INITIATING state
    if (!transitionState(sessionId, NegotiationState::INITIATING, "Initiating negotiation with " + targetAgentId)) {
        throw std::runtime_error("Failed to transition to INITIATING state");
    }

    // Send the proposal over the network (MSG_PROPOSE)
    bool sentOk = sendProposal(targetAgentId, sessionId, proposedParams);

    if (sentOk) {
        // Transition to AWAITING_RESPONSE state
        transitionState(sessionId, NegotiationState::AWAITING_RESPONSE, "Proposal sent, awaiting response");
    } else {
        // Transition to FAILED state
        transitionState(sessionId, NegotiationState::FAILED, "Failed to send proposal");
        throw std::runtime_error("Failed to send negotiation proposal to target agent: " + targetAgentId);
    }

    return sessionId;
}

bool ConcreteNegotiationProtocol::respondToNegotiation(SessionId sessionId, NegotiationResponse responseType, const std::optional<NegotiableParams>& responseParams) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId); // Throws if not found

    // Validate state - Only Responder in PROPOSAL_RECEIVED state can call this
    if (session.state != NegotiationState::PROPOSAL_RECEIVED) {
        logger_.error("Cannot respond to negotiation: Invalid state " + 
                     StateTransitionValidator::stateToString(session.state) + 
                     ", expected PROPOSAL_RECEIVED");
        return false; 
    }

    // Validate response parameters if provided
    if (responseParams && responseType == NegotiationResponse::COUNTER_PROPOSAL) {
        auto validationResult = validation::validateParameterSet(*responseParams);
        if (validationResult != validation::ValidationResult::VALID) {
            logger_.error("Invalid counter-proposal parameters: " + 
                         validation::validationResultToString(validationResult));
            return false;
        }
    }

    // Transition to RESPONDING state
    if (!transitionState(sessionId, NegotiationState::RESPONDING, "Preparing response")) {
        return false;
    }

    // Send the response over the network (MSG_ACCEPT, MSG_COUNTER, or MSG_REJECT)
    bool sentOk = sendResponse(sessionId, responseType, responseParams);
    
    if (sentOk) {
        switch (responseType) {
            case NegotiationResponse::ACCEPTED:
                // Record the final parameters
                session.finalParams = responseParams.value_or(session.initialProposal);
                // Transition to AWAITING_FINALIZATION state
                return transitionState(sessionId, NegotiationState::AWAITING_FINALIZATION, 
                                    "Acceptance sent, awaiting finalization");
            
            case NegotiationResponse::COUNTER_PROPOSAL:
                if (!responseParams) {
                    logger_.error("Counter proposal requires parameters");
                    transitionState(sessionId, NegotiationState::FAILED, "Invalid counter-proposal: missing parameters");
                    return false;
                }
                session.counterProposal = *responseParams;
                // Transition to AWAITING_FINALIZATION state
                return transitionState(sessionId, NegotiationState::AWAITING_FINALIZATION, 
                                    "Counter-proposal sent, awaiting response");
            
            case NegotiationResponse::REJECTED:
                // Transition to FAILED state
                return transitionState(sessionId, NegotiationState::FAILED, "Proposal rejected by local agent");
            
            default:
                logger_.error("Unknown response type");
                return false;
        }
    } else {
        // Failed to send response
        transitionState(sessionId, NegotiationState::FAILED, "Failed to send response");
        return false;
    }
}

bool ConcreteNegotiationProtocol::acceptCounterProposal(SessionId sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId);

    // Validate state - Only Initiator in COUNTER_RECEIVED state can call this
    if (session.state != NegotiationState::COUNTER_RECEIVED) {
        logger_.error("Cannot accept counter-proposal: Invalid state " + 
                     StateTransitionValidator::stateToString(session.state) + 
                     ", expected COUNTER_RECEIVED");
        return false;
    }

    if (!session.counterProposal.has_value()) {
        // Should not happen if state is COUNTER_RECEIVED, but check anyway
        logger_.error("Cannot accept counter-proposal: No counter-proposal parameters available");
        return transitionState(sessionId, NegotiationState::FAILED, "No counter-proposal available");
    }

    // Mark the counter-proposal parameters as the final ones
    session.finalParams = session.counterProposal;
    
    // Transition state - ready to finalize based on the accepted counter
    return transitionState(sessionId, NegotiationState::FINALIZING, "Counter-proposal accepted, ready to finalize");
}

NegotiableParams ConcreteNegotiationProtocol::finalizeSession(SessionId sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId); // Throws if not found

    // Enhanced state validation with detailed error messages
    if (session.state != NegotiationState::FINALIZING) {
        std::string errorMsg = "Cannot finalize session: Invalid state " + 
                             StateTransitionValidator::stateToString(session.state) + 
                             ", expected FINALIZING";
        logger_.error(errorMsg);
        throw std::runtime_error(errorMsg);
    }
    
    // Ensure we have final parameters decided
    if (!session.finalParams.has_value()) {
        std::string errorMsg = "Cannot finalize session: No agreement reached";
        logger_.error(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Additional validation of final parameters
    auto validationResult = validation::validateParameterSet(*session.finalParams);
    if (validationResult != validation::ValidationResult::VALID) {
        std::string errorMsg = "Cannot finalize session: Invalid final parameters - " + 
                             validation::validationResultToString(validationResult);
        logger_.error(errorMsg);
        throw std::runtime_error(errorMsg);
    }

    // Send the final confirmation message (MSG_FINALIZE) with the agreed parameters
    bool sentOk = sendFinalization(sessionId, *session.finalParams);

    if (sentOk) {
        if (transitionState(sessionId, NegotiationState::FINALIZED, "Negotiation successful")) {
            // Log successful finalization with parameter details
            logger_.info("Session " + std::to_string(sessionId) + " finalized successfully with parameters: " +
                        "DataFormat=" + std::to_string(static_cast<int>(session.finalParams->dataFormat)) + ", " +
                        "CompressionAlgorithm=" + std::to_string(static_cast<int>(session.finalParams->compressionAlgorithm)) + ", " +
                        "ErrorCorrection=" + std::to_string(static_cast<int>(session.finalParams->errorCorrection)) + ", " +
                        "EncryptionAlgorithm=" + std::to_string(static_cast<int>(session.finalParams->encryptionAlgorithm)) + ", " +
                        "KeyExchangeMethod=" + std::to_string(static_cast<int>(session.finalParams->keyExchangeMethod)) + ", " +
                        "AuthenticationMethod=" + std::to_string(static_cast<int>(session.finalParams->authenticationMethod)) + ", " +
                        "KeySize=" + std::to_string(static_cast<int>(session.finalParams->keySize)));

            // Clean up any temporary negotiation data while preserving final parameters
            session.counterProposal = std::nullopt;
            session.triedProposals.clear();
            session.fallbackAttempts = 0;

            // Return the finalized parameters
            return *session.finalParams;
        } else {
            // This should not happen if state transition validation is consistent
            std::string errorMsg = "Failed to transition to FINALIZED state despite successful send";
            logger_.error(errorMsg);
            throw std::runtime_error("Failed to finalize session: State transition error");
        }
    } else {
        std::string errorMsg = "Failed to send finalization message";
        logger_.error(errorMsg + " for session " + std::to_string(sessionId));
        transitionState(sessionId, NegotiationState::FAILED, errorMsg);
        transitionState(sessionId, NegotiationState::FAILED, "Failed to send finalization"); // Duplicate transition?
        throw std::runtime_error("Failed to send finalization message for session");
    }
}

NegotiationState ConcreteNegotiationProtocol::getSessionState(SessionId sessionId) const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    const auto& session = getSessionData(sessionId); // Throws if not found
    
    // Check for timeout in non-terminal states
    if (session.state != NegotiationState::FINALIZED && 
        session.state != NegotiationState::FAILED && 
        session.state != NegotiationState::CLOSED &&
        isStateTimedOut(session)) {
        // Can't transition state in a const method, but we can log the timeout
        logger_.warning("Session " + std::to_string(sessionId) + " has timed out in state " + 
                       StateTransitionValidator::stateToString(session.state));
    }
    
    return session.state;
}

std::optional<NegotiableParams> ConcreteNegotiationProtocol::getNegotiatedParams(SessionId sessionId) const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    const auto& session = getSessionData(sessionId); // Throws if not found
    
    // Only return params if the negotiation was successful
    if (session.state == NegotiationState::FINALIZED) {
        return session.finalParams;
    }
    
    return std::nullopt;
}

bool ConcreteNegotiationProtocol::closeSession(SessionId sessionId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId); // Throws if not found
    
    // Can't close a session that's already closed
    if (session.state == NegotiationState::CLOSED) {
        logger_.warning("Session " + std::to_string(sessionId) + " is already closed");
        return true; // Already in desired state
    }
    
    // Try to send CLOSE message if session is not in a terminal state
    if (session.state != NegotiationState::FINALIZED && 
        session.state != NegotiationState::FAILED) {
        // Optional: send CLOSE message to peer
        // For simplicity we're not checking the send result here, as we're closing anyway
        sendReject(sessionId, std::optional<std::string>("Session closed by local agent"));
    }
    
    // Transition to CLOSED state
    return transitionState(sessionId, NegotiationState::CLOSED, "Session closed by local agent");
}

bool ConcreteNegotiationProtocol::rejectCounterProposal(SessionId sessionId, const std::optional<std::string>& reason) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId);

    // Validate state - Only Initiator in COUNTER_RECEIVED state can call this
    if (session.state != NegotiationState::COUNTER_RECEIVED) {
        logger_.error("Cannot reject counter-proposal: Invalid state " + 
                     StateTransitionValidator::stateToString(session.state) + 
                     ", expected COUNTER_RECEIVED");
        return false;
    }

    // Attempt to send the REJECT message to the peer
    bool sentOk = sendReject(sessionId, reason);
    
    // Standard reason if none provided
    std::string rejectionReason = reason.value_or("Counter-proposal rejected");

    // Update state regardless of send success
    if (transitionState(sessionId, NegotiationState::FAILED, rejectionReason)) {
        if (!sentOk) {
            logger_.warning("Failed to send REJECT message, but session marked as FAILED locally");
        }
        return true;
    } else {
        // This should never happen if our state transition validation logic is correct
        logger_.error("Failed to transition to FAILED state when rejecting counter-proposal");
        return false;
    }
}

// --- Definitions of PRIVATE helper methods --- 
// (Must be qualified with class name)

NegotiationSessionData& ConcreteNegotiationProtocol::getSessionData(NegotiationProtocol::SessionId sessionId) {
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        throw std::runtime_error("Invalid or unknown session ID: " + std::to_string(sessionId));
    }
    return it->second;
}

const NegotiationSessionData& ConcreteNegotiationProtocol::getSessionData(NegotiationProtocol::SessionId sessionId) const {
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        throw std::runtime_error("Invalid or unknown session ID: " + std::to_string(sessionId));
    }
    return it->second;
}

bool ConcreteNegotiationProtocol::transitionState(NegotiationProtocol::SessionId sessionId, NegotiationState newState, const std::string& reason) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId); // Calls the member function getSessionData
    NegotiationState currentState = session.state;
    
    if (!StateTransitionValidator::isValidTransition(currentState, newState)) {
        logger_.error("Invalid state transition: " + 
                     StateTransitionValidator::stateToString(currentState) + " -> " + 
                     StateTransitionValidator::stateToString(newState) + 
                     (reason.empty() ? "" : " (" + reason + ")"));
        return false;
    }
    
    session.state = newState;
    session.stateTimestamps[newState] = std::chrono::steady_clock::now();
    if (currentState != newState) {
        session.retryCount = 0;
    }
    
    logger_.info("Session " + std::to_string(sessionId) + " state transition: " + 
                StateTransitionValidator::stateToString(currentState) + " -> " + 
                StateTransitionValidator::stateToString(newState) + 
                (reason.empty() ? "" : " (" + reason + ")"));
    
    for (const auto& handler : stateChangeHandlers_) {
        try {
            handler(currentState, newState, reason);
        } catch (const std::exception& e) {
            logger_.error("Exception in state change handler: " + std::string(e.what()));
        }
    }
    return true;
}

bool ConcreteNegotiationProtocol::isStateTimedOut(const NegotiationSessionData& session, std::chrono::milliseconds timeout) const {
    auto currentState = session.state;
    auto it = session.stateTimestamps.find(currentState);
    if (it == session.stateTimestamps.end()) {
        return (std::chrono::steady_clock::now() - session.createdTime) > timeout;
    }
    return (std::chrono::steady_clock::now() - it->second) > timeout;
}

bool ConcreteNegotiationProtocol::sendProposal(const std::string& targetAgentId, NegotiationProtocol::SessionId sessionId, const NegotiableParams& params) {
    (void)targetAgentId; (void)sessionId; (void)params;
    return true; // Placeholder
}

bool ConcreteNegotiationProtocol::sendResponse(NegotiationProtocol::SessionId sessionId, NegotiationResponse responseType, const std::optional<NegotiableParams>& params) {
    (void)sessionId; (void)responseType; (void)params;
    return true; // Placeholder
}

bool ConcreteNegotiationProtocol::sendFinalization(NegotiationProtocol::SessionId sessionId, const NegotiableParams& finalParams) {
    (void)sessionId; (void)finalParams;
    return true; // Placeholder
}

bool ConcreteNegotiationProtocol::sendReject(NegotiationProtocol::SessionId sessionId, const std::optional<std::string>& reason) {
    (void)sessionId; (void)reason;
    return true; // Placeholder
}

void ConcreteNegotiationProtocol::handleIncomingMessage(NegotiationProtocol::SessionId sessionId, MessageType type, const MessagePayload& payload) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    try {
        auto& session = getSessionData(sessionId);
        std::string messageTypeStr;
        switch (type) {
            case MessageType::PROPOSE: messageTypeStr = "PROPOSE"; break;
            case MessageType::ACCEPT: messageTypeStr = "ACCEPT"; break;
            case MessageType::COUNTER: messageTypeStr = "COUNTER"; break;
            case MessageType::REJECT: messageTypeStr = "REJECT"; break;
            case MessageType::FINALIZE: messageTypeStr = "FINALIZE"; break;
            case MessageType::CLOSE: messageTypeStr = "CLOSE"; break;
            default: messageTypeStr = "UNKNOWN"; break;
        }

        logger_.info("Received " + messageTypeStr + " message for session " + std::to_string(sessionId) + 
                    " in state " + StateTransitionValidator::stateToString(session.state));

        // Process message based on current state and message type
        switch (session.state) {
            // ... cases for different states ...
        }
    } catch (const std::runtime_error& e) {
        logger_.error("Runtime error handling message for session " + std::to_string(sessionId) + ": " + e.what());
        // Attempt to mark session as FAILED
    } catch (const std::bad_variant_access& e) {
        logger_.error("Bad variant access for session " + std::to_string(sessionId) + ": " + e.what());
        // Attempt to mark session as FAILED
    }
}

NegotiableParams ConcreteNegotiationProtocol::createProposal(const ParameterPreference& preferences) {
    try {
        return preferences.createOptimalParameters();
    } catch (const std::runtime_error& e) {
        logger_.warning("Failed to create optimal parameters: " + std::string(e.what()));
        NegotiableParams fallback;
        // ... set fallback parameters ...
        return fallback;
    }
}

std::optional<NegotiableParams> ConcreteNegotiationProtocol::createCounterProposal(
    const NegotiableParams& receivedParams,
    const ParameterPreference& preferences,
    NegotiationSessionData& session) 
{
    // Note: The check for remoteFormats.empty() and storing capabilities
    // should likely happen *before* this function is called, e.g., 
    // when the initial proposal is received and evaluated.
    // if (session.remoteFormats.empty()) { ... }

    try {
        // Call with all required arguments from the session
        return preferences.buildCompatibleParamsWithFallbacks(
            session.remoteFormats,
            session.remoteCompression,
            session.remoteErrorCorrection,
            session.remoteEncryption,
            session.remoteKeyExchange,
            session.remoteAuth,
            session.remoteKeySizes
        );
    } catch (const std::runtime_error& e) {
        logger_.warning("Failed to create counter-proposal: " + std::string(e.what()));
        return std::nullopt;
    }
}

NegotiationResponse ConcreteNegotiationProtocol::evaluateProposal(
    const NegotiableParams& proposedParams,
    const ParameterPreference& preferences,
    NegotiationSessionData& session)
{
    // Store remote capabilities
    // Check if proposal already tried
    // Check compatibility
    // Check fallback attempts
    return NegotiationResponse::REJECTED; // Placeholder return
}

bool ConcreteNegotiationProtocol::handleProposal(NegotiationProtocol::SessionId sessionId, const NegotiableParams& proposedParams) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId);
    ParameterPreference preferences; // Placeholder
    auto response = evaluateProposal(proposedParams, preferences, session);
    // ... switch statement based on response ...
    return false; // Placeholder return
}

bool ConcreteNegotiationProtocol::acceptProposal(NegotiationProtocol::SessionId sessionId, const std::optional<NegotiableParams>& finalParams) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId);
    // ... state validation, send response, transition state ...
    return false; // Placeholder return
}

bool ConcreteNegotiationProtocol::rejectProposal(NegotiationProtocol::SessionId sessionId, const std::optional<std::string>& reason) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId);
    // ... state validation, send response, transition state ...
    return false; // Placeholder return
}

// --- Definitions for ParameterPreference methods (declared in header) ---

// Placeholder definition - Replace with actual logic
NegotiableParams ParameterPreference::createOptimalParameters() const {
    // TODO: Implement logic to select the highest-ranked options
    // from the preference lists (dataFormats, compressionAlgorithms, etc.)
    // respecting compatibility rules.
    // Throw std::runtime_error if no valid set can be formed based on requirements.
    
    NegotiableParams optimalParams;
    // Example: Assume the first ranked option is the most preferred
    if (!dataFormats.empty()) optimalParams.dataFormat = dataFormats[0].value;
    if (!compressionAlgorithms.empty()) optimalParams.compressionAlgorithm = compressionAlgorithms[0].value;
    if (!errorCorrectionSchemes.empty()) optimalParams.errorCorrection = errorCorrectionSchemes[0].value;
    // Add logic for security params if needed

    // Validate the created set (optional but recommended)
    // if (!validateSecurityParameters(optimalParams)) { 
    //    throw std::runtime_error("Could not create valid optimal security parameters"); 
    // }

    return optimalParams; 
}

// Placeholder definition - Replace with actual logic
NegotiableParams ParameterPreference::buildCompatibleParamsWithFallbacks(
    [[maybe_unused]] const std::vector<DataFormat>& remoteFormats,
    [[maybe_unused]] const std::vector<CompressionAlgorithm>& remoteCompression,
    [[maybe_unused]] const std::vector<ErrorCorrectionScheme>& remoteErrorCorrection,
    [[maybe_unused]] const std::vector<EncryptionAlgorithm>& remoteEncryption,
    [[maybe_unused]] const std::vector<KeyExchangeMethod>& remoteKeyExchange,
    [[maybe_unused]] const std::vector<AuthenticationMethod>& remoteAuth,
    [[maybe_unused]] const std::vector<KeySize>& remoteKeySizes) const 
{
    // TODO: Implement logic to find the best intersection between local preferences
    // (including fallbacks) and remote capabilities. Prioritize higher-ranked local options.
    // Start with optimal, then iterate through fallbacks if no match found.
    
    NegotiableParams compatibleParams = createOptimalParameters(); // Start with local optimal
    
    // Example simplified logic: Just return the locally optimal for now
    // A real implementation would involve matching against remote capabilities.

    return compatibleParams;
}

// Factory function remains outside
std::unique_ptr<NegotiationProtocol> createNegotiationProtocol(bool enableLogging) {
    return std::make_unique<ConcreteNegotiationProtocol>(enableLogging);
}

} // namespace core
} // namespace xenocomm 
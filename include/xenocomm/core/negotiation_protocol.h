#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace xenocomm {
namespace core {

/**
 * @brief Defines possible data formats for communication sessions.
 */
enum class DataFormat {
    VECTOR_FLOAT32,
    VECTOR_INT8,
    COMPRESSED_STATE,
    BINARY_CUSTOM,
    GGWAVE_FSK, // Example of a specific modulation scheme
    // Add more formats as needed
};

/**
 * @brief Defines possible compression algorithms.
 */
enum class CompressionAlgorithm {
    NONE,
    ZLIB,
    LZ4,
    ZSTD,
    // Add more algorithms as needed
};

/**
 * @brief Defines possible error correction schemes.
 */
enum class ErrorCorrectionScheme {
    NONE,
    CHECKSUM_ONLY,
    REED_SOLOMON,
    // Add more schemes as needed
};

/**
 * @brief Defines supported encryption algorithms.
 */
enum class EncryptionAlgorithm {
    NONE,
    AES_GCM,     // AES in Galois/Counter Mode
    AES_CBC,     // AES in Cipher Block Chaining mode
    CHACHA20_POLY1305,
    XCHACHA20_POLY1305
};

/**
 * @brief Defines supported key exchange methods.
 */
enum class KeyExchangeMethod {
    NONE,
    RSA,
    DH,          // Classic Diffie-Hellman
    ECDH_P256,   // Elliptic Curve DH with NIST P-256
    ECDH_P384,   // Elliptic Curve DH with NIST P-384
    ECDH_X25519  // Curve25519-based ECDH
};

/**
 * @brief Defines supported authentication methods.
 */
enum class AuthenticationMethod {
    NONE,
    HMAC_SHA256,
    HMAC_SHA512,
    ED25519_SIGNATURE,
    RSA_SIGNATURE
};

/**
 * @brief Defines supported key sizes.
 */
enum class KeySize {
    BITS_128,
    BITS_192,
    BITS_256,
    BITS_384,
    BITS_512
};

/**
 * @brief Represents the parameters that can be negotiated for a communication session.
 */
struct NegotiableParams {
    std::string protocolVersion = "1.0.0"; // Semantic versioning
    std::string securityVersion = "1.0.0"; // Security protocol versioning
    
    // Data handling parameters
    DataFormat dataFormat = DataFormat::BINARY_CUSTOM;
    CompressionAlgorithm compressionAlgorithm = CompressionAlgorithm::NONE;
    ErrorCorrectionScheme errorCorrection = ErrorCorrectionScheme::NONE;
    
    // Security parameters
    EncryptionAlgorithm encryptionAlgorithm = EncryptionAlgorithm::NONE;
    KeyExchangeMethod keyExchangeMethod = KeyExchangeMethod::NONE;
    AuthenticationMethod authenticationMethod = AuthenticationMethod::NONE;
    KeySize keySize = KeySize::BITS_256;
    
    std::map<std::string, std::string> customParameters; // For protocol-specific extensions

    bool operator==(const NegotiableParams& other) const {
        return protocolVersion == other.protocolVersion &&
               securityVersion == other.securityVersion &&
               dataFormat == other.dataFormat &&
               compressionAlgorithm == other.compressionAlgorithm &&
               errorCorrection == other.errorCorrection &&
               encryptionAlgorithm == other.encryptionAlgorithm &&
               keyExchangeMethod == other.keyExchangeMethod &&
               authenticationMethod == other.authenticationMethod &&
               keySize == other.keySize &&
               customParameters == other.customParameters;
    }
    
    bool operator!=(const NegotiableParams& other) const { 
        return !(*this == other); 
    }
};

/**
 * @brief Represents a ranked option with fallback alternatives for parameter negotiation.
 * 
 * @tparam T The type of the parameter value (e.g., DataFormat, CompressionAlgorithm)
 */
template <typename T>
struct RankedOption {
    T value;                     ///< The parameter value
    uint8_t rank;               ///< Preference rank (lower is more preferred)
    bool required;              ///< If true, negotiation fails if this can't be satisfied
    std::vector<T> fallbacks;   ///< Ordered list of fallback options

    RankedOption(T v, uint8_t r, bool req = false) 
        : value(v), rank(r), required(req) {}

    RankedOption(T v, uint8_t r, bool req, std::vector<T> fb) 
        : value(v), rank(r), required(req), fallbacks(std::move(fb)) {}

    bool operator<(const RankedOption& other) const {
        return rank < other.rank;
    }
};

/**
 * @brief Manages parameter preferences and fallback strategies during negotiation.
 */
struct ParameterPreference {
    std::vector<RankedOption<DataFormat>> dataFormats;
    std::vector<RankedOption<CompressionAlgorithm>> compressionAlgorithms;
    std::vector<RankedOption<ErrorCorrectionScheme>> errorCorrectionSchemes;
    
    // Security parameter preferences
    std::vector<RankedOption<EncryptionAlgorithm>> encryptionAlgorithms;
    std::vector<RankedOption<KeyExchangeMethod>> keyExchangeMethods;
    std::vector<RankedOption<AuthenticationMethod>> authenticationMethods;
    std::vector<RankedOption<KeySize>> keySizes;
    
    std::map<std::string, std::vector<RankedOption<std::string>>> customParameters;

    /**
     * @brief Validates security parameter compatibility.
     * 
     * @param params The parameters to validate
     * @return true if parameters are compatible, false otherwise
     */
    bool validateSecurityParameters(const NegotiableParams& params) const {
        // Validate encryption and key size compatibility
        if (params.encryptionAlgorithm != EncryptionAlgorithm::NONE) {
            // Require key exchange if encryption is enabled
            if (params.keyExchangeMethod == KeyExchangeMethod::NONE) {
                return false;
            }

            // Validate key sizes for AES
            if ((params.encryptionAlgorithm == EncryptionAlgorithm::AES_GCM || 
                 params.encryptionAlgorithm == EncryptionAlgorithm::AES_CBC) &&
                (params.keySize != KeySize::BITS_128 && 
                 params.keySize != KeySize::BITS_192 && 
                 params.keySize != KeySize::BITS_256)) {
                return false;
            }

            // ChaCha20 variants require 256-bit keys
            if ((params.encryptionAlgorithm == EncryptionAlgorithm::CHACHA20_POLY1305 || 
                 params.encryptionAlgorithm == EncryptionAlgorithm::XCHACHA20_POLY1305) &&
                params.keySize != KeySize::BITS_256) {
                return false;
            }
        } else {
            // If no encryption, key exchange and key size should be NONE/default
            if (params.keyExchangeMethod != KeyExchangeMethod::NONE) {
                return false;
            }
        }

        // Validate key exchange method compatibility
        if (params.keyExchangeMethod != KeyExchangeMethod::NONE) {
            // RSA key exchange requires larger key sizes
            if (params.keyExchangeMethod == KeyExchangeMethod::RSA &&
                params.keySize < KeySize::BITS_256) {
                return false;
            }

            // ECDH curves have specific key size requirements
            if (params.keyExchangeMethod == KeyExchangeMethod::ECDH_P256 &&
                params.keySize != KeySize::BITS_256) {
                return false;
            }
            if (params.keyExchangeMethod == KeyExchangeMethod::ECDH_P384 &&
                params.keySize != KeySize::BITS_384) {
                return false;
            }
            if (params.keyExchangeMethod == KeyExchangeMethod::ECDH_X25519 &&
                params.keySize != KeySize::BITS_256) {
                return false;
            }
        }

        return true;
    }

    /**
     * @brief Finds the best matching parameter value considering fallbacks.
     * 
     * @tparam T The parameter type
     * @param local Local ranked options with fallbacks
     * @param remote Available options from remote peer
     * @return The best matching value or nullopt if no match found
     */
    template <typename T>
    std::optional<T> findBestMatchWithFallbacks(
        const std::vector<RankedOption<T>>& local,
        const std::vector<T>& remote) const;

    /**
     * @brief Generates alternative parameter sets when initial proposal is rejected.
     */
    std::vector<NegotiableParams> generateAlternativeProposals(
        const NegotiableParams& rejectedProposal,
        const std::vector<DataFormat>& remoteFormats,
        const std::vector<CompressionAlgorithm>& remoteCompression,
        const std::vector<ErrorCorrectionScheme>& remoteErrorCorrection,
        const std::vector<EncryptionAlgorithm>& remoteEncryption,
        const std::vector<KeyExchangeMethod>& remoteKeyExchange,
        const std::vector<AuthenticationMethod>& remoteAuth,
        const std::vector<KeySize>& remoteKeySizes,
        size_t maxAlternatives = 3) const;

    /**
     * @brief Creates optimal parameters based on local preferences.
     * 
     * @return The optimal parameter set
     * @throws std::runtime_error if no valid parameter set can be created
     */
    NegotiableParams createOptimalParameters() const;

    /**
     * @brief Builds compatible parameters considering remote capabilities and local fallbacks.
     */
    NegotiableParams buildCompatibleParamsWithFallbacks(
        const std::vector<DataFormat>& remoteFormats,
        const std::vector<CompressionAlgorithm>& remoteCompression,
        const std::vector<ErrorCorrectionScheme>& remoteErrorCorrection,
        const std::vector<EncryptionAlgorithm>& remoteEncryption,
        const std::vector<KeyExchangeMethod>& remoteKeyExchange,
        const std::vector<AuthenticationMethod>& remoteAuth,
        const std::vector<KeySize>& remoteKeySizes) const;

    /**
     * @brief Checks if a proposal meets our requirements.
     * 
     * @param proposal The parameter set to check
     * @return true if compatible with requirements
     */
    bool isCompatibleWithRequirements(const NegotiableParams& proposal) const {
        // Check basic parameter compatibility
        bool basicCompatible = std::any_of(dataFormats.begin(), dataFormats.end(),
            [&](const auto& opt) { return opt.value == proposal.dataFormat; }) &&
            std::any_of(compressionAlgorithms.begin(), compressionAlgorithms.end(),
                [&](const auto& opt) { return opt.value == proposal.compressionAlgorithm; }) &&
            std::any_of(errorCorrectionSchemes.begin(), errorCorrectionSchemes.end(),
                [&](const auto& opt) { return opt.value == proposal.errorCorrection; });

        if (!basicCompatible) {
            return false;
        }

        // Check security parameter compatibility
        bool securityCompatible = std::any_of(encryptionAlgorithms.begin(), encryptionAlgorithms.end(),
            [&](const auto& opt) { return opt.value == proposal.encryptionAlgorithm; }) &&
            std::any_of(keyExchangeMethods.begin(), keyExchangeMethods.end(),
                [&](const auto& opt) { return opt.value == proposal.keyExchangeMethod; }) &&
            std::any_of(authenticationMethods.begin(), authenticationMethods.end(),
                [&](const auto& opt) { return opt.value == proposal.authenticationMethod; }) &&
            std::any_of(keySizes.begin(), keySizes.end(),
                [&](const auto& opt) { return opt.value == proposal.keySize; });

        if (!securityCompatible) {
            return false;
        }

        // Validate security parameter combinations
        return validateSecurityParameters(proposal);
    }

    /**
     * @brief Calculates how well a proposal matches our preferences.
     * 
     * @param proposal The parameter set to score
     * @return Score (lower is better)
     */
    uint32_t calculateCompatibilityScore(const NegotiableParams& proposal) const {
        uint32_t score = 0;

        // Score basic parameters
        for (const auto& opt : dataFormats) {
            if (opt.value == proposal.dataFormat) {
                score += opt.rank;
                break;
            }
        }
        for (const auto& opt : compressionAlgorithms) {
            if (opt.value == proposal.compressionAlgorithm) {
                score += opt.rank;
                break;
            }
        }
        for (const auto& opt : errorCorrectionSchemes) {
            if (opt.value == proposal.errorCorrection) {
                score += opt.rank;
                break;
            }
        }

        // Score security parameters
        for (const auto& opt : encryptionAlgorithms) {
            if (opt.value == proposal.encryptionAlgorithm) {
                score += opt.rank;
                break;
            }
        }
        for (const auto& opt : keyExchangeMethods) {
            if (opt.value == proposal.keyExchangeMethod) {
                score += opt.rank;
                break;
            }
        }
        for (const auto& opt : authenticationMethods) {
            if (opt.value == proposal.authenticationMethod) {
                score += opt.rank;
                break;
            }
        }
        for (const auto& opt : keySizes) {
            if (opt.value == proposal.keySize) {
                score += opt.rank;
                break;
            }
        }

        return score;
    }
};

/**
 * @brief Represents the state of a negotiation session.
 * 
 * Note: States may represent the perspective of the Initiator, Responder, or be shared.
 */
enum class NegotiationState {
    // Shared States
    IDLE,           // Initial state or after closure/failure (implicit)
    FINALIZED,      // Negotiation successful, parameters agreed upon
    FAILED,         // Negotiation failed (rejected, timeout, error)
    CLOSED,         // Session explicitly closed

    // Initiator States
    INITIATING,         // Sending initial proposal
    AWAITING_RESPONSE,  // Initial proposal sent, waiting for response
    COUNTER_RECEIVED,   // Received a counter-proposal, waiting for local decision
    FINALIZING,         // Accepted response or counter, sending final confirmation

    // Responder States
    PROPOSAL_RECEIVED,  // Received initial proposal, waiting for local decision
    RESPONDING,         // Sending accept/counter/reject response
    AWAITING_FINALIZATION // Sent ACCEPT or COUNTER, waiting for final confirmation from Initiator
};

/**
 * @brief Represents the outcome of a negotiation step.
 */
enum class NegotiationResponse {
    ACCEPTED,           // Proposal accepted
    COUNTER_PROPOSAL,   // New parameters proposed
    REJECTED            // Proposal rejected
};

/**
 * @brief Interface for the negotiation protocol module.
 * 
 * Enables agents to dynamically agree upon communication parameters (like data format,
 * compression, error correction) for a specific interaction session. This promotes
 * adaptability, allowing agents to optimize communication based on context, task,
 * and peer capabilities.
 */
class NegotiationProtocol {
public:
    using SessionId = std::uint64_t; // Type definition for session identifiers

    virtual ~NegotiationProtocol() noexcept = default;

    /**
     * @brief Initiates a negotiation session with a target agent.
     * 
     * Sends an initial proposal of communication parameters.
     * 
     * @param targetAgentId The unique identifier of the agent to negotiate with.
     * @param proposedParams The initial set of parameters proposed by the initiator.
     * @return A unique SessionId for the initiated negotiation.
     * @throws std::runtime_error If initiation fails (e.g., target agent unreachable).
     */
    virtual SessionId initiateSession(const std::string& targetAgentId, 
                                    const NegotiableParams& proposedParams) = 0;

    /**
     * @brief Responds to an incoming negotiation request.
     * 
     * Allows the receiving agent to accept the proposed parameters, propose counter-parameters,
     * or reject the negotiation.
     * 
     * @param sessionId The ID of the session being responded to.
     * @param responseType Indicates whether the proposal is accepted, countered, or rejected.
     * @param responseParams If responseType is COUNTER_PROPOSAL, this contains the new proposed parameters. 
     *                       If ACCEPTED, this might optionally echo the agreed parameters.
     *                       If REJECTED, this is typically empty.
     * @return True if the response was successfully sent, false otherwise.
     * @throws std::runtime_error If the session ID is invalid or the state doesn't allow response.
     */
    virtual bool respondToNegotiation(const SessionId sessionId, 
                                    const NegotiationResponse responseType,
                                    const std::optional<NegotiableParams>& responseParams) = 0;

    /**
     * @brief Finalizes a negotiation session after parameters have been agreed upon.
     * 
     * This step confirms the agreement and transitions the session to a finalized state.
     * It might involve a final handshake message.
     * 
     * @param sessionId The ID of the session to finalize.
     * @return The finally agreed upon NegotiableParams.
     * @throws std::runtime_error If the session cannot be finalized (e.g., not in a state ready for finalization, timeout).
     */
    virtual NegotiableParams finalizeSession(const SessionId sessionId) = 0;

    /**
     * @brief Retrieves the current state of a negotiation session.
     * 
     * @param sessionId The ID of the session to query.
     * @return The current NegotiationState.
     * @throws std::runtime_error If the session ID is invalid.
     */
    virtual NegotiationState getSessionState(const SessionId sessionId) const = 0;

    /**
     * @brief Retrieves the agreed-upon parameters for a finalized session.
     * 
     * @param sessionId The ID of the finalized session.
     * @return The negotiated parameters if the session is finalized, std::nullopt otherwise.
     * @throws std::runtime_error If the session ID is invalid.
     */
    virtual std::optional<NegotiableParams> getNegotiatedParams(const SessionId sessionId) const = 0;

    /**
     * @brief (Initiator Role) Accepts a counter-proposal received from the responder.
     * 
     * This should be called by the initiating agent after receiving a counter-proposal
     * (indicated by the state transitioning to COUNTER_RECEIVED) and deciding to accept it.
     * It triggers the finalization process based on the counter-proposed parameters.
     * 
     * @param sessionId The ID of the session where a counter-proposal was received.
     * @return True if the counter-proposal was successfully accepted and finalization initiated, false otherwise.
     * @throws std::runtime_error If the session ID is invalid or not in the COUNTER_RECEIVED state.
     */
    virtual bool acceptCounterProposal(const SessionId sessionId) = 0;

    /**
     * @brief (Initiator Role) Rejects a counter-proposal received from the responder.
     * 
     * This should be called by the initiating agent after receiving a counter-proposal
     * (indicated by the state transitioning to COUNTER_RECEIVED) and deciding to reject it.
     * This terminates the negotiation session unsuccessfully.
     * 
     * @param sessionId The ID of the session where a counter-proposal was received.
     * @param reason Optional reason for rejection to send to the peer.
     * @return True if the rejection was successfully processed and sent, false otherwise.
     * @throws std::runtime_error If the session ID is invalid or not in the COUNTER_RECEIVED state.
     */
    virtual bool rejectCounterProposal(const SessionId sessionId,
                                     const std::optional<std::string>& reason) = 0;

    /**
     * @brief Closes a negotiation session explicitly.
     * 
     * This can be used to terminate a session prematurely or clean up resources
     * after it's finalized or failed.
     * 
     * @param sessionId The ID of the session to close.
     * @return True if the session was successfully closed, false otherwise.
     */
    virtual bool closeSession(const SessionId sessionId) = 0;

protected:
    // Protected constructor for abstract base class
    NegotiationProtocol() = default;

    // Prevent copying and assignment
    NegotiationProtocol(const NegotiationProtocol&) = delete;
    NegotiationProtocol& operator=(const NegotiationProtocol&) = delete;
    NegotiationProtocol(NegotiationProtocol&&) = delete;
    NegotiationProtocol& operator=(NegotiationProtocol&&) = delete;
};

/**
 * Factory function to create a NegotiationProtocol instance.
 * 
 * @param enableLogging Whether to enable logging for the protocol (default: true)
 * @return A unique_ptr to a NegotiationProtocol instance
 */
std::unique_ptr<NegotiationProtocol> createNegotiationProtocol(bool enableLogging = true);

} // namespace core
} // namespace xenocomm 
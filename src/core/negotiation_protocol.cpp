#include "xenocomm/core/negotiation_protocol.h"
#include <stdexcept> // For std::runtime_error
#include <unordered_map> // To manage sessions
#include <mutex> // For thread safety
#include <atomic> // For generating session IDs
#include <chrono> // For timeouts (optional)
#include <variant> // For message payloads
#include <thread> // For cleanup thread
#include <functional> // For std::function
#include <iostream> // For logging (can be replaced with a proper logging system)
#include <unordered_set> // For state transition validation

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
            case KeyExchangeMethod::X25519:
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
            case AuthenticationMethod::ED25519:
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

    void log(Level level, const std::string& message) {
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
    void warning(const std::string& message) { log(Level::WARNING, message); }
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

// --- Parameter preference with ranking capability ---
struct ParameterPreference {
    template <typename T>
    struct RankedOption {
        T value;
        uint8_t rank;  // Lower is more preferred
        bool required;  // If true, negotiation fails if this can't be satisfied
        std::vector<T> fallbacks;  // Ordered list of fallback options specific to this value

        RankedOption(T v, uint8_t r, bool req = false) 
            : value(v), rank(r), required(req) {}

        RankedOption(T v, uint8_t r, bool req, std::vector<T> fb) 
            : value(v), rank(r), required(req), fallbacks(std::move(fb)) {}

        bool operator<(const RankedOption& other) const {
            return rank < other.rank;
        }
    };
    
    std::vector<RankedOption<DataFormat>> dataFormats;
    std::vector<RankedOption<CompressionAlgorithm>> compressionAlgorithms;
    std::vector<RankedOption<ErrorCorrectionScheme>> errorCorrectionSchemes;
    std::vector<RankedOption<EncryptionAlgorithm>> encryptionAlgorithms;
    std::vector<RankedOption<KeyExchangeMethod>> keyExchangeMethods;
    std::vector<RankedOption<AuthenticationMethod>> authenticationMethods;
    std::vector<RankedOption<KeySize>> keySizes;
    std::map<std::string, std::vector<RankedOption<std::string>>> customParameters;
    
    // Helper to find best match between local and remote preferences
    template <typename T>
    std::optional<T> findBestMatch(const std::vector<RankedOption<T>>& local,
                                  const std::vector<T>& remote) const {
        if (local.empty() || remote.empty()) {
            return std::nullopt;
        }

        // Find the lowest-rank (most preferred) option that's in both sets
        for (const auto& option : local) {
            if (std::find(remote.begin(), remote.end(), option.value) != remote.end()) {
                return option.value;
            }
        }

        // Check if we have any required options that weren't matched
        for (const auto& option : local) {
            if (option.required) {
                // A required option wasn't found in remote options
                return std::nullopt;
            }
        }

        // No match found, but nothing required
        return local.front().value;  // Return our most preferred option
    }

    // Enhanced best match finding with fallback support
    template <typename T>
    std::optional<T> findBestMatchWithFallbacks(const std::vector<RankedOption<T>>& local,
                                              const std::vector<T>& remote) const {
        if (local.empty() || remote.empty()) {
            return std::nullopt;
        }

        // First try to find a direct match with our preferences
        for (const auto& option : local) {
            if (std::find(remote.begin(), remote.end(), option.value) != remote.end()) {
                return option.value;
            }

            // If no direct match, try the fallbacks for this option
            for (const auto& fallback : option.fallbacks) {
                if (std::find(remote.begin(), remote.end(), fallback) != remote.end()) {
                    return fallback;
                }
            }
        }

        // Check if we have any required options that weren't matched
        for (const auto& option : local) {
            if (option.required) {
                // A required option wasn't found in remote options
                return std::nullopt;
            }
        }

        // No match found, but nothing required
        return local.front().value;  // Return our most preferred option
    }

    // Build a NegotiableParams object based on local preferences and remote options
    NegotiableParams buildCompatibleParams(
        const std::vector<DataFormat>& remoteFormats,
        const std::vector<CompressionAlgorithm>& remoteCompression,
        const std::vector<ErrorCorrectionScheme>& remoteErrorCorrection,
        const std::vector<EncryptionAlgorithm>& remoteEncryption,
        const std::vector<KeyExchangeMethod>& remoteKeyExchange,
        const std::vector<AuthenticationMethod>& remoteAuth,
        const std::vector<KeySize>& remoteKeySizes) const {
        
        NegotiableParams params;
        
        auto format = findBestMatch(dataFormats, remoteFormats);
        if (format) {
            params.dataFormat = *format;
        } else if (!dataFormats.empty()) {
            params.dataFormat = dataFormats.front().value;
        }
        
        auto compression = findBestMatch(compressionAlgorithms, remoteCompression);
        if (compression) {
            params.compressionAlgorithm = *compression;
        } else if (!compressionAlgorithms.empty()) {
            params.compressionAlgorithm = compressionAlgorithms.front().value;
        }
        
        auto errorScheme = findBestMatch(errorCorrectionSchemes, remoteErrorCorrection);
        if (errorScheme) {
            params.errorCorrection = *errorScheme;
        } else if (!errorCorrectionSchemes.empty()) {
            params.errorCorrection = errorCorrectionSchemes.front().value;
        }
        
        auto encryption = findBestMatch(encryptionAlgorithms, remoteEncryption);
        if (encryption) {
            params.encryptionAlgorithm = *encryption;
        } else if (!encryptionAlgorithms.empty()) {
            params.encryptionAlgorithm = encryptionAlgorithms.front().value;
        }
        
        auto keyExchange = findBestMatch(keyExchangeMethods, remoteKeyExchange);
        if (keyExchange) {
            params.keyExchangeMethod = *keyExchange;
        } else if (!keyExchangeMethods.empty()) {
            params.keyExchangeMethod = keyExchangeMethods.front().value;
        }
        
        auto authMethod = findBestMatch(authenticationMethods, remoteAuth);
        if (authMethod) {
            params.authenticationMethod = *authMethod;
        } else if (!authenticationMethods.empty()) {
            params.authenticationMethod = authenticationMethods.front().value;
        }
        
        auto keySize = findBestMatch(keySizes, remoteKeySizes);
        if (keySize) {
            params.keySize = *keySize;
        } else if (!keySizes.empty()) {
            params.keySize = keySizes.front().value;
        }
        
        // Validate the resulting parameter set
        auto validationResult = validation::validateParameterSet(params);
        if (validationResult != validation::ValidationResult::VALID) {
            throw std::runtime_error("Failed to create compatible parameter set: " + 
                validation::validationResultToString(validationResult));
        }
        
        return params;
    }

    // Create optimal parameters based solely on local preferences
    NegotiableParams createOptimalParameters() const {
        NegotiableParams params;
        
        // Use the highest preference (lowest rank) for each parameter
        if (!dataFormats.empty()) {
            auto bestFormat = std::min_element(dataFormats.begin(), dataFormats.end());
            params.dataFormat = bestFormat->value;
        }
        
        if (!compressionAlgorithms.empty()) {
            auto bestCompression = std::min_element(compressionAlgorithms.begin(), compressionAlgorithms.end());
            params.compressionAlgorithm = bestCompression->value;
        }
        
        if (!errorCorrectionSchemes.empty()) {
            auto bestErrorScheme = std::min_element(errorCorrectionSchemes.begin(), errorCorrectionSchemes.end());
            params.errorCorrection = bestErrorScheme->value;
        }
        
        if (!encryptionAlgorithms.empty()) {
            auto bestEncryption = std::min_element(encryptionAlgorithms.begin(), encryptionAlgorithms.end());
            params.encryptionAlgorithm = bestEncryption->value;
        }
        
        if (!keyExchangeMethods.empty()) {
            auto bestKeyExchange = std::min_element(keyExchangeMethods.begin(), keyExchangeMethods.end());
            params.keyExchangeMethod = bestKeyExchange->value;
        }
        
        if (!authenticationMethods.empty()) {
            auto bestAuthMethod = std::min_element(authenticationMethods.begin(), authenticationMethods.end());
            params.authenticationMethod = bestAuthMethod->value;
        }
        
        if (!keySizes.empty()) {
            auto bestKeySize = std::min_element(keySizes.begin(), keySizes.end());
            params.keySize = bestKeySize->value;
        }
        
        // Validate the resulting parameter set
        auto validationResult = validation::validateParameterSet(params);
        if (validationResult != validation::ValidationResult::VALID) {
            throw std::runtime_error("Failed to create optimal parameter set: " + 
                validation::validationResultToString(validationResult));
        }
        
        return params;
    }

    // Check if a proposal is compatible with our preferences
    bool isCompatibleWithRequirements(const NegotiableParams& proposal) const {
        // Check if proposal's data format is acceptable
        bool formatCompatible = false;
        bool hasRequiredFormat = false;
        
        for (const auto& format : dataFormats) {
            if (format.required) {
                hasRequiredFormat = true;
                if (proposal.dataFormat == format.value) {
                    formatCompatible = true;
                    break;
                }
            } else if (proposal.dataFormat == format.value) {
                formatCompatible = true;
                break;
            }
        }
        
        // If we have a required format but proposal doesn't match, reject
        if (hasRequiredFormat && !formatCompatible) {
            return false;
        }
        
        // Perform similar checks for compression and error correction
        bool compressionCompatible = false;
        bool hasRequiredCompression = false;
        
        for (const auto& compression : compressionAlgorithms) {
            if (compression.required) {
                hasRequiredCompression = true;
                if (proposal.compressionAlgorithm == compression.value) {
                    compressionCompatible = true;
                    break;
                }
            } else if (proposal.compressionAlgorithm == compression.value) {
                compressionCompatible = true;
                break;
            }
        }
        
        if (hasRequiredCompression && !compressionCompatible) {
            return false;
        }
        
        bool errorCorrectionCompatible = false;
        bool hasRequiredErrorCorrection = false;
        
        for (const auto& errorScheme : errorCorrectionSchemes) {
            if (errorScheme.required) {
                hasRequiredErrorCorrection = true;
                if (proposal.errorCorrection == errorScheme.value) {
                    errorCorrectionCompatible = true;
                    break;
                }
            } else if (proposal.errorCorrection == errorScheme.value) {
                errorCorrectionCompatible = true;
                break;
            }
        }
        
        if (hasRequiredErrorCorrection && !errorCorrectionCompatible) {
            return false;
        }
        
        // Check the overall validity of the parameter set
        auto validationResult = validation::validateParameterSet(proposal);
        return validationResult == validation::ValidationResult::VALID;
    }

    // Calculate a score for a proposal based on how well it matches our preferences
    uint32_t calculateCompatibilityScore(const NegotiableParams& proposal) const {
        // Lower score is better (like rank)
        uint32_t score = 0;
        
        // Find the rank of the proposed data format
        bool formatFound = false;
        for (const auto& format : dataFormats) {
            if (proposal.dataFormat == format.value) {
                score += format.rank;
                formatFound = true;
                break;
            }
        }
        
        // Penalize if format not in our preferences
        if (!formatFound && !dataFormats.empty()) {
            score += 100; // Large penalty for missing format
        }
        
        // Find the rank of the proposed compression algorithm
        bool compressionFound = false;
        for (const auto& compression : compressionAlgorithms) {
            if (proposal.compressionAlgorithm == compression.value) {
                score += compression.rank;
                compressionFound = true;
                break;
            }
        }
        
        // Penalize if compression not in our preferences
        if (!compressionFound && !compressionAlgorithms.empty()) {
            score += 50; // Medium penalty for compression
        }
        
        // Find the rank of the proposed error correction scheme
        bool errorCorrectionFound = false;
        for (const auto& errorScheme : errorCorrectionSchemes) {
            if (proposal.errorCorrection == errorScheme.value) {
                score += errorScheme.rank;
                errorCorrectionFound = true;
                break;
            }
        }
        
        // Penalize if error correction not in our preferences
        if (!errorCorrectionFound && !errorCorrectionSchemes.empty()) {
            score += 50; // Medium penalty for error correction
        }
        
        // Find the rank of the proposed encryption algorithm
        bool encryptionFound = false;
        for (const auto& encryption : encryptionAlgorithms) {
            if (proposal.encryptionAlgorithm == encryption.value) {
                score += encryption.rank;
                encryptionFound = true;
                break;
            }
        }
        
        // Penalize if encryption not in our preferences
        if (!encryptionFound && !encryptionAlgorithms.empty()) {
            score += 50; // Medium penalty for encryption
        }
        
        // Find the rank of the proposed key exchange method
        bool keyExchangeFound = false;
        for (const auto& keyExchange : keyExchangeMethods) {
            if (proposal.keyExchangeMethod == keyExchange.value) {
                score += keyExchange.rank;
                keyExchangeFound = true;
                break;
            }
        }
        
        // Penalize if key exchange not in our preferences
        if (!keyExchangeFound && !keyExchangeMethods.empty()) {
            score += 50; // Medium penalty for key exchange
        }
        
        // Find the rank of the proposed authentication method
        bool authMethodFound = false;
        for (const auto& authMethod : authenticationMethods) {
            if (proposal.authenticationMethod == authMethod.value) {
                score += authMethod.rank;
                authMethodFound = true;
                break;
            }
        }
        
        // Penalize if authentication method not in our preferences
        if (!authMethodFound && !authenticationMethods.empty()) {
            score += 50; // Medium penalty for authentication method
        }
        
        // Find the rank of the proposed key size
        bool keySizeFound = false;
        for (const auto& keySize : keySizes) {
            if (proposal.keySize == keySize.value) {
                score += keySize.rank;
                keySizeFound = true;
                break;
            }
        }
        
        // Penalize if key size not in our preferences
        if (!keySizeFound && !keySizes.empty()) {
            score += 50; // Medium penalty for key size
        }
        
        return score;
    }

    // Generate alternative proposals when initial proposal is rejected
    std::vector<NegotiableParams> generateAlternativeProposals(
        const NegotiableParams& rejectedProposal,
        const std::vector<DataFormat>& remoteFormats,
        const std::vector<CompressionAlgorithm>& remoteCompression,
        const std::vector<ErrorCorrectionScheme>& remoteErrorCorrection,
        const std::vector<EncryptionAlgorithm>& remoteEncryption,
        const std::vector<KeyExchangeMethod>& remoteKeyExchange,
        const std::vector<AuthenticationMethod>& remoteAuth,
        const std::vector<KeySize>& remoteKeySizes,
        size_t maxAlternatives = 3) const {
        
        std::vector<NegotiableParams> alternatives;
        
        // Helper to get all valid options including fallbacks
        auto getAllOptions = [](const auto& rankedOptions) {
            std::vector<typename std::remove_reference_t<decltype(rankedOptions)>::value_type::value_type> allOptions;
            for (const auto& option : rankedOptions) {
                allOptions.push_back(option.value);
                allOptions.insert(allOptions.end(), option.fallbacks.begin(), option.fallbacks.end());
            }
            return allOptions;
        };

        // Get all possible options
        auto allFormats = getAllOptions(dataFormats);
        auto allCompressions = getAllOptions(compressionAlgorithms);
        auto allErrorSchemes = getAllOptions(errorCorrectionSchemes);
        auto allEncryptions = getAllOptions(encryptionAlgorithms);
        auto allKeyExchanges = getAllOptions(keyExchangeMethods);
        auto allAuthMethods = getAllOptions(authenticationMethods);
        auto allKeySizes = getAllOptions(keySizes);

        // Try different combinations, prioritizing keeping more preferred options
        for (const auto& format : allFormats) {
            for (const auto& compression : allCompressions) {
                for (const auto& errorScheme : allErrorSchemes) {
                    for (const auto& encryption : allEncryptions) {
                        for (const auto& keyExchange : allKeyExchanges) {
                            for (const auto& authMethod : allAuthMethods) {
                                for (const auto& keySize : allKeySizes) {
                                    // Skip the rejected combination
                                    if (format == rejectedProposal.dataFormat &&
                                        compression == rejectedProposal.compressionAlgorithm &&
                                        errorScheme == rejectedProposal.errorCorrection &&
                                        encryption == rejectedProposal.encryptionAlgorithm &&
                                        keyExchange == rejectedProposal.keyExchangeMethod &&
                                        authMethod == rejectedProposal.authenticationMethod &&
                                        keySize == rejectedProposal.keySize) {
                                        continue;
                                    }

                                    // Create a new parameter set
                                    NegotiableParams params;
                                    params.dataFormat = format;
                                    params.compressionAlgorithm = compression;
                                    params.errorCorrection = errorScheme;
                                    params.encryptionAlgorithm = encryption;
                                    params.keyExchangeMethod = keyExchange;
                                    params.authenticationMethod = authMethod;
                                    params.keySize = keySize;

                                    // Validate the parameter set
                                    auto validationResult = validation::validateParameterSet(params);
                                    if (validationResult == validation::ValidationResult::VALID) {
                                        // Check if the remote peer supports these parameters
                                        bool isSupported = 
                                            std::find(remoteFormats.begin(), remoteFormats.end(), format) != remoteFormats.end() &&
                                            std::find(remoteCompression.begin(), remoteCompression.end(), compression) != remoteCompression.end() &&
                                            std::find(remoteErrorCorrection.begin(), remoteErrorCorrection.end(), errorScheme) != remoteErrorCorrection.end() &&
                                            std::find(remoteEncryption.begin(), remoteEncryption.end(), encryption) != remoteEncryption.end() &&
                                            std::find(remoteKeyExchange.begin(), remoteKeyExchange.end(), keyExchange) != remoteKeyExchange.end() &&
                                            std::find(remoteAuth.begin(), remoteAuth.end(), authMethod) != remoteAuth.end() &&
                                            std::find(remoteKeySizes.begin(), remoteKeySizes.end(), keySize) != remoteKeySizes.end();

                                        if (isSupported) {
                                            alternatives.push_back(params);
                                            if (alternatives.size() >= maxAlternatives) {
                                                return alternatives;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        return alternatives;
    }

    // Build compatible parameters with enhanced fallback support
    NegotiableParams buildCompatibleParamsWithFallbacks(
        const std::vector<DataFormat>& remoteFormats,
        const std::vector<CompressionAlgorithm>& remoteCompression,
        const std::vector<ErrorCorrectionScheme>& remoteErrorCorrection,
        const std::vector<EncryptionAlgorithm>& remoteEncryption,
        const std::vector<KeyExchangeMethod>& remoteKeyExchange,
        const std::vector<AuthenticationMethod>& remoteAuth,
        const std::vector<KeySize>& remoteKeySizes
    ) const {
        NegotiableParams params;
        
        // Find best matching options for each parameter
        params.dataFormat = findBestMatchWithFallbacks(dataFormats, remoteFormats);
        params.compressionAlgorithm = findBestMatchWithFallbacks(compressionAlgorithms, remoteCompression);
        params.errorCorrection = findBestMatchWithFallbacks(errorCorrectionSchemes, remoteErrorCorrection);
        params.encryptionAlgorithm = findBestMatchWithFallbacks(encryptionAlgorithms, remoteEncryption);
        params.keyExchangeMethod = findBestMatchWithFallbacks(keyExchangeMethods, remoteKeyExchange);
        params.authenticationMethod = findBestMatchWithFallbacks(authenticationMethods, remoteAuth);
        params.keySize = findBestMatchWithFallbacks(keySizes, remoteKeySizes);
        
        // Validate the combined parameter set
        auto validationResult = validation::validateParameterSet(params);
        
        // If validation fails, try fallback to no encryption
        if (validationResult != validation::ValidationResult::VALID) {
            params.encryptionAlgorithm = EncryptionAlgorithm::NONE;
            params.keyExchangeMethod = KeyExchangeMethod::NONE;
            params.keySize = KeySize::NONE;
            
            // Revalidate with fallback security settings
            validationResult = validation::validateParameterSet(params);
            if (validationResult != validation::ValidationResult::VALID) {
                throw std::runtime_error("Failed to find compatible parameters: " + 
                                       validation::validationResultToString(validationResult));
            }
        }

        return params;
    }
};

// Enhanced message structures with sequence IDs and timestamps
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

// --- Internal Message Representation (Placeholder) ---
// This simulates what the network layer might provide.

enum class MessageType {
    PROPOSE,
    ACCEPT,
    COUNTER,
    REJECT,
    FINALIZE,
    CLOSE
};

// Simple payload structures (could be more complex, e.g., serialized data)
struct ProposePayload { NegotiableParams params; };
struct AcceptPayload { std::optional<NegotiableParams> params; }; // Optional echo
struct CounterPayload { NegotiableParams params; };
struct RejectPayload { std::optional<std::string> reason; };
struct FinalizePayload { NegotiableParams params; };
struct ClosePayload { std::optional<std::string> reason; };

// Using std::variant for the payload type
using MessagePayload = std::variant<
    std::monostate, // For messages potentially without payload
    ProposePayload,
    AcceptPayload,
    CounterPayload,
    RejectPayload,
    FinalizePayload,
    ClosePayload
>;

// Forward declaration for internal session data structure
struct NegotiationSessionData;

// --- Event Handler Types ---
// Define the types of event handlers that can be registered
using StateChangeHandler = std::function<void(NegotiationState, NegotiationState, const std::string&)>;

class ConcreteNegotiationProtocol : public NegotiationProtocol {
public:
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
    
    ~ConcreteNegotiationProtocol() override = default;

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
    // Structure to hold internal state for each negotiation session
    struct NegotiationSessionData {
        std::string initiatorAgentId;
        std::string targetAgentId;
        NegotiableParams initialProposal;
        std::optional<NegotiableParams> counterProposal;
        std::optional<NegotiableParams> finalParams;
        NegotiationState state = NegotiationState::IDLE; // Default to IDLE
        
        // Timing information for timeout handling
        std::chrono::steady_clock::time_point createdTime = std::chrono::steady_clock::now();
        std::unordered_map<NegotiationState, std::chrono::steady_clock::time_point> stateTimestamps;
        
        // Retry counters
        int retryCount = 0;
        
        // Additional metadata
        std::optional<std::string> lastError;
        std::optional<std::string> failureReason;
        
        // Track fallback attempts
        std::vector<NegotiableParams> triedProposals;
        size_t fallbackAttempts = 0;
        static constexpr size_t MAX_FALLBACK_ATTEMPTS = 3;
        
        // Store remote capabilities for fallback generation
        std::vector<DataFormat> remoteFormats;
        std::vector<CompressionAlgorithm> remoteCompression;
        std::vector<ErrorCorrectionScheme> remoteErrorCorrection;
        std::vector<EncryptionAlgorithm> remoteEncryption;
        std::vector<KeyExchangeMethod> remoteKeyExchange;
        std::vector<AuthenticationMethod> remoteAuth;
        std::vector<KeySize> remoteKeySizes;
        
        // Constructor with initialization
        NegotiationSessionData() {
            // Record initial state timestamp
            stateTimestamps[NegotiationState::IDLE] = createdTime;
            fallbackAttempts = 0;
        }
    };

    // Helper to get session data safely
    NegotiationSessionData& getSessionData(SessionId sessionId);
    const NegotiationSessionData& getSessionData(SessionId sessionId) const;
    
    // State transition helper with validation and event handling
    bool transitionState(SessionId sessionId, NegotiationState newState, const std::string& reason = "");
    
    // Helper method to check for timeouts
    bool isStateTimedOut(const NegotiationSessionData& session, std::chrono::milliseconds timeout = DEFAULT_NEGOTIATION_TIMEOUT) const;

    mutable std::mutex sessionsMutex_;
    std::unordered_map<SessionId, NegotiationSessionData> sessions_;
    std::atomic<SessionId> nextSessionId_;
    Logger logger_;
    std::vector<StateChangeHandler> stateChangeHandlers_;

    // --- Private helper methods for actual communication --- 
    // These would interact with ConnectionManager, TransmissionManager etc.
    // They are placeholders here.
    bool sendProposal(const std::string& targetAgentId, SessionId sessionId, const NegotiableParams& params) {
        // TODO: Implement actual network send logic
        (void)targetAgentId; (void)sessionId; (void)params;
        // Simulate success for now
        return true;
    }

    bool sendResponse(SessionId sessionId, NegotiationResponse responseType, const std::optional<NegotiableParams>& params) {
        // TODO: Implement actual network send logic
        (void)sessionId; (void)responseType; (void)params;
        // Simulate success for now
        return true;
    }

     bool sendFinalization(SessionId sessionId, const NegotiableParams& finalParams) {
        // TODO: Implement actual network send logic (e.g., ACK + final params)
        (void)sessionId; (void)finalParams;
        // Simulate success for now
        return true;
    }

    bool sendReject(SessionId sessionId, const std::optional<std::string>& reason) {
        // TODO: Implement actual network send logic for REJECT message
        (void)sessionId; (void)reason;
        // Simulate success for now
        return true;
    }

    // Placeholder for handling incoming messages - would be called by network layer
    // This is the entry point for network events related to negotiation.
    void handleIncomingMessage(SessionId sessionId, MessageType type, const MessagePayload& payload);

    // --- Methods for negotiation proposal and response handling ---
    
    /**
     * @brief Creates a proposal based on local preferences
     *
     * @param preferences Local parameter preferences
     * @return NegotiableParams A proposal based on the best local preferences
     */
    NegotiableParams createProposal(const ParameterPreference& preferences) {
        // Try to create optimal parameters first
        try {
            return preferences.createOptimalParameters();
        } catch (const std::runtime_error& e) {
            logger_.warning("Failed to create optimal parameters: " + std::string(e.what()));
            
            // Fall back to minimum viable parameters
            NegotiableParams fallback;
            fallback.dataFormat = DataFormat::BINARY_CUSTOM;  // Most basic format
            fallback.compressionAlgorithm = CompressionAlgorithm::NONE;
            fallback.errorCorrection = ErrorCorrectionScheme::NONE;
            fallback.encryptionAlgorithm = EncryptionAlgorithm::NONE;
            fallback.keyExchangeMethod = KeyExchangeMethod::NONE;
            fallback.authenticationMethod = AuthenticationMethod::NONE;
            fallback.keySize = KeySize::NONE;
            
            return fallback;
        }
    }
    
    /**
     * @brief Creates a counter-proposal that tries to accommodate both peers' preferences
     *
     * @param localPreferences The local agent's preferences
     * @param remoteProposal The proposal received from the remote agent
     * @return std::optional<NegotiableParams> A counter-proposal if possible, nullopt if no compatible settings
     */
    std::optional<NegotiableParams> createCounterProposal(
        const NegotiableParams& receivedParams,
        const ParameterPreference& preferences,
        NegotiationSessionData& session) {
        
        // Store remote capabilities if not already stored
        if (session.remoteFormats.empty()) {
            session.remoteFormats.push_back(receivedParams.dataFormat);
            session.remoteCompression.push_back(receivedParams.compressionAlgorithm);
            session.remoteErrorCorrection.push_back(receivedParams.errorCorrection);
            session.remoteEncryption.push_back(receivedParams.encryptionAlgorithm);
            session.remoteKeyExchange.push_back(receivedParams.keyExchangeMethod);
            session.remoteAuth.push_back(receivedParams.authenticationMethod);
            session.remoteKeySizes.push_back(receivedParams.keySize);
        }
        
        try {
            // Try to create compatible parameters with fallback support
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
    
    /**
     * @brief Evaluates a proposal against local preferences to determine if it's acceptable
     *
     * @param localPreferences The local agent's preferences
     * @param proposal The proposal to evaluate
     * @return NegotiationResponse ACCEPTED, COUNTER_PROPOSAL, or REJECTED
     */
    NegotiationResponse evaluateProposal(
        const NegotiableParams& proposedParams,
        const ParameterPreference& preferences,
        NegotiationSessionData& session) {
        
        // Store remote capabilities
        session.remoteFormats.push_back(proposedParams.dataFormat);
        session.remoteCompression.push_back(proposedParams.compressionAlgorithm);
        session.remoteErrorCorrection.push_back(proposedParams.errorCorrection);
        session.remoteEncryption.push_back(proposedParams.encryptionAlgorithm);
        session.remoteKeyExchange.push_back(proposedParams.keyExchangeMethod);
        session.remoteAuth.push_back(proposedParams.authenticationMethod);
        session.remoteKeySizes.push_back(proposedParams.keySize);
        
        // Check if we've seen this exact proposal before
        auto it = std::find(session.triedProposals.begin(), session.triedProposals.end(), proposedParams);
        if (it != session.triedProposals.end()) {
            logger_.warning("Received a previously tried proposal combination");
            return NegotiationResponse::REJECTED;
        }
        
        // Add to tried proposals
        session.triedProposals.push_back(proposedParams);
        
        // Check compatibility with our requirements
        if (preferences.isCompatibleWithRequirements(proposedParams)) {
            return NegotiationResponse::ACCEPTED;
        }
        
        // If not compatible but we haven't exceeded fallback attempts, try counter-proposal
        if (session.fallbackAttempts < session.MAX_FALLBACK_ATTEMPTS) {
            session.fallbackAttempts++;
            return NegotiationResponse::COUNTER_PROPOSAL;
        }
        
        // If we've exhausted fallback attempts, reject
        return NegotiationResponse::REJECTED;
    }
    
    /**
     * @brief Handles an incoming proposal from a remote agent
     * 
     * @param sessionId The ID of the session receiving the proposal
     * @param proposedParams The negotiable parameters proposed by the remote agent
     * @return true if the proposal was handled successfully
     * @return false if there was an error processing the proposal
     */
    bool handleProposal(SessionId sessionId, const NegotiableParams& proposedParams) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        
        auto& session = getSessionData(sessionId);
        
        // Create preferences for evaluation (this should be customizable per session in practice)
        ParameterPreference preferences;
        // ... setup preferences ...
        
        auto response = evaluateProposal(proposedParams, preferences, session);
        
        switch (response) {
            case NegotiationResponse::ACCEPTED:
                return acceptProposal(sessionId, proposedParams);
                
            case NegotiationResponse::COUNTER_PROPOSAL: {
                auto counterProposal = createCounterProposal(proposedParams, preferences, session);
                if (counterProposal) {
                    session.counterProposal = *counterProposal;
                    return sendResponse(sessionId, NegotiationResponse::COUNTER_PROPOSAL, *counterProposal);
                }
                // Fall through to rejection if counter-proposal creation fails
            }
                
            case NegotiationResponse::REJECTED:
            default:
                return rejectProposal(sessionId, "Parameters not compatible with requirements");
        }
    }
    
    /**
     * @brief Accepts a proposal in the current session
     * 
     * @param sessionId The ID of the session
     * @param finalParams The parameters being accepted (optional, defaults to the initial proposal)
     * @return true if accepted successfully
     * @return false if there was an error
     */
    bool acceptProposal(SessionId sessionId, const std::optional<NegotiableParams>& finalParams = std::nullopt) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto& session = getSessionData(sessionId); // Throws if not found
        
        // Only valid from PROPOSAL_RECEIVED state
        if (session.state != NegotiationState::PROPOSAL_RECEIVED) {
            logger_.error("Cannot accept proposal: Invalid state " + 
                         StateTransitionValidator::stateToString(session.state));
            return false;
        }
        
        // Use the provided parameters if given, otherwise use the initial proposal
        NegotiableParams paramsToAccept = finalParams.value_or(session.initialProposal);
        
        // Transition to RESPONDING state
        if (!transitionState(sessionId, NegotiationState::RESPONDING, "Preparing acceptance")) {
            return false;
        }
        
        // Send ACCEPT response
        bool sentOk = sendResponse(sessionId, NegotiationResponse::ACCEPTED, paramsToAccept);
        
        if (sentOk) {
            // Record the final parameters
            session.finalParams = paramsToAccept;
            
            // Transition to AWAITING_FINALIZATION state
            return transitionState(sessionId, NegotiationState::AWAITING_FINALIZATION, 
                                  "Acceptance sent, awaiting finalization");
        } else {
            // Failed to send acceptance
            transitionState(sessionId, NegotiationState::FAILED, "Failed to send acceptance");
            return false;
        }
    }
    
    /**
     * @brief Rejects a proposal from the remote agent
     * 
     * @param sessionId The ID of the session
     * @param reason Optional reason for rejection
     * @return true if rejection was sent successfully
     * @return false if there was an error
     */
    bool rejectProposal(SessionId sessionId, const std::optional<std::string>& reason = std::nullopt) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto& session = getSessionData(sessionId); // Throws if not found
        
        // Only valid from PROPOSAL_RECEIVED state
        if (session.state != NegotiationState::PROPOSAL_RECEIVED) {
            logger_.error("Cannot reject proposal: Invalid state " + 
                         StateTransitionValidator::stateToString(session.state));
            return false;
        }
        
        // Transition to RESPONDING state
        if (!transitionState(sessionId, NegotiationState::RESPONDING, "Preparing rejection")) {
            return false;
        }
        
        // Send REJECT response
        bool sentOk = sendResponse(sessionId, NegotiationResponse::REJECTED, std::nullopt);
        
        if (sentOk) {
            // Transition to FAILED state
            std::string failureReason = "Proposal rejected";
            if (reason) {
                failureReason += ": " + *reason;
            }
            return transitionState(sessionId, NegotiationState::FAILED, failureReason);
        } else {
            // Failed to send rejection
            transitionState(sessionId, NegotiationState::FAILED, "Failed to send rejection");
            return false;
        }
    }

    /**
     * @brief Automatically process a proposal based on the local preferences
     * 
     * @param sessionId The ID of the session with a received proposal
     * @param preferences The local preferences to use for evaluation
     * @return true if the proposal was processed successfully (accepted, counter, or rejected)
     * @return false if there was an error processing the proposal
     */
    bool autoProcessProposal(SessionId sessionId, const ParameterPreference& preferences) {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto& session = getSessionData(sessionId); // Throws if not found
        
        // Verify we're in the correct state
        if (session.state != NegotiationState::PROPOSAL_RECEIVED) {
            logger_.error("Cannot auto-process proposal: Invalid state " + 
                         StateTransitionValidator::stateToString(session.state) + 
                         ", expected PROPOSAL_RECEIVED");
            return false;
        }
        
        // Log initial proposal
        logger_.info("Auto-processing proposal for session " + std::to_string(sessionId) + 
                    ". Incoming format: " + std::to_string(static_cast<int>(session.initialProposal.dataFormat)) +
                    ", compression: " + std::to_string(static_cast<int>(session.initialProposal.compressionAlgorithm)) + 
                    ", error correction: " + std::to_string(static_cast<int>(session.initialProposal.errorCorrection)) +
                    ", encryption: " + std::to_string(static_cast<int>(session.initialProposal.encryptionAlgorithm)) +
                    ", key exchange: " + std::to_string(static_cast<int>(session.initialProposal.keyExchangeMethod)) +
                    ", auth method: " + std::to_string(static_cast<int>(session.initialProposal.authenticationMethod)) +
                    ", key size: " + std::to_string(static_cast<int>(session.initialProposal.keySize)));
        
        // Evaluate the proposal against preferences
        auto response = evaluateProposal(session.initialProposal, preferences, session);
        
        // Process based on evaluation result
        switch (response) {
            case NegotiationResponse::ACCEPTED:
                logger_.info("Auto-accepting proposal for session " + std::to_string(sessionId));
                return acceptProposal(sessionId, session.finalParams);
                
            case NegotiationResponse::COUNTER_PROPOSAL: {
                auto counterProposal = createCounterProposal(session.initialProposal, preferences, session);
                if (counterProposal) {
                    logger_.info("Auto-countering proposal for session " + std::to_string(sessionId) + 
                               ". Counter format: " + std::to_string(static_cast<int>(counterProposal->dataFormat)) +
                               ", compression: " + std::to_string(static_cast<int>(counterProposal->compressionAlgorithm)) + 
                               ", error correction: " + std::to_string(static_cast<int>(counterProposal->errorCorrection)) +
                               ", encryption: " + std::to_string(static_cast<int>(counterProposal->encryptionAlgorithm)) +
                               ", key exchange: " + std::to_string(static_cast<int>(counterProposal->keyExchangeMethod)) +
                               ", auth method: " + std::to_string(static_cast<int>(counterProposal->authenticationMethod)) +
                               ", key size: " + std::to_string(static_cast<int>(counterProposal->keySize)));
                    return respondToNegotiation(sessionId, NegotiationResponse::COUNTER_PROPOSAL, counterProposal);
                } else {
                    logger_.error("Auto-process error: Counter proposal lacks parameters");
                    return false;
                }
            }
                
            case NegotiationResponse::REJECTED:
                logger_.info("Auto-rejecting proposal for session " + std::to_string(sessionId) + 
                           ": Incompatible with requirements");
                return rejectProposal(sessionId, "Incompatible with local requirements");
                
            default:
                logger_.error("Auto-process error: Unknown evaluation result");
                return false;
        }
    }
};

// --- New State Transition Implementation ---
bool ConcreteNegotiationProtocol::transitionState(SessionId sessionId, NegotiationState newState, const std::string& reason) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto& session = getSessionData(sessionId);
    NegotiationState currentState = session.state;
    
    // Validate the state transition
    if (!StateTransitionValidator::isValidTransition(currentState, newState)) {
        logger_.error("Invalid state transition: " + 
                     StateTransitionValidator::stateToString(currentState) + " -> " + 
                     StateTransitionValidator::stateToString(newState) + 
                     (reason.empty() ? "" : " (" + reason + ")"));
        return false;
    }
    
    // Update the state
    session.state = newState;
    
    // Record timestamp for the new state
    session.stateTimestamps[newState] = std::chrono::steady_clock::now();
    
    // Clear retry count if moving to a new state (not repeating the same state)
    if (currentState != newState) {
        session.retryCount = 0;
    }
    
    // Log the transition
    logger_.info("Session " + std::to_string(sessionId) + " state transition: " + 
                StateTransitionValidator::stateToString(currentState) + " -> " + 
                StateTransitionValidator::stateToString(newState) + 
                (reason.empty() ? "" : " (" + reason + ")"));
    
    // Notify event handlers
    for (const auto& handler : stateChangeHandlers_) {
        try {
            handler(currentState, newState, reason);
        } catch (const std::exception& e) {
            logger_.error("Exception in state change handler: " + std::string(e.what()));
        }
    }
    
    return true;
}

// --- Timeout Checking Implementation ---
bool ConcreteNegotiationProtocol::isStateTimedOut(const NegotiationSessionData& session, std::chrono::milliseconds timeout) const {
    auto currentState = session.state;
    auto it = session.stateTimestamps.find(currentState);
    
    if (it == session.stateTimestamps.end()) {
        // No timestamp for current state, use creation time as fallback
        return (std::chrono::steady_clock::now() - session.createdTime) > timeout;
    }
    
    return (std::chrono::steady_clock::now() - it->second) > timeout;
}

// --- Method Implementations --- 

NegotiationProtocol::SessionId ConcreteNegotiationProtocol::initiateSession(const std::string& targetAgentId, const NegotiableParams& proposedParams) {
    // Validate params before proceeding
    auto validationResult = validation::validateParameterSet(proposedParams);
    if (validationResult != validation::ValidationResult::VALID) {
        throw std::runtime_error("Invalid negotiation parameters: " + 
                                validation::validationResultToString(validationResult));
    }
    
    SessionId sessionId = nextSessionId_.fetch_add(1);
    
    // Create and initialize session data
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

// (Initiator Role) Accept a counter-proposal
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
        transitionState(sessionId, NegotiationState::FAILED, "Failed to send finalization");
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

// --- Private Helper Implementations ---

ConcreteNegotiationProtocol::NegotiationSessionData& ConcreteNegotiationProtocol::getSessionData(SessionId sessionId) {
     auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        throw std::runtime_error("Invalid or unknown session ID: " + std::to_string(sessionId));
    }
    return it->second;
}

const ConcreteNegotiationProtocol::NegotiationSessionData& ConcreteNegotiationProtocol::getSessionData(SessionId sessionId) const {
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        throw std::runtime_error("Invalid or unknown session ID: " + std::to_string(sessionId));
    }
    return it->second;
}

// --- Incoming Message Handler Implementation ---

void ConcreteNegotiationProtocol::handleIncomingMessage(SessionId sessionId, MessageType type, const MessagePayload& payload) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    try {
        auto& session = getSessionData(sessionId); // Find the session or throw

        // Create a string representation of the message type for logging
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
            case NegotiationState::IDLE: // Initial state
                // The only valid message in IDLE state is a PROPOSE
                if (type == MessageType::PROPOSE) {
                    if (const auto* proposePayload = std::get_if<ProposePayload>(&payload)) {
                        // Handle proposal from remote agent
                        handleProposal(sessionId, proposePayload->params);
                    } else {
                        transitionState(sessionId, NegotiationState::FAILED, "Invalid payload type for PROPOSE message");
                    }
                } else {
                    logger_.warning("Ignoring unexpected " + messageTypeStr + " message in IDLE state");
                }
                break;

            case NegotiationState::AWAITING_RESPONSE: // Initiator waiting for response
                switch (type) {
                    case MessageType::ACCEPT:
                        // Initiator receives acceptance of its initial proposal.
                        if (std::holds_alternative<AcceptPayload>(payload) || std::holds_alternative<std::monostate>(payload)) {
                            // AcceptPayload might optionally echo params, but we rely on our initial ones.
                            session.finalParams = session.initialProposal;
                            transitionState(sessionId, NegotiationState::FINALIZING, "Received acceptance of initial proposal");
                        } else {
                            transitionState(sessionId, NegotiationState::FAILED, "Invalid payload type for ACCEPT message");
                        }
                        break;
                    case MessageType::COUNTER:
                        // Initiator receives a counter-proposal.
                        if (const auto* counterPayload = std::get_if<CounterPayload>(&payload)) {
                            session.counterProposal = counterPayload->params;
                            transitionState(sessionId, NegotiationState::COUNTER_RECEIVED, "Received counter-proposal");
                        } else {
                            transitionState(sessionId, NegotiationState::FAILED, "Invalid payload type for COUNTER message");
                        }
                        break;
                    case MessageType::REJECT:
                        // Initiator receives rejection of its initial proposal.
                        std::string reason = "Proposal rejected by peer";
                        if (const auto* rejectPayload = std::get_if<RejectPayload>(&payload)) {
                            if (rejectPayload->reason) {
                                reason = "Proposal rejected: " + *rejectPayload->reason;
                            }
                        }
                        transitionState(sessionId, NegotiationState::FAILED, reason);
                        break;
                    case MessageType::CLOSE:
                        // Handle potential early close
                        transitionState(sessionId, NegotiationState::CLOSED, "Session closed by peer while awaiting response");
                        break;
                    default:
                        // Invalid message type for this state
                        transitionState(sessionId, NegotiationState::FAILED, 
                                      "Protocol violation: Unexpected " + messageTypeStr + " message in AWAITING_RESPONSE state");
                        break;
                }
                break;

            case NegotiationState::AWAITING_FINALIZATION: // Responder waiting for finalization
                 switch (type) {
                    case MessageType::FINALIZE:
                        // Responder receives final confirmation from Initiator.
                        if (const auto* finalizePayload = std::get_if<FinalizePayload>(&payload)) {
                            // Verify finalized params match expectations (optional but good practice)
                            session.finalParams = finalizePayload->params;
                            transitionState(sessionId, NegotiationState::FINALIZED, "Negotiation successfully finalized");
                        } else {
                            transitionState(sessionId, NegotiationState::FAILED, "Invalid payload type for FINALIZE message");
                        }
                        break;
                    case MessageType::REJECT: // Initiator could reject a counter-proposal
                        // Responder receives rejection, likely of its counter-proposal.
                        std::string reason = "Counter-proposal rejected by peer";
                        if (const auto* rejectPayload = std::get_if<RejectPayload>(&payload)) {
                            if (rejectPayload->reason) {
                                reason = "Counter-proposal rejected: " + *rejectPayload->reason;
                            }
                        }
                        transitionState(sessionId, NegotiationState::FAILED, reason);
                        break;
                     case MessageType::CLOSE:
                        transitionState(sessionId, NegotiationState::CLOSED, "Session closed by peer while awaiting finalization");
                        break;
                    default:
                        // Invalid message type for this state
                        transitionState(sessionId, NegotiationState::FAILED, 
                                      "Protocol violation: Unexpected " + messageTypeStr + " message in AWAITING_FINALIZATION state");
                        break;
                }
                break;
            
             case NegotiationState::PROPOSAL_RECEIVED: // Responder deciding
             case NegotiationState::RESPONDING:        // Responder sending response
             case NegotiationState::COUNTER_RECEIVED: // Initiator deciding on counter
             case NegotiationState::FINALIZING:      // Initiator sending finalize
                // Generally, should not receive messages in these transient/active states,
                // except perhaps CLOSE
                 if (type == MessageType::CLOSE) {
                     transitionState(sessionId, NegotiationState::CLOSED, "Session closed by peer during active phase");
                 } else {
                    // Unexpected message in this state, but don't fail the session
                    logger_.warning("Unexpected " + messageTypeStr + " message received in " + 
                                 StateTransitionValidator::stateToString(session.state) + " state");
                 }
                break;

            case NegotiationState::FINALIZED:
            case NegotiationState::FAILED:
            case NegotiationState::CLOSED:
                // Session is already in a terminal state. Should only potentially handle CLOSE
                 if (type == MessageType::CLOSE) {
                     // Only log, don't transition if already in CLOSED state
                     if (session.state != NegotiationState::CLOSED) {
                         transitionState(sessionId, NegotiationState::CLOSED, "Session closed by peer");
                     }
                 } else {
                     logger_.info("Ignoring " + messageTypeStr + " message for session in terminal state " + 
                                StateTransitionValidator::stateToString(session.state));
                 }
                break;
            
            case NegotiationState::INITIATING: // Should not receive messages while initiating
                 logger_.warning("Received " + messageTypeStr + " message for session in unexpected state " + 
                              StateTransitionValidator::stateToString(session.state));
                 break;
        }

    } catch (const std::runtime_error& e) {
        logger_.error("Runtime error handling message for session " + std::to_string(sessionId) + ": " + e.what());
        
        // Attempt to mark session as FAILED if it exists
        try {
            auto it = sessions_.find(sessionId);
            if (it != sessions_.end()) {
                transitionState(sessionId, NegotiationState::FAILED, "Error processing message: " + std::string(e.what()));
            }
        } catch (...) { /* Ignore potential recursive exceptions */ }
        
    } catch (const std::bad_variant_access& e) {
        logger_.error("Bad variant access for session " + std::to_string(sessionId) + ": " + e.what());
        
        // Attempt to mark session as FAILED if it exists
        try {
            auto it = sessions_.find(sessionId);
            if (it != sessions_.end()) {
                transitionState(sessionId, NegotiationState::FAILED, "Error processing message payload: " + std::string(e.what()));
            }
        } catch (...) { /* Ignore */ }
    }
}

// --- Reject Counter Implementation ---
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

// --- Factory Function ---
// Create a NegotiationProtocol instance with default configuration
std::unique_ptr<NegotiationProtocol> createNegotiationProtocol(bool enableLogging) {
    return std::make_unique<ConcreteNegotiationProtocol>(enableLogging);
}

} // namespace core
} // namespace xenocomm 
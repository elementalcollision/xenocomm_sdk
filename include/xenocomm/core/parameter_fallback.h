#pragma once

// Include negotiation_protocol.h removed to break cycle
#include "xenocomm/core/negotiation_preferences.h"
#include <vector>
#include <optional>
#include <string>
#include <map>
#include <algorithm>
#include <limits> // Required for numeric_limits

namespace xenocomm {
namespace core {

// Forward declaration
struct NegotiationPreferences;
struct NegotiableParams; // Also forward declare if used in declarations
enum class DataFormat : uint8_t; 

enum class CompressionAlgorithm : uint8_t;
enum class ErrorCorrectionScheme : uint8_t;

/**
 * @brief Configuration for parameter fallback behavior.
 */
struct FallbackConfig {
    bool allowFormatDowngrade{true};      // Allow falling back to simpler data formats
    bool allowCompressionDowngrade{true};  // Allow disabling compression
    bool allowErrorCorrectionDowngrade{true}; // Allow simpler error correction schemes
    std::size_t maxFallbackAttempts{3};   // Maximum number of fallback attempts
};

/**
 * @brief Manages fallback strategies for negotiable parameters.
 * 
 * Provides logic to determine acceptable parameter sets and generate
 * fallback options when initial proposals are rejected during negotiation.
 */
class ParameterFallback {
public:
    // Constructor takes const reference now
    explicit ParameterFallback(const NegotiationPreferences& preferences);

    /**
     * @brief Checks if a given set of parameters is acceptable according to preferences.
     */
    bool isAcceptable(const NegotiableParams& params) const;

    /**
     * @brief Generates the next fallback parameter set based on the current rejected set.
     *
     * Tries to find the next best combination according to preferences.
     * Returns std::nullopt if no further fallback options are possible.
     */
    std::optional<NegotiableParams> getNextFallback(const NegotiableParams& current) const;

private:
    // Stores a const reference to avoid needing full definition here
    const NegotiationPreferences& preferences_;

    // Added declarations for helper functions
    bool isFormatCompatibleWithCompression(DataFormat format, CompressionAlgorithm compression) const;
    bool isFormatCompatibleWithErrorCorrection(DataFormat format, ErrorCorrectionScheme scheme) const;

    template<typename T>
    std::optional<T> getNextPreferred(const std::vector<T>& preferred, T current) const {
        auto it = std::find(preferred.begin(), preferred.end(), current);
        if (it != preferred.end() && std::next(it) != preferred.end()) {
            return *std::next(it);
        }
        return std::nullopt;
    }
};

/**
 * @brief Class handling parameter fallback logic during negotiation.
 */
class ParameterFallbackHandler {
public:
    explicit ParameterFallbackHandler(
        const NegotiationPreferences& prefs,
        const FallbackConfig& config = FallbackConfig());

    /**
     * @brief Generate fallback parameters after a rejection.
     * 
     * @param rejected The rejected parameters
     * @param attempt Current fallback attempt number
     * @return Next set of parameters to try, or std::nullopt if no more fallbacks
     */
    std::optional<NegotiableParams> handleRejection(
        const NegotiableParams& rejected,
        std::size_t attempt) const;

    /**
     * @brief Check if proposed parameters are acceptable according to preferences.
     * 
     * @param params Parameters to check
     * @return true if parameters are acceptable
     */
    bool areParametersAcceptable(const NegotiableParams& params) const;

    /**
     * @brief Get the current preferences.
     * 
     * @return const reference to current preferences
     */
    const NegotiationPreferences& getPreferences() const { return preferences_; }

    /**
     * @brief Get the current fallback configuration.
     * 
     * @return const reference to current config
     */
    const FallbackConfig& getConfig() const { return config_; }

private:
    NegotiationPreferences preferences_;
    FallbackConfig config_;

    /**
     * @brief Try to find a fallback for data format while keeping other parameters.
     * 
     * @param current Current parameters
     * @return New parameters with fallback format, or std::nullopt if no fallback available
     */
    std::optional<NegotiableParams> tryDataFormatFallback(
        const NegotiableParams& current) const;

    /**
     * @brief Try to find a fallback for compression while keeping other parameters.
     * 
     * @param current Current parameters
     * @return New parameters with fallback compression, or std::nullopt if no fallback available
     */
    std::optional<NegotiableParams> tryCompressionFallback(
        const NegotiableParams& current) const;

    /**
     * @brief Try to find a fallback for error correction while keeping other parameters.
     * 
     * @param current Current parameters
     * @return New parameters with fallback error correction, or std::nullopt if no fallback available
     */
    std::optional<NegotiableParams> tryErrorCorrectionFallback(
        const NegotiableParams& current) const;
};

} // namespace core
} // namespace xenocomm 
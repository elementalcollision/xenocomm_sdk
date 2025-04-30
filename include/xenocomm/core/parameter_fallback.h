#pragma once

#include "xenocomm/core/negotiation_protocol.h"
#include <vector>
#include <functional>
#include <optional>

namespace xenocomm {
namespace core {

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
 * @brief Represents a parameter preference with fallback options.
 * 
 * @tparam T The parameter type (e.g., DataFormat, CompressionAlgorithm)
 */
template<typename T>
struct ParameterPreference {
    T preferred;                     // Most preferred option
    std::vector<T> fallbackOrder;    // Ordered list of fallback options
    bool required{false};            // If true, negotiation fails if no option works
    
    /**
     * @brief Get the next fallback option after the current one.
     * 
     * @param current The current option in use
     * @return The next fallback option if available, std::nullopt otherwise
     */
    std::optional<T> getNextFallback(const T& current) const {
        auto it = std::find(fallbackOrder.begin(), fallbackOrder.end(), current);
        if (it != fallbackOrder.end() && std::next(it) != fallbackOrder.end()) {
            return *std::next(it);
        }
        return std::nullopt;
    }
    
    /**
     * @brief Check if a given option is acceptable according to preferences.
     * 
     * @param option The option to check
     * @return true if the option is either preferred or in fallback list
     */
    bool isAcceptable(const T& option) const {
        if (option == preferred) return true;
        return std::find(fallbackOrder.begin(), fallbackOrder.end(), option) != fallbackOrder.end();
    }
};

/**
 * @brief Complete set of parameter preferences with fallback options.
 */
struct NegotiationPreferences {
    ParameterPreference<DataFormat> dataFormat{};
    ParameterPreference<CompressionAlgorithm> compression{};
    ParameterPreference<ErrorCorrectionScheme> errorCorrection{};
    std::string minProtocolVersion{"1.0.0"};
    
    /**
     * @brief Generate fallback parameters based on current parameters.
     * 
     * @param current Current parameters that were rejected
     * @param attempt Current fallback attempt number
     * @return Next set of parameters to try, or std::nullopt if no more fallbacks
     */
    std::optional<NegotiableParams> generateFallback(
        const NegotiableParams& current,
        std::size_t attempt) const;
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
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>
#include "xenocomm/core/protocol_variant.hpp"

namespace xenocomm {
namespace extensions {

/**
 * @brief Class responsible for checking compatibility between protocol variants
 * 
 * The CompatibilityChecker ensures that protocol variants can safely coexist
 * and prevents harmful protocol divergence. It validates various aspects of
 * compatibility including message formats, state transitions, and version
 * requirements.
 */
class CompatibilityChecker {
public:
    /**
     * @brief Result of a compatibility check
     */
    struct CompatibilityResult {
        bool isCompatible;                    ///< Whether the variants are compatible
        std::vector<std::string> conflicts;   ///< List of identified conflicts
        std::vector<std::string> warnings;    ///< Non-critical compatibility warnings
    };

    /**
     * @brief Constructor
     */
    CompatibilityChecker();

    /**
     * @brief Destructor
     */
    virtual ~CompatibilityChecker() = default;

    /**
     * @brief Check if a new variant is compatible with existing active variants
     * 
     * @param newVariant The variant to check for compatibility
     * @param activeVariants Currently active variants to check against
     * @return CompatibilityResult containing compatibility status and details
     */
    CompatibilityResult checkCompatibility(
        const ProtocolVariant& newVariant,
        const std::vector<ProtocolVariant>& activeVariants
    );

    /**
     * @brief Validate that a set of variants can coexist
     * 
     * @param variants Set of variants to validate for mutual compatibility
     * @return true if all variants are mutually compatible, false otherwise
     */
    bool validateVariantSet(const std::vector<ProtocolVariant>& variants);

    /**
     * @brief Configure compatibility checking rules
     * 
     * @param config JSON configuration for compatibility rules
     */
    void configure(const nlohmann::json& config);

private:
    /**
     * @brief Check compatibility of message formats between variants
     */
    bool checkMessageFormatCompatibility(
        const ProtocolVariant& v1,
        const ProtocolVariant& v2
    );

    /**
     * @brief Check compatibility of state transitions between variants
     */
    bool checkStateTransitionCompatibility(
        const ProtocolVariant& v1,
        const ProtocolVariant& v2
    );

    /**
     * @brief Check version compatibility between variants
     */
    bool checkVersionCompatibility(
        const ProtocolVariant& v1,
        const ProtocolVariant& v2
    );

    /**
     * @brief Check if changes in one variant conflict with another
     */
    std::vector<std::string> findConflicts(
        const ProtocolVariant& v1,
        const ProtocolVariant& v2
    );

    /**
     * @brief Identify potential compatibility warnings
     */
    std::vector<std::string> findWarnings(
        const ProtocolVariant& v1,
        const ProtocolVariant& v2
    );

    // Configuration for compatibility rules
    nlohmann::json config_;
};

} // namespace extensions
} // namespace xenocomm 
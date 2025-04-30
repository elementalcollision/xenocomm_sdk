#include "xenocomm/extensions/compatibility_checker.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace xenocomm {
namespace extensions {

CompatibilityChecker::CompatibilityChecker() {
    // Initialize default configuration
    config_ = {
        {"version_check", true},
        {"message_format_check", true},
        {"state_transition_check", true},
        {"min_version_gap", 1},
        {"max_version_gap", 3}
    };
}

CompatibilityResult CompatibilityChecker::checkCompatibility(
    const ProtocolVariant& newVariant,
    const std::vector<ProtocolVariant>& activeVariants
) {
    CompatibilityResult result{true, {}, {}};

    // Check compatibility with each active variant
    for (const auto& activeVariant : activeVariants) {
        // Check version compatibility
        if (config_["version_check"].get<bool>() && 
            !checkVersionCompatibility(newVariant, activeVariant)) {
            result.isCompatible = false;
            result.conflicts.push_back(
                "Version incompatibility between " + newVariant.getId() + 
                " and " + activeVariant.getId()
            );
        }

        // Check message format compatibility
        if (config_["message_format_check"].get<bool>() && 
            !checkMessageFormatCompatibility(newVariant, activeVariant)) {
            result.isCompatible = false;
            result.conflicts.push_back(
                "Message format incompatibility between " + newVariant.getId() + 
                " and " + activeVariant.getId()
            );
        }

        // Check state transition compatibility
        if (config_["state_transition_check"].get<bool>() && 
            !checkStateTransitionCompatibility(newVariant, activeVariant)) {
            result.isCompatible = false;
            result.conflicts.push_back(
                "State transition incompatibility between " + newVariant.getId() + 
                " and " + activeVariant.getId()
            );
        }

        // Find additional conflicts
        auto conflicts = findConflicts(newVariant, activeVariant);
        result.conflicts.insert(
            result.conflicts.end(),
            conflicts.begin(),
            conflicts.end()
        );

        // Find warnings
        auto warnings = findWarnings(newVariant, activeVariant);
        result.warnings.insert(
            result.warnings.end(),
            warnings.begin(),
            warnings.end()
        );
    }

    return result;
}

bool CompatibilityChecker::validateVariantSet(
    const std::vector<ProtocolVariant>& variants
) {
    // Check each pair of variants for compatibility
    for (size_t i = 0; i < variants.size(); ++i) {
        for (size_t j = i + 1; j < variants.size(); ++j) {
            std::vector<ProtocolVariant> others = {variants[j]};
            auto result = checkCompatibility(variants[i], others);
            if (!result.isCompatible) {
                return false;
            }
        }
    }
    return true;
}

void CompatibilityChecker::configure(const nlohmann::json& config) {
    // Validate and merge configuration
    if (config.contains("version_check")) {
        config_["version_check"] = config["version_check"];
    }
    if (config.contains("message_format_check")) {
        config_["message_format_check"] = config["message_format_check"];
    }
    if (config.contains("state_transition_check")) {
        config_["state_transition_check"] = config["state_transition_check"];
    }
    if (config.contains("min_version_gap")) {
        config_["min_version_gap"] = config["min_version_gap"];
    }
    if (config.contains("max_version_gap")) {
        config_["max_version_gap"] = config["max_version_gap"];
    }
}

bool CompatibilityChecker::checkMessageFormatCompatibility(
    const ProtocolVariant& v1,
    const ProtocolVariant& v2
) {
    // Get message formats from both variants
    const auto& format1 = v1.getMessageFormat();
    const auto& format2 = v2.getMessageFormat();

    // Check if formats are compatible
    // This is a basic implementation - extend based on actual message format structure
    return format1.isCompatibleWith(format2);
}

bool CompatibilityChecker::checkStateTransitionCompatibility(
    const ProtocolVariant& v1,
    const ProtocolVariant& v2
) {
    // Get state transition rules from both variants
    const auto& transitions1 = v1.getStateTransitions();
    const auto& transitions2 = v2.getStateTransitions();

    // Check if state transitions are compatible
    // This is a basic implementation - extend based on actual state transition structure
    return transitions1.isCompatibleWith(transitions2);
}

bool CompatibilityChecker::checkVersionCompatibility(
    const ProtocolVariant& v1,
    const ProtocolVariant& v2
) {
    // Get version information
    auto version1 = v1.getVersion();
    auto version2 = v2.getVersion();

    // Calculate version gap
    int versionGap = std::abs(version1 - version2);

    // Check if version gap is within acceptable range
    return versionGap >= config_["min_version_gap"].get<int>() &&
           versionGap <= config_["max_version_gap"].get<int>();
}

std::vector<std::string> CompatibilityChecker::findConflicts(
    const ProtocolVariant& v1,
    const ProtocolVariant& v2
) {
    std::vector<std::string> conflicts;

    // Check for conflicting changes in protocol behavior
    auto changes1 = v1.getChanges();
    auto changes2 = v2.getChanges();

    // Example conflict check - extend based on actual change structure
    for (const auto& change1 : changes1) {
        for (const auto& change2 : changes2) {
            if (change1.conflictsWith(change2)) {
                conflicts.push_back(
                    "Conflicting changes: " + change1.description() + 
                    " conflicts with " + change2.description()
                );
            }
        }
    }

    return conflicts;
}

std::vector<std::string> CompatibilityChecker::findWarnings(
    const ProtocolVariant& v1,
    const ProtocolVariant& v2
) {
    std::vector<std::string> warnings;

    // Check for potential issues that don't prevent compatibility
    // but should be noted

    // Example: Check for overlapping functionality
    if (v1.hasOverlappingFunctionality(v2)) {
        warnings.push_back(
            "Variants " + v1.getId() + " and " + v2.getId() + 
            " have overlapping functionality"
        );
    }

    // Example: Check for performance implications
    if (v1.hasPerformanceImpact() && v2.hasPerformanceImpact()) {
        warnings.push_back(
            "Multiple variants with performance impact may affect system behavior"
        );
    }

    return warnings;
}

} // namespace extensions
} // namespace xenocomm 
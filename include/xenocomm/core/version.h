#pragma once

#include <cstdint>
#include <string>

namespace xenocomm {
namespace core {

/**
 * @brief Represents a semantic version number with major, minor, and patch components.
 * 
 * This class implements semantic versioning (SemVer) rules:
 * - Major version changes indicate breaking changes
 * - Minor version changes indicate backward-compatible feature additions
 * - Patch version changes indicate backward-compatible bug fixes
 */
struct Version {
    uint16_t major{0};
    uint16_t minor{0};
    uint16_t patch{0};

    Version() = default;
    Version(uint16_t maj, uint16_t min, uint16_t pat) 
        : major(maj), minor(min), patch(pat) {}

    /**
     * @brief Checks if this version is compatible with the required version.
     * 
     * Compatibility rules:
     * 1. Major versions must match (breaking changes)
     * 2. Minor version must be >= required (backward compatible additions)
     * 3. If minor versions match, patch must be >= required
     * 
     * @param required The version that must be satisfied
     * @return true if this version is compatible with the required version
     */
    bool isCompatibleWith(const Version& required) const {
        // Major versions must match for compatibility
        if (major != required.major) {
            return false;
        }
        
        // Minor version must be >= required
        if (minor < required.minor) {
            return false;
        }
        
        // If minor versions match, patch must be >= required
        if (minor == required.minor && patch < required.patch) {
            return false;
        }
        
        return true;
    }

    /**
     * @brief Checks if this version satisfies the required version with flexible matching.
     * 
     * Flexible matching rules:
     * 1. Major version can be higher (assumes potential backward compatibility)
     * 2. For same major version, follows standard compatibility rules
     * 
     * @param required The version that must be satisfied
     * @return true if this version satisfies the required version
     */
    bool satisfies(const Version& required) const {
        // If major versions match, use standard compatibility rules
        if (major == required.major) {
            return isCompatibleWith(required);
        }
        
        // If our major version is higher, assume we can satisfy lower versions
        return major > required.major;
    }

    /**
     * @brief Checks if this version is newer than another version.
     * 
     * @param other The version to compare against
     * @return true if this version is newer
     */
    bool isNewerThan(const Version& other) const {
        return *this > other;
    }

    /**
     * @brief Creates a string representation of the version.
     * 
     * @return A string in the format "major.minor.patch"
     */
    std::string toString() const {
        return std::to_string(major) + "." + 
               std::to_string(minor) + "." + 
               std::to_string(patch);
    }

    // Standard comparison operators
    bool operator<(const Version& other) const {
        if (major != other.major) return major < other.major;
        if (minor != other.minor) return minor < other.minor;
        return patch < other.patch;
    }

    bool operator>(const Version& other) const {
        return other < *this;
    }

    bool operator==(const Version& other) const {
        return major == other.major && 
               minor == other.minor && 
               patch == other.patch;
    }

    bool operator!=(const Version& other) const {
        return !(*this == other);
    }

    bool operator<=(const Version& other) const {
        return !(other < *this);
    }

    bool operator>=(const Version& other) const {
        return !(*this < other);
    }
};

} // namespace core
} // namespace xenocomm 
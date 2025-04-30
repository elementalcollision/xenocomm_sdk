#ifndef XENOCOMM_CORE_CAPABILITY_SIGNALER_H
#define XENOCOMM_CORE_CAPABILITY_SIGNALER_H

#include "xenocomm/core/version.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint> // For uint16_t
#include <optional>

namespace xenocomm {
namespace core {

/**
 * @brief Represents the version of a capability.
 */
struct CapabilityVersion {
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;

    // Basic comparison operator for simplicity
    bool operator==(const CapabilityVersion& other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }
    // Add other comparison operators as needed (<, >, etc.)
    bool operator<(const CapabilityVersion& other) const {
        if (major != other.major) return major < other.major;
        if (minor != other.minor) return minor < other.minor;
        return patch < other.patch;
    }
};

/**
 * @brief Represents a capability that an agent can possess or require.
 */
struct Capability {
    std::string name;                                    // Capability name
    Version version;                                     // Capability version
    std::map<std::string, std::string> parameters;      // Configuration parameters
    bool is_deprecated{false};                          // Whether this capability is deprecated
    std::optional<Version> deprecated_since;            // Version when deprecation started
    std::optional<Version> removal_version;             // Version when capability will be removed
    std::optional<std::string> replacement_capability;  // Name of capability that replaces this one

    Capability() = default;

    Capability(std::string n, Version v, 
              std::map<std::string, std::string> params = {})
        : name(std::move(n)), version(std::move(v)), parameters(std::move(params)) {}

    /**
     * @brief Marks this capability as deprecated.
     * 
     * @param since Version when deprecation started
     * @param removal Version when capability will be removed (optional)
     * @param replacement Name of replacement capability (optional)
     */
    void deprecate(Version since, 
                  std::optional<Version> removal = std::nullopt,
                  std::optional<std::string> replacement = std::nullopt) {
        is_deprecated = true;
        deprecated_since = std::move(since);
        removal_version = std::move(removal);
        replacement_capability = std::move(replacement);
    }

    /**
     * @brief Checks if this capability matches another capability's requirements.
     * 
     * @param required The capability requirements to match against
     * @param allow_partial If true, allows more flexible version matching
     * @return true if this capability satisfies the requirements
     */
    bool matches(const Capability& required, bool allow_partial = false) const {
        // Names must match exactly
        if (name != required.name) {
            return false;
        }

        // Check version compatibility
        if (allow_partial) {
            if (!version.satisfies(required.version)) {
                return false;
            }
        } else {
            if (!version.isCompatibleWith(required.version)) {
                return false;
            }
        }

        // Check if required parameters are present with matching values
        for (const auto& [key, value] : required.parameters) {
            auto it = parameters.find(key);
            if (it == parameters.end() || it->second != value) {
                return false;
            }
        }

        return true;
    }

    // Basic comparison operator for containers
    bool operator==(const Capability& other) const {
        return name == other.name && version == other.version;
    }

    // Add operator< if needed for ordered sets
    bool operator<(const Capability& other) const {
        if (name != other.name) return name < other.name;
        return version < other.version;
    }
};

/**
 * @brief Interface for managing agent capability advertisement and discovery.
 * 
 * The CapabilitySignaler provides a mechanism for agents to advertise their capabilities
 * and discover other agents based on required capabilities. The implementation uses an
 * inverted index data structure for efficient capability matching.
 * 
 * The system supports both exact and partial capability matching:
 * - Exact matching (default): Agents must possess all required capabilities with exact name and version matches
 * - Partial matching: Agents must possess capabilities that partially match the required ones (e.g., matching
 *   capability names but potentially different versions)
 * 
 * Performance Characteristics:
 * - Registration: O(T) where T is the number of terms in the capability name
 * - Unregistration: O(T) where T is the number of terms in the capability name
 * - Discovery: O(C * log(A)) where C is the number of required capabilities and A is the number of agents
 *   with matching capabilities. The inverted index structure allows for fast filtering of agents based on
 *   capability terms.
 * - Retrieval: O(1) for getting an agent's capabilities
 * 
 * The inverted index maps capability terms (parts of the capability name) to agents that have
 * capabilities containing those terms. This allows for efficient partial matching and filtering
 * during discovery operations.
 */
class CapabilitySignaler {
public:
    /**
     * @brief Virtual destructor.
     */
    virtual ~CapabilitySignaler() = default;

    /**
     * @brief Registers a capability for a specific agent.
     * @param agentId The unique identifier of the agent.
     * @param capability The capability being registered.
     * @return True if registration was successful, false otherwise.
     */
    virtual bool registerCapability(const std::string& agentId, const Capability& capability) = 0;

    /**
     * @brief Unregisters a specific capability for an agent.
     * @param agentId The unique identifier of the agent.
     * @param capability The capability being unregistered.
     * @return True if unregistration was successful, false otherwise (e.g., capability not found).
     */
    virtual bool unregisterCapability(const std::string& agentId, const Capability& capability) = 0;

    /**
     * @brief Discovers agents that possess all the specified required capabilities using exact matching.
     * This is equivalent to calling discoverAgents(requiredCapabilities, false).
     * 
     * @param requiredCapabilities A list of capabilities that discovered agents must possess.
     * @return A list of agent IDs matching the requirements. Returns an empty list if none found.
     */
    virtual std::vector<std::string> discoverAgents(const std::vector<Capability>& requiredCapabilities) = 0;

    /**
     * @brief Discovers agents that possess all the specified required capabilities with optional partial matching.
     * 
     * @param requiredCapabilities A list of capabilities that discovered agents must possess.
     * @param partialMatch If true, allows partial matching of capabilities:
     *                     - Capability names must match exactly
     *                     - Version matching is more flexible (e.g., an agent with version 2.0.0 might
     *                       match a requirement for 1.0.0 if partial matching is enabled)
     *                     - Parameter matching may be more lenient
     *                     If false, requires exact matches for all capability attributes.
     * @return A list of agent IDs matching the requirements. Returns an empty list if none found.
     * 
     * @note Partial matching is useful in scenarios where:
     *       - You want to find agents with newer versions of a capability
     *       - You want to be more flexible with version requirements
     *       - You're interested in capability presence more than exact version matches
     */
    virtual std::vector<std::string> discoverAgents(
        const std::vector<Capability>& requiredCapabilities,
        bool partialMatch) = 0;

    /**
     * @brief Retrieves all capabilities registered for a specific agent.
     * @param agentId The unique identifier of the agent.
     * @return A list of capabilities registered by the agent. Returns an empty list if agent not found.
     */
    virtual std::vector<Capability> getAgentCapabilities(const std::string& agentId) = 0;

    /**
     * @brief Registers a capability for a specific agent using a binary representation.
     * @param agentId The unique identifier of the agent.
     * @param capabilityData The binary representation of the capability.
     * @return True if registration was successful (including successful deserialization), false otherwise.
     */
    virtual bool registerCapabilityBinary(const std::string& agentId, const std::vector<uint8_t>& capabilityData) = 0;

    /**
     * @brief Retrieves all capabilities registered for a specific agent in a combined binary format.
     * The format consists of a count of capabilities followed by size-prefixed capability blobs.
     * [uint32_t count] [uint32_t size1] [cap1_data] [uint32_t size2] [cap2_data] ...
     * @param agentId The unique identifier of the agent.
     * @return A byte vector containing the serialized capabilities. Returns an empty vector if agent not found or has no capabilities.
     */
    virtual std::vector<uint8_t> getAgentCapabilitiesBinary(const std::string& agentId) = 0;

protected:
    // Protected constructor for abstract base class
    CapabilitySignaler() = default;
};

} // namespace core
} // namespace xenocomm

namespace std {
    template <>
    struct hash<xenocomm::core::Capability> {
        size_t operator()(const xenocomm::core::Capability& cap) const noexcept {
            // Combine hashes of name and version. Parameters ignored for hashing simplicity.
            size_t h1 = std::hash<std::string>{}(cap.name);
            // Simple hash combination for version - could be improved
            size_t h_version = std::hash<uint16_t>{}(cap.version.major) ^ 
                               (std::hash<uint16_t>{}(cap.version.minor) << 1) ^
                               (std::hash<uint16_t>{}(cap.version.patch) << 2);
            return h1 ^ (h_version << 1); // Combine name and version hashes
        }
    };
} // namespace std

#endif // XENOCOMM_CORE_CAPABILITY_SIGNALER_H 
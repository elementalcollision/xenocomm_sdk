#pragma once

#include "xenocomm/core/capability_signaler.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include "xenocomm/core/version.h"
#include <functional>

namespace xenocomm {
namespace core {

/**
 * @brief An inverted index data structure for fast capability matching.
 * 
 * The CapabilityIndex maintains an inverted index mapping individual capabilities
 * to the agents that provide them. This enables efficient discovery of agents
 * based on required capabilities, with O(1) lookup time for each capability
 * and O(k) time for intersecting the results, where k is the number of matching agents.
 * 
 * The index supports two matching modes:
 * 1. Exact Matching:
 *    - Capability names must match exactly
 *    - Versions must match exactly
 *    - All required parameters must be present with exact values
 * 
 * 2. Partial Matching:
 *    - Capability names must still match exactly
 *    - Version matching is more flexible:
 *      * Higher versions can satisfy lower version requirements
 *      * Major version changes might still be considered compatible
 *    - Parameter matching is more lenient:
 *      * Additional parameters beyond those required are allowed
 *      * Parameter values may be matched with more flexibility
 * 
 * Use Cases:
 * - Exact matching is suitable for systems requiring strict compatibility
 * - Partial matching is useful for:
 *   * Finding newer versions of capabilities
 *   * Allowing backward compatibility
 *   * Supporting capability upgrades without breaking existing connections
 */
class CapabilityIndex {
public:
    /**
     * @brief Adds a capability for an agent to the index.
     * 
     * @param agentId The unique identifier of the agent.
     * @param capability The capability to index.
     * @return true if the capability was newly added, false if it was already present.
     * 
     * Time Complexity: O(1) amortized
     * 
     * @note The capability's name and version are used as keys in the index.
     *       Parameters are stored but don't affect the indexing structure.
     */
    bool addCapability(const std::string& agentId, const Capability& capability);

    /**
     * @brief Removes a capability for an agent from the index.
     * 
     * @param agentId The unique identifier of the agent.
     * @param capability The capability to remove.
     * @return true if the capability was removed, false if it wasn't found.
     * 
     * Time Complexity: O(1) amortized
     */
    bool removeCapability(const std::string& agentId, const Capability& capability);

    /**
     * @brief Removes all capabilities for an agent from the index.
     * 
     * @param agentId The unique identifier of the agent.
     * @return The number of capabilities that were removed.
     * 
     * Time Complexity: O(n) where n is the number of capabilities the agent had
     */
    size_t removeAgent(const std::string& agentId);

    /**
     * @brief Finds all agents that provide the required capabilities.
     * 
     * @param capabilities The set of required capabilities.
     * @param partialMatch If true, enables more flexible capability matching:
     *                     - Names must still match exactly
     *                     - Versions: Higher versions can satisfy lower version requirements
     *                       Example: Agent with v2.0.0 matches requirement for v1.0.0
     *                     - Parameters: Additional parameters are allowed, and matching
     *                       may be more lenient with parameter values
     *                     If false, requires exact matches for all capability attributes.
     * @return A vector of agent IDs that provide all the required capabilities.
     * 
     * Time Complexity: O(k) where k is the number of matching agents
     * 
     * Example:
     * ```cpp
     * // Agent registers: {"serviceA", {2, 0, 0}, {{"mode", "advanced"}}}
     * 
     * // Exact matching (partialMatch = false):
     * findAgents({{"serviceA", {1, 0, 0}}, false})  // No match
     * findAgents({{"serviceA", {2, 0, 0}}, false})  // Match
     * 
     * // Partial matching (partialMatch = true):
     * findAgents({{"serviceA", {1, 0, 0}}, true})   // Match (2.0.0 satisfies 1.0.0)
     * findAgents({{"serviceA", {3, 0, 0}}, true})   // No match (2.0.0 can't satisfy 3.0.0)
     * ```
     */
    std::vector<std::string> findAgents(
        const std::vector<Capability>& capabilities,
        bool partialMatch = false) const;

    /**
     * @brief Gets all capabilities registered for an agent.
     * 
     * @param agentId The unique identifier of the agent.
     * @return A vector of capabilities registered for the agent.
     * 
     * Time Complexity: O(n) where n is the number of capabilities the agent has
     */
    std::vector<Capability> getAgentCapabilities(const std::string& agentId) const;

    /**
     * @brief Clears all entries from the index.
     * 
     * Time Complexity: O(1)
     */
    void clear();

    /**
     * @brief Returns the total number of capability-agent mappings in the index.
     * 
     * Time Complexity: O(1)
     */
    size_t size() const;

private:
    // Maps capability name to a map of versions to sets of agent IDs
    std::unordered_map<
        std::string,  // Capability name
        std::unordered_map<
            Version,  // Version
            std::unordered_set<std::string>  // Agent IDs
        >
    > capabilityIndex_;

    // Maps agent ID to their set of capabilities
    std::unordered_map<
        std::string,  // Agent ID
        std::unordered_set<Capability>  // Capabilities
    > agentIndex_;
};

} // namespace core
} // namespace xenocomm 
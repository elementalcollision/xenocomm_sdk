#pragma once

#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/capability_cache.h"
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>

namespace xenocomm {
namespace core {

/**
 * @brief In-memory implementation of the CapabilitySignaler interface with caching support.
 * 
 * This implementation stores capabilities in memory and provides caching functionality
 * for faster lookups of frequently accessed capabilities.
 */
class InMemoryCapabilitySignaler : public CapabilitySignaler {
public:
    /**
     * @brief Constructs an InMemoryCapabilitySignaler with the specified cache configuration.
     * 
     * @param cache_config The configuration for the capability cache
     */
    explicit InMemoryCapabilitySignaler(const CacheConfig& cache_config);

    /**
     * @brief Virtual destructor.
     */
    ~InMemoryCapabilitySignaler() noexcept override = default;

    /**
     * @brief Registers a capability for a specific agent.
     * 
     * @param agentId The unique identifier of the agent
     * @param capability The capability to register
     * @return true if registration was successful, false otherwise
     */
    bool registerCapability(const std::string& agentId, const Capability& capability) override;

    /**
     * @brief Unregisters a capability from a specific agent.
     * 
     * @param agentId The unique identifier of the agent
     * @param capability The capability to unregister
     * @return true if unregistration was successful, false otherwise
     */
    bool unregisterCapability(const std::string& agentId, const Capability& capability) override;

    /**
     * @brief Discovers agents that possess all the specified required capabilities.
     * 
     * @param requiredCapabilities A list of capabilities that discovered agents must possess
     * @return std::vector<std::string> The IDs of agents matching the requirements
     */
    std::vector<std::string> discoverAgents(const std::vector<Capability>& requiredCapabilities) override;

    /**
     * @brief Discovers agents with optional partial matching of capabilities.
     * 
     * @param requiredCapabilities A list of capabilities that discovered agents must possess
     * @param partialMatch If true, allows partial matching of capabilities
     * @return std::vector<std::string> The IDs of agents matching the requirements
     */
    std::vector<std::string> discoverAgents(
        const std::vector<Capability>& requiredCapabilities,
        bool partialMatch) override;

    /**
     * @brief Retrieves all capabilities registered for a specific agent.
     * 
     * @param agentId The unique identifier of the agent
     * @return std::vector<Capability> The capabilities registered by the agent
     */
    std::vector<Capability> getAgentCapabilities(const std::string& agentId) override;

    /**
     * @brief Registers a capability using its binary representation.
     * 
     * @param agentId The unique identifier of the agent
     * @param capabilityData Binary representation of the capability
     * @return true if registration was successful, false otherwise
     */
    bool registerCapabilityBinary(const std::string& agentId, 
                                const std::vector<uint8_t>& capabilityData) override;

    /**
     * @brief Retrieves capabilities in binary format for a specific agent.
     * 
     * @param agentId The unique identifier of the agent
     * @return std::vector<uint8_t> Binary representation of the agent's capabilities
     */
    std::vector<uint8_t> getAgentCapabilitiesBinary(const std::string& agentId) override;

    // Get cache statistics
    CacheStats get_stats() const { return cache_.get_stats(); }

private:
    // Map of agent IDs to their capabilities
    std::unordered_map<std::string, std::vector<Capability>> agent_capabilities_;
    std::mutex mutex_;
    CapabilityCache cache_;
};

} // namespace core
} // namespace xenocomm 
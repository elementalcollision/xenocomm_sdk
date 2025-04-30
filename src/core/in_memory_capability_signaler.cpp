#include "xenocomm/core/in_memory_capability_signaler.h"
#include <sstream>

namespace xenocomm {
namespace core {

namespace {
// Helper function to generate a cache key from capabilities
std::string generate_cache_key(const std::vector<Capability>& capabilities) {
    std::ostringstream oss;
    for (const auto& cap : capabilities) {
        oss << cap.name << "|" << cap.version.major << "." 
            << cap.version.minor << "." << cap.version.patch << ";";
    }
    return oss.str();
}

// Helper function to serialize agent list to string
std::string serialize_agents(const std::vector<std::string>& agents) {
    std::ostringstream oss;
    for (const auto& agent : agents) {
        oss << agent << ";";
    }
    return oss.str();
}

// Helper function to deserialize agent list from string
std::vector<std::string> deserialize_agents(const std::string& data) {
    std::vector<std::string> agents;
    std::istringstream iss(data);
    std::string agent;
    while (std::getline(iss, agent, ';')) {
        if (!agent.empty()) {
            agents.push_back(agent);
        }
    }
    return agents;
}
} // anonymous namespace

InMemoryCapabilitySignaler::InMemoryCapabilitySignaler(const CacheConfig& cache_config)
    : cache_(cache_config) {}

bool InMemoryCapabilitySignaler::registerCapability(const std::string& agentId, const Capability& capability) {
    if (agentId.empty() || capability.name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    bool success = index_.addCapability(agentId, capability);
    if (success) {
        // Clear cache since the capability set has changed
        cache_.clear();
    }
    return success;
}

bool InMemoryCapabilitySignaler::unregisterCapability(const std::string& agentId, const Capability& capability) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool success = index_.removeCapability(agentId, capability);
    if (success) {
        // Clear cache since the capability set has changed
        cache_.clear();
    }
    return success;
}

void InMemoryCapabilitySignaler::unregisterAgent(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t removed = index_.removeAgent(agentId);
    if (removed > 0) {
        // Clear cache if any capabilities were removed
        cache_.clear();
    }
}

std::vector<std::string> InMemoryCapabilitySignaler::discoverAgents(
    const std::vector<Capability>& requiredCapabilities) {
    return discoverAgents(requiredCapabilities, false);
}

std::vector<std::string> InMemoryCapabilitySignaler::discoverAgents(
    const std::vector<Capability>& requiredCapabilities,
    bool partialMatch) {
    if (requiredCapabilities.empty()) {
        return {};
    }

    // Only use cache for exact matches
    if (!partialMatch) {
        std::string cache_key = generate_cache_key(requiredCapabilities);
        if (auto cached = cache_.get(cache_key)) {
            return deserialize_agents(*cached);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto result = index_.findAgents(requiredCapabilities, partialMatch);

    // Cache the result for exact matches
    if (!partialMatch) {
        std::string cache_key = generate_cache_key(requiredCapabilities);
        cache_.put(cache_key, serialize_agents(result));
    }

    return result;
}

std::vector<Capability> InMemoryCapabilitySignaler::getAgentCapabilities(const std::string& agentId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.getAgentCapabilities(agentId);
}

bool InMemoryCapabilitySignaler::registerCapabilityBinary(
    const std::string& agentId,
    const std::vector<uint8_t>& capabilityData) {
    try {
        auto capability = utils::deserialize<Capability>(capabilityData);
        return registerCapability(agentId, capability);
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<uint8_t> InMemoryCapabilitySignaler::getAgentCapabilitiesBinary(const std::string& agentId) {
    auto capabilities = getAgentCapabilities(agentId);
    return utils::serialize(capabilities);
}

CacheStats InMemoryCapabilitySignaler::getCacheStats() const {
    return cache_.get_stats();
}

} // namespace core
} // namespace xenocomm 
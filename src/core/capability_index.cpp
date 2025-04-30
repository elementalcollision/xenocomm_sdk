#include "xenocomm/core/capability_index.h"
#include <algorithm>
#include <stdexcept>

namespace xenocomm {
namespace core {

bool CapabilityIndex::addCapability(const std::string& agentId, const Capability& capability) {
    // Add to agent index
    auto [agentIt, agentInserted] = agentIndex_[agentId].insert(capability);
    if (!agentInserted) {
        // Capability was already registered for this agent
        return false;
    }

    // Add to capability index
    auto& versionMap = capabilityIndex_[capability.name];
    auto& agentSet = versionMap[capability.version];
    agentSet.insert(agentId);

    return true;
}

bool CapabilityIndex::removeCapability(const std::string& agentId, const Capability& capability) {
    // Remove from agent index
    auto agentIt = agentIndex_.find(agentId);
    if (agentIt == agentIndex_.end()) {
        return false;
    }

    auto& agentCaps = agentIt->second;
    if (!agentCaps.erase(capability)) {
        return false;
    }

    // Remove from capability index
    auto capIt = capabilityIndex_.find(capability.name);
    if (capIt != capabilityIndex_.end()) {
        auto& versionMap = capIt->second;
        auto verIt = versionMap.find(capability.version);
        if (verIt != versionMap.end()) {
            verIt->second.erase(agentId);

            // Clean up empty sets
            if (verIt->second.empty()) {
                versionMap.erase(verIt);
                if (versionMap.empty()) {
                    capabilityIndex_.erase(capIt);
                }
            }
        }
    }

    // Clean up empty agent entry
    if (agentCaps.empty()) {
        agentIndex_.erase(agentIt);
    }

    return true;
}

size_t CapabilityIndex::removeAgent(const std::string& agentId) {
    auto agentIt = agentIndex_.find(agentId);
    if (agentIt == agentIndex_.end()) {
        return 0;
    }

    size_t removedCount = 0;
    const auto& capabilities = agentIt->second;

    // Remove agent from all capability entries
    for (const auto& cap : capabilities) {
        auto capIt = capabilityIndex_.find(cap.name);
        if (capIt != capabilityIndex_.end()) {
            auto& versionMap = capIt->second;
            auto verIt = versionMap.find(cap.version);
            if (verIt != versionMap.end()) {
                verIt->second.erase(agentId);
                removedCount++;

                // Clean up empty sets
                if (verIt->second.empty()) {
                    versionMap.erase(verIt);
                    if (versionMap.empty()) {
                        capabilityIndex_.erase(capIt);
                    }
                }
            }
        }
    }

    // Remove agent entry
    agentIndex_.erase(agentIt);

    return removedCount;
}

std::vector<std::string> CapabilityIndex::findAgents(
    const std::vector<Capability>& capabilities,
    bool partialMatch) const {
    
    if (capabilities.empty()) {
        return {};
    }

    // Start with agents matching the first capability
    std::unordered_set<std::string> result;
    bool firstCapability = true;

    for (const auto& cap : capabilities) {
        auto capIt = capabilityIndex_.find(cap.name);
        if (capIt == capabilityIndex_.end()) {
            return {};  // Required capability not found
        }

        const auto& versionMap = capIt->second;
        std::unordered_set<std::string> matchingAgents;

        // Find agents with compatible versions
        for (const auto& [version, agents] : versionMap) {
            if (version >= cap.version) {  // Version compatibility check
                matchingAgents.insert(agents.begin(), agents.end());
            }
        }

        if (matchingAgents.empty()) {
            return {};  // No agents with compatible version
        }

        // For partial matching, verify parameters
        if (partialMatch) {
            std::unordered_set<std::string> validAgents;
            for (const auto& agentId : matchingAgents) {
                auto agentIt = agentIndex_.find(agentId);
                if (agentIt != agentIndex_.end()) {
                    // Check if agent has a capability that includes all required parameters
                    for (const auto& agentCap : agentIt->second) {
                        if (agentCap.name == cap.name && 
                            agentCap.version >= cap.version &&
                            std::includes(
                                agentCap.parameters.begin(), agentCap.parameters.end(),
                                cap.parameters.begin(), cap.parameters.end())) {
                            validAgents.insert(agentId);
                            break;
                        }
                    }
                }
            }
            matchingAgents = std::move(validAgents);
        }

        if (firstCapability) {
            result = std::move(matchingAgents);
            firstCapability = false;
        } else {
            // Intersect with previous results
            std::unordered_set<std::string> intersection;
            for (const auto& agentId : matchingAgents) {
                if (result.find(agentId) != result.end()) {
                    intersection.insert(agentId);
                }
            }
            result = std::move(intersection);
        }

        if (result.empty()) {
            return {};  // No agents match all capabilities so far
        }
    }

    return std::vector<std::string>(result.begin(), result.end());
}

std::vector<Capability> CapabilityIndex::getAgentCapabilities(const std::string& agentId) const {
    auto it = agentIndex_.find(agentId);
    if (it == agentIndex_.end()) {
        return {};
    }

    return std::vector<Capability>(it->second.begin(), it->second.end());
}

void CapabilityIndex::clear() {
    capabilityIndex_.clear();
    agentIndex_.clear();
}

size_t CapabilityIndex::size() const {
    size_t total = 0;
    for (const auto& [_, versionMap] : capabilityIndex_) {
        for (const auto& [_, agents] : versionMap) {
            total += agents.size();
        }
    }
    return total;
}

} // namespace core
} // namespace xenocomm 
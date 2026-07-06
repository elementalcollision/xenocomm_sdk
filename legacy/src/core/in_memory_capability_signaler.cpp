#include "xenocomm/core/in_memory_capability_signaler.hpp"
#include <sstream>
#include <algorithm>
#include <set>

namespace xenocomm {
namespace core {

// InMemoryCapabilityIndex implementation
bool InMemoryCapabilityIndex::addCapability(const std::string& agentId, const Capability& capability) {
    // Check if agent already has this capability
    auto it = agentCapabilities.find(agentId);
    if (it != agentCapabilities.end()) {
        // Check for duplicates
        for (const auto& cap : it->second) {
            if (cap.name == capability.name && cap.version == capability.version) {
                return false; // Already registered
            }
        }
        // Add new capability
        it->second.push_back(capability);
    } else {
        // Create new agent entry
        agentCapabilities[agentId] = {capability};
    }
    
    // Update inverted index
    auto nameIt = capabilityNameIndex.find(capability.name);
    if (nameIt != capabilityNameIndex.end()) {
        // Add agent ID if not already in the list
        if (std::find(nameIt->second.begin(), nameIt->second.end(), agentId) == nameIt->second.end()) {
            nameIt->second.push_back(agentId);
        }
    } else {
        // Create new index entry
        capabilityNameIndex[capability.name] = {agentId};
    }
    
    return true;
}

bool InMemoryCapabilityIndex::removeCapability(const std::string& agentId, const Capability& capability) {
    // Find agent's capabilities
    auto it = agentCapabilities.find(agentId);
    if (it == agentCapabilities.end()) {
        return false; // Agent not found
    }
    
    // Find and remove the capability
    auto& caps = it->second;
    auto capIt = std::find_if(caps.begin(), caps.end(), [&capability](const Capability& cap) {
        return cap.name == capability.name && cap.version == capability.version;
    });
    
    if (capIt == caps.end()) {
        return false; // Capability not found
    }
    
    // Remove capability from agent's list
    caps.erase(capIt);
    
    // Update inverted index
    auto nameIt = capabilityNameIndex.find(capability.name);
    if (nameIt != capabilityNameIndex.end()) {
        // Check if this was the agent's only capability with this name
        bool hasOtherCapWithSameName = false;
        for (const auto& cap : caps) {
            if (cap.name == capability.name) {
                hasOtherCapWithSameName = true;
                break;
            }
        }
        
        // Remove agent from index if it no longer has capabilities with this name
        if (!hasOtherCapWithSameName) {
            auto& agents = nameIt->second;
            agents.erase(std::remove(agents.begin(), agents.end(), agentId), agents.end());
            
            // Remove empty index entries
            if (agents.empty()) {
                capabilityNameIndex.erase(nameIt);
            }
        }
    }
    
    // Remove agent if it has no more capabilities
    if (caps.empty()) {
        agentCapabilities.erase(it);
    }
    
    return true;
}

size_t InMemoryCapabilityIndex::removeAgent(const std::string& agentId) {
    // Find agent's capabilities
    auto it = agentCapabilities.find(agentId);
    if (it == agentCapabilities.end()) {
        return 0; // Agent not found
    }
    
    size_t removedCount = it->second.size();
    
    // Remove agent from all capability indexes
    for (const auto& cap : it->second) {
        auto nameIt = capabilityNameIndex.find(cap.name);
        if (nameIt != capabilityNameIndex.end()) {
            auto& agents = nameIt->second;
            agents.erase(std::remove(agents.begin(), agents.end(), agentId), agents.end());
            
            // Remove empty index entries
            if (agents.empty()) {
                capabilityNameIndex.erase(nameIt);
            }
        }
    }
    
    // Remove agent from main map
    agentCapabilities.erase(it);
    
    return removedCount;
}

std::vector<std::string> InMemoryCapabilityIndex::findAgents(
    const std::vector<Capability>& capabilities, 
    bool partialMatch) const {
    
    if (capabilities.empty()) {
        return {};
    }
    
    // First, find candidates that have all capability names
    std::set<std::string> candidates;
    bool firstCapability = true;
    
    for (const auto& cap : capabilities) {
        auto nameIt = capabilityNameIndex.find(cap.name);
        if (nameIt == capabilityNameIndex.end()) {
            return {}; // No agents have this capability name
        }
        
        std::set<std::string> capAgents(nameIt->second.begin(), nameIt->second.end());
        
        if (firstCapability) {
            candidates = capAgents;
            firstCapability = false;
        } else {
            // Keep only agents that have all capabilities so far
            std::set<std::string> intersection;
            std::set_intersection(
                candidates.begin(), candidates.end(),
                capAgents.begin(), capAgents.end(),
                std::inserter(intersection, intersection.begin())
            );
            candidates = intersection;
            
            if (candidates.empty()) {
                return {}; // No agents have all required capabilities
            }
        }
    }
    
    // Filter candidates by version and parameters
    std::vector<std::string> result;
    
    for (const auto& agentId : candidates) {
        bool matchesAll = true;
        
        // Get agent's capabilities
        const auto& agentCaps = agentCapabilities.at(agentId);
        
        // Check if agent has all required capabilities
        for (const auto& requiredCap : capabilities) {
            bool hasMatchingCap = false;
            
            for (const auto& agentCap : agentCaps) {
                if (agentCap.matches(requiredCap, partialMatch)) {
                    hasMatchingCap = true;
                    break;
                }
            }
            
            if (!hasMatchingCap) {
                matchesAll = false;
                break;
            }
        }
        
        if (matchesAll) {
            result.push_back(agentId);
        }
    }
    
    return result;
}

std::vector<Capability> InMemoryCapabilityIndex::getAgentCapabilities(const std::string& agentId) const {
    auto it = agentCapabilities.find(agentId);
    if (it == agentCapabilities.end()) {
        return {}; // Agent not found
    }
    
    return it->second;
}

// Cache implementation
Cache::Cache(const CacheConfig& config)
    : config_(config) {
    stats_.max_size = config.max_size;
}

void Cache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    
    // Check if we need to evict entries to make room
    if (data_.size() >= config_.max_size) {
        evict();
    }
    
    // Update or insert the entry
    auto it = data_.find(key);
    if (it != data_.end()) {
        // Update existing entry
        it->second.first = value;
        it->second.second = now;
        update_lru(key);
    } else {
        // Insert new entry
        data_[key] = {value, now};
        lru_order_.push_back(key);
        stats_.size = data_.size();
    }
}

std::optional<std::string> Cache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = data_.find(key);
    if (it == data_.end()) {
        if (config_.enable_stats) {
            stats_.misses++;
        }
        return std::nullopt;
    }
    
    // Check if entry has expired
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.second > config_.ttl) {
        data_.erase(it);
        
        // Remove from LRU order
        auto lru_it = std::find(lru_order_.begin(), lru_order_.end(), key);
        if (lru_it != lru_order_.end()) {
            lru_order_.erase(lru_it);
        }
        
        stats_.size = data_.size();
        stats_.evictions++;
        stats_.last_eviction = now;
        
        if (config_.enable_stats) {
            stats_.misses++;
        }
        
        return std::nullopt;
    }
    
    // Entry is valid, update LRU and return
    update_lru(key);
    
    if (config_.enable_stats) {
        stats_.hits++;
    }
    
    return it->second.first;
}

void Cache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
    lru_order_.clear();
    stats_.size = 0;
}

CacheStats Cache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void Cache::evict() {
    // Ensure we have entries to evict
    if (lru_order_.empty()) {
        return;
    }
    
    // Remove the least recently used entry
    const std::string& key = lru_order_.front();
    data_.erase(key);
    lru_order_.erase(lru_order_.begin());
    
    stats_.evictions++;
    stats_.last_eviction = std::chrono::steady_clock::now();
    stats_.size = data_.size();
}

void Cache::update_lru(const std::string& key) {
    // Remove key from its current position in LRU list
    auto it = std::find(lru_order_.begin(), lru_order_.end(), key);
    if (it != lru_order_.end()) {
        lru_order_.erase(it);
    }
    
    // Add key to the end of LRU list (most recently used)
    lru_order_.push_back(key);
}

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
        Capability capability;
        if (!utils::deserializeCapability(capabilityData.data(), capabilityData.size(), capability)) {
            return false;
        }
        return registerCapability(agentId, capability);
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<uint8_t> InMemoryCapabilitySignaler::getAgentCapabilitiesBinary(const std::string& agentId) {
    auto capabilities = getAgentCapabilities(agentId);
    std::vector<uint8_t> result;
    
    // Serialize all capabilities into the result vector
    for (const auto& capability : capabilities) {
        utils::serializeCapability(capability, result);
    }
    
    return result;
}

CacheStats InMemoryCapabilitySignaler::getCacheStats() const {
    return cache_.get_stats();
}

} // namespace core
} // namespace xenocomm 
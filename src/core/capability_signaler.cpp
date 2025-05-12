#include "xenocomm/core/version.h" // Ensure this is early
#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/capability_index.h"
#include "xenocomm/utils/serialization.h" // Added for binary serialization
#include "xenocomm/core/capability_cache.h" // Added

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <set> // Still needed for intermediate results potentially
#include <mutex>
#include <shared_mutex>
#include <algorithm> // For std::all_of
#include <stdexcept>
#include <sstream>
#include <memory>
#include <cstring> // For memcpy
#include <arpa/inet.h> // For htonl, ntohl - Assuming Linux/macOS

namespace xenocomm {
namespace core {

namespace {

// Helper function to split capability name into terms for indexing
std::vector<std::string> splitCapabilityName(const std::string& name) {
    std::vector<std::string> terms;
    std::stringstream ss(name);
    std::string term;
    while (std::getline(ss, term, '.')) {
        if (!term.empty()) {
            terms.push_back(term);
        }
    }
    return terms;
}

// Simple stringification for cache key
std::string capabilitiesKey(const std::vector<Capability>& caps) {
    std::string key;
    for (const auto& cap : caps) {
        key += cap.name + ":" + cap.version.toString() + ";"; // Use Version::toString()
        for (const auto& [k, v] : cap.parameters) {
            key += k + "=" + v + ",";
        }
        key += "|";
    }
    return key;
}

// Serialize a vector of Capability into a buffer
std::vector<uint8_t> serializeCapabilities(const std::vector<Capability>& caps) {
    std::vector<uint8_t> out;
    uint32_t count = static_cast<uint32_t>(caps.size());
    uint32_t count_be = htonl(count);
    out.insert(out.end(), reinterpret_cast<uint8_t*>(&count_be), reinterpret_cast<uint8_t*>(&count_be) + sizeof(count_be));
    for (const auto& cap : caps) {
        std::vector<uint8_t> capBuf;
        // Assuming serializeCapability exists in utils and returns void or throws
        xenocomm::utils::serializeCapability(cap, capBuf); 
        // Remove the 'if' check, assume success if no exception
        uint32_t sz = static_cast<uint32_t>(capBuf.size());
        uint32_t sz_be = htonl(sz);
        out.insert(out.end(), reinterpret_cast<uint8_t*>(&sz_be), reinterpret_cast<uint8_t*>(&sz_be) + sizeof(sz_be));
        out.insert(out.end(), capBuf.begin(), capBuf.end());
    }
    return out;
}

// Deserialize a vector of Capability from a buffer
bool deserializeCapabilities(const uint8_t* data, size_t size, std::vector<Capability>& outCaps) {
    if (size < 4) return false;
    uint32_t count = 0;
    std::memcpy(&count, data, 4);
    count = ntohl(count);
    size_t offset = 4;
    outCaps.clear();
    outCaps.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (offset + 4 > size) return false;
        uint32_t sz = 0;
        std::memcpy(&sz, data + offset, 4);
        sz = ntohl(sz);
        offset += 4;
        if (offset + sz > size) return false;
        Capability cap;
        size_t capBytes = 0;
        // Assuming deserializeCapability exists in utils
        if (!xenocomm::utils::deserializeCapability(data + offset, sz, cap, &capBytes) || capBytes != sz) { 
             return false;
        }
        outCaps.push_back(cap);
        offset += sz;
    }
    return offset == size; // Ensure all data was consumed
}

} // anonymous namespace

/**
 * @brief A concrete in-memory implementation of the CapabilitySignaler interface.
 *
 * This implementation uses unordered maps for efficient storage and retrieval.
 * It is designed to be thread-safe using shared mutexes for read/write locking.
 */
class InMemoryCapabilitySignaler : public CapabilitySignaler {
public:
    /**
     * @brief Constructor with cache configuration.
     */
    explicit InMemoryCapabilitySignaler(const CacheConfig& cacheConfig)
        : cache_(cacheConfig) {}

    /**
     * @brief Destructor.
     */
    ~InMemoryCapabilitySignaler() noexcept override = default;

    // Prevent copy and assignment
    InMemoryCapabilitySignaler(const InMemoryCapabilitySignaler&) = delete;
    InMemoryCapabilitySignaler& operator=(const InMemoryCapabilitySignaler&) = delete;

    // Explicitly delete move operations as Cache_ member is not movable
    InMemoryCapabilitySignaler(InMemoryCapabilitySignaler&&) = delete;
    InMemoryCapabilitySignaler& operator=(InMemoryCapabilitySignaler&&) = delete;

    bool registerCapability(const std::string& agentId, const Capability& capability) override {
        if (agentId.empty() || capability.name.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        bool success = index_.addCapability(agentId, capability);
        if (success) {
            cache_.clear();
        }
        return success;
    }

    bool unregisterCapability(const std::string& agentId, const Capability& capability) override {
        std::lock_guard<std::mutex> lock(mutex_);
        bool success = index_.removeCapability(agentId, capability);
        if (success) {
            cache_.clear();
        }
        return success;
    }

    void unregisterAgent(const std::string& agentId) /*override*/ {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t removed = index_.removeAgent(agentId);
        if (removed > 0) {
            cache_.clear();
        }
    }

    /**
     * @brief Discovers agents with exact capability matching.
     * 
     * This method is equivalent to calling discoverAgents(requiredCapabilities, false).
     * All capability attributes (name, version, parameters) must match exactly.
     */
    std::vector<std::string> discoverAgents(
        const std::vector<Capability>& requiredCapabilities) override {
        return discoverAgents(requiredCapabilities, false); // Delegate to the overload
    }

    /**
     * @brief Discovers agents with optional partial capability matching.
     */
    std::vector<std::string> discoverAgents(
        const std::vector<Capability>& requiredCapabilities,
        bool partialMatch) override {
        if (requiredCapabilities.empty()) {
            return {};
        }
        std::string key = capabilitiesKey(requiredCapabilities);
        // Only check cache for exact matches
        if (!partialMatch) {
            // Use get for cache lookup, which returns optional<string>
            if (auto cached = cache_.get(key)) { 
                std::vector<std::string> result;
                std::stringstream ss(*cached);
                std::string id;
                while (std::getline(ss, id, ',')) {
                    if (!id.empty()) result.push_back(id);
                }
                return result;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = index_.findAgents(requiredCapabilities, partialMatch);
        // Only cache results for exact matches
        if (!partialMatch) {
            std::string value;
            for (const auto& id : result) {
                if (!value.empty()) value += ",";
                value += id;
            }
            cache_.put(key, value); // Use cache_.put()
        }
        return result;
    }

    std::vector<Capability> getAgentCapabilities(const std::string& agentId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return index_.getAgentCapabilities(agentId);
    }

    bool registerCapabilityBinary(
        const std::string& agentId,
        const std::vector<uint8_t>& capabilityData) override {
        Capability cap;
        size_t bytesRead = 0;
        if (!xenocomm::utils::deserializeCapability(capabilityData.data(), capabilityData.size(), cap, &bytesRead)) {
            return false;
        }
        return registerCapability(agentId, cap);
    }

    std::vector<uint8_t> getAgentCapabilitiesBinary(const std::string& agentId) override {
        auto caps = getAgentCapabilities(agentId);
        return serializeCapabilities(caps);
    }

    // Public method to access cache stats (if needed for testing)
    CacheStats getCacheStats() const {
        return cache_.get_stats();
    }

private:
    CapabilityIndex index_;
    CapabilityCache cache_;
    std::mutex mutex_;
};

// Factory function implementation
std::unique_ptr<CapabilitySignaler> createInMemoryCapabilitySignaler() {
    // Use the constructor that takes CacheConfig, providing a default one
    return std::make_unique<InMemoryCapabilitySignaler>(CacheConfig{}); 
}

} // namespace core
} // namespace xenocomm 
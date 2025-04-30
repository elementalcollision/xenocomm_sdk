#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/capability_index.h"
#include "xenocomm/utils/serialization.h" // Added for binary serialization
#include "xenocomm/core/version.h"  // Added for Version type

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
     * @brief Default constructor.
     */
    InMemoryCapabilitySignaler() = default;

    /**
     * @brief Constructor with cache configuration.
     */
    explicit InMemoryCapabilitySignaler(const CacheConfig& cacheConfig)
        : cache_(cacheConfig) {}

    /**
     * @brief Destructor.
     */
    ~InMemoryCapabilitySignaler() override = default;

    // Prevent copy and assignment
    InMemoryCapabilitySignaler(const InMemoryCapabilitySignaler&) = delete;
    InMemoryCapabilitySignaler& operator=(const InMemoryCapabilitySignaler&) = delete;

    bool registerCapability(const std::string& agentId, const Capability& capability) override {
        if (agentId.empty() || capability.name.empty()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        bool success = index_.addCapability(agentId, capability);
        if (success) {
            // Invalidate any cached results that might be affected by this new capability
            cache_.clear();  // For simplicity, clear entire cache. Could be more selective.
        }
        return success;
    }

    bool unregisterCapability(const std::string& agentId, const Capability& capability) override {
        std::lock_guard<std::mutex> lock(mutex_);
        bool success = index_.removeCapability(agentId, capability);
        if (success) {
            // Invalidate any cached results that might be affected by this removal
            cache_.clear();  // For simplicity, clear entire cache. Could be more selective.
        }
        return success;
    }

    void unregisterAgent(const std::string& agentId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t removed = index_.removeAgent(agentId);
        if (removed > 0) {
            // Invalidate cache if any capabilities were removed
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
        if (requiredCapabilities.empty()) {
            return {};
        }

        // Try to get from cache first
        if (auto cached = cache_.get(requiredCapabilities)) {
            return *cached;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto result = index_.findAgents(requiredCapabilities, false);
        
        // Cache the result
        cache_.put(requiredCapabilities, result);
        
        return result;
    }

    /**
     * @brief Discovers agents with optional partial capability matching.
     * 
     * When partialMatch is true:
     * - Capability names must still match exactly
     * - Version matching is more flexible (e.g., newer versions may match older requirements)
     * - Parameter matching may be more lenient (e.g., additional parameters may be allowed)
     * 
     * Example:
     * Required: {"serviceA", {1, 0, 0}}
     * Agent has: {"serviceA", {2, 0, 0}}
     * - With partialMatch=false: No match
     * - With partialMatch=true: Match (newer version accepted)
     * 
     * @see CapabilityIndex::findAgents for the specific matching logic
     */
    std::vector<std::string> discoverAgents(
        const std::vector<Capability>& requiredCapabilities,
        bool partialMatch) override {
        if (requiredCapabilities.empty()) {
            return {};
        }

        // Only use cache for exact matches to avoid complexity with partial matching
        if (!partialMatch) {
            if (auto cached = cache_.get(requiredCapabilities)) {
                return *cached;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto result = index_.findAgents(requiredCapabilities, partialMatch);
        
        // Cache only exact matches
        if (!partialMatch) {
            cache_.put(requiredCapabilities, result);
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
        try {
            auto capability = utils::deserialize<Capability>(capabilityData);
            return registerCapability(agentId, capability);
        } catch (const std::exception& e) {
            return false;
        }
    }

    std::vector<uint8_t> getAgentCapabilitiesBinary(const std::string& agentId) override {
        auto capabilities = getAgentCapabilities(agentId);
        return utils::serialize(capabilities);
    }

    /**
     * @brief Get cache statistics.
     * @return Current cache statistics.
     */
    CacheStats getCacheStats() const {
        return cache_.getStats();
    }

    /**
     * @brief Reset cache statistics.
     */
    void resetCacheStats() {
        cache_.resetStats();
    }

private:
    CapabilityIndex index_;
    CapabilityCache cache_;
    std::mutex mutex_;
};

// --- We no longer need the placeholder implementations for the base class --- 
/*
bool CapabilitySignaler::registerCapability(const std::string& agentId, const Capability& capability) {
    // Placeholder: Implementation belongs in the concrete class
    return false;
}

bool CapabilitySignaler::unregisterCapability(const std::string& agentId, const Capability& capability) {
    // Placeholder: Implementation belongs in the concrete class
    return false;
}

std::vector<std::string> CapabilitySignaler::discoverAgents(const std::vector<Capability>& requiredCapabilities) {
    // Placeholder: Implementation belongs in the concrete class
    return {};
}

std::vector<Capability> CapabilitySignaler::getAgentCapabilities(const std::string& agentId) {
    // Placeholder: Implementation belongs in the concrete class
    return {};
}
*/

// --- Optional: Factory function --- 
// If needed elsewhere, a factory function could create instances:
std::unique_ptr<CapabilitySignaler> createInMemoryCapabilitySignaler() {
    return std::make_unique<InMemoryCapabilitySignaler>();
}

// CapabilityVersion implementation
bool CapabilityVersion::operator==(const CapabilityVersion& other) const {
    return major == other.major && minor == other.minor && patch == other.patch;
}

bool CapabilityVersion::operator<(const CapabilityVersion& other) const {
    if (major != other.major) return major < other.major;
    if (minor != other.minor) return minor < other.minor;
    return patch < other.patch;
}

// Capability implementation
bool Capability::operator==(const Capability& other) const {
    return name == other.name && version == other.version;
}

} // namespace core
} // namespace xenocomm 
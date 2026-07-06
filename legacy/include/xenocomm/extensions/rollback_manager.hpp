#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>
#include "xenocomm/core/protocol_variant.hpp"
#include "xenocomm/extensions/compatibility_checker.hpp"
#include <array>
#include <unordered_map>
#include <list>

namespace xenocomm {
namespace extensions {

struct StateChunk {
    std::string id;                    // Chunk identifier
    size_t offset;                     // Offset in the complete state
    std::vector<uint8_t> data;        // Chunk data
    std::string checksum;             // Chunk-level checksum
};

/**
 * @brief Represents a snapshot of protocol state for rollback purposes
 */
struct RollbackPoint {
    std::string id;                                    // Unique identifier for this rollback point
    std::chrono::system_clock::time_point timestamp;   // When the rollback point was created
    std::string variantId;                            // ID of the protocol variant
    nlohmann::json state;                             // Protocol state snapshot (for small states)
    std::vector<StateChunk> stateChunks;             // Chunked state data for large states
    std::string checksum;                             // Integrity checksum of the state
    std::map<std::string, std::string> metadata;      // Additional metadata
    bool isChunked;                                   // Whether the state is stored in chunks
};

/**
 * @brief Configuration for the RollbackManager
 */
struct RollbackConfig {
    size_t maxRollbackPoints = 10;                    // Maximum number of rollback points to keep
    std::chrono::hours retentionPeriod{24 * 7};      // How long to keep rollback points
    bool enableIncrementalSnapshots = true;           // Whether to use incremental snapshots
    size_t maxSnapshotSizeBytes = 1024 * 1024 * 100; // Maximum size of a snapshot (100MB)
    std::string storagePath = "rollbacks/";           // Where to store rollback data
    size_t chunkSize = 1024 * 1024;                  // Size of each chunk (1MB)
    size_t maxMemoryCache = 1024 * 1024 * 512;       // Maximum memory for caching (512MB)
    bool enableCompression = true;                    // Whether to compress chunks
};

/**
 * @brief Class responsible for managing protocol rollback points and state restoration
 * 
 * The RollbackManager ensures system stability by maintaining safe rollback points
 * that can be used to restore the system to a known good state if issues are
 * detected with a protocol variant.
 */
class RollbackManager {
public:
    /**
     * @brief Construct a new RollbackManager
     * 
     * @param config Configuration for the rollback manager
     * @param compatibilityChecker Reference to the compatibility checker
     */
    explicit RollbackManager(
        const RollbackConfig& config,
        std::shared_ptr<CompatibilityChecker> compatibilityChecker
    );

    /**
     * @brief Create a new rollback point
     * 
     * @param variantId ID of the protocol variant
     * @param state Current protocol state
     * @param metadata Additional metadata for the rollback point
     * @return std::string ID of the created rollback point
     */
    std::string createRollbackPoint(
        const std::string& variantId,
        const nlohmann::json& state,
        const std::map<std::string, std::string>& metadata = {}
    );

    /**
     * @brief Restore the system to a specific rollback point
     * 
     * @param rollbackId ID of the rollback point to restore to
     * @return bool True if restoration was successful
     */
    bool restoreToPoint(const std::string& rollbackId);

    /**
     * @brief Get information about a specific rollback point
     * 
     * @param rollbackId ID of the rollback point
     * @return std::optional<RollbackPoint> The rollback point if found
     */
    std::optional<RollbackPoint> getRollbackPoint(const std::string& rollbackId) const;

    /**
     * @brief List all available rollback points
     * 
     * @param variantId Optional variant ID to filter by
     * @return std::vector<RollbackPoint> List of rollback points
     */
    std::vector<RollbackPoint> listRollbackPoints(
        const std::string& variantId = ""
    ) const;

    /**
     * @brief Verify the integrity of a rollback point
     * 
     * @param rollbackId ID of the rollback point to verify
     * @return bool True if the rollback point is valid
     */
    bool verifyRollbackPoint(const std::string& rollbackId) const;

    /**
     * @brief Clean up old rollback points based on retention policy
     * 
     * @return size_t Number of rollback points removed
     */
    size_t cleanupOldRollbackPoints();

    /**
     * @brief Get the current configuration
     * 
     * @return const RollbackConfig& Current configuration
     */
    const RollbackConfig& getConfig() const { return config_; }

private:
    RollbackConfig config_;
    std::shared_ptr<CompatibilityChecker> compatibilityChecker_;
    std::map<std::string, RollbackPoint> rollbackPoints_;
    
    // Enhanced B-tree structures
    struct BTreeNode {
        static constexpr size_t ORDER = 128;  // Increased order for better branching
        std::array<std::string, 2 * ORDER - 1> keys;
        std::array<std::string, 2 * ORDER - 1> values;
        std::array<std::shared_ptr<BTreeNode>, 2 * ORDER> children;
        size_t keyCount;
        bool isLeaf;
        
        BTreeNode() : keyCount(0), isLeaf(true) {}
    };
    
    // LRU Cache for B-tree nodes
    class NodeCache {
    public:
        static constexpr size_t MAX_CACHE_SIZE = 1000;  // Adjust based on memory constraints
        
        std::shared_ptr<BTreeNode> get(const std::string& nodeId) {
            auto it = cache_.find(nodeId);
            if (it != cache_.end()) {
                // Move to front of LRU list
                lruList_.splice(lruList_.begin(), lruList_, it->second.second);
                return it->second.first;
            }
            return nullptr;
        }
        
        void put(const std::string& nodeId, std::shared_ptr<BTreeNode> node) {
            if (cache_.size() >= MAX_CACHE_SIZE) {
                // Remove least recently used
                auto last = lruList_.back();
                cache_.erase(last);
                lruList_.pop_back();
            }
            
            lruList_.push_front(nodeId);
            cache_[nodeId] = {node, lruList_.begin()};
        }
        
        void clear() {
            cache_.clear();
            lruList_.clear();
        }
        
    private:
        std::unordered_map<std::string, 
            std::pair<std::shared_ptr<BTreeNode>, 
                     std::list<std::string>::iterator>> cache_;
        std::list<std::string> lruList_;
    };
    
    std::shared_ptr<BTreeNode> btreeRoot_;
    mutable NodeCache nodeCache_;
    
    // Enhanced B-tree methods
    void insertIntoBTree(const std::string& key, const std::string& value);
    std::string searchBTree(const std::string& key);
    void optimizeBTree();
    void splitChild(std::shared_ptr<BTreeNode> parent, size_t index);
    void insertNonFull(std::shared_ptr<BTreeNode> node, const std::string& key, const std::string& value);
    void mergeNodes(std::shared_ptr<BTreeNode> parent, size_t index);
    std::string generateNodeId(const std::shared_ptr<BTreeNode>& node) const;
    void persistNode(const std::shared_ptr<BTreeNode>& node, const std::string& nodeId);
    std::shared_ptr<BTreeNode> loadNode(const std::string& nodeId);
    void rebalanceTree();
    size_t calculateOptimalOrder() const;
    
    // Bulk loading optimization
    void bulkLoadBTree(const std::vector<std::pair<std::string, std::string>>& sortedEntries);
    std::vector<std::shared_ptr<BTreeNode>> createLeafNodes(
        const std::vector<std::pair<std::string, std::string>>& sortedEntries);
    std::vector<std::shared_ptr<BTreeNode>> createInternalNodes(
        const std::vector<std::shared_ptr<BTreeNode>>& children);
    
    // New private methods for optimized storage
    std::vector<StateChunk> chunkifyState(const nlohmann::json& state) const;
    nlohmann::json reassembleState(const std::vector<StateChunk>& chunks) const;
    void compressChunk(StateChunk& chunk) const;
    void decompressChunk(StateChunk& chunk) const;
    std::string persistChunk(const StateChunk& chunk) const;
    StateChunk loadChunk(const std::string& chunkId) const;
    
    /**
     * @brief Calculate checksum for state data
     */
    std::string calculateChecksum(const nlohmann::json& state) const;

    /**
     * @brief Save rollback point to persistent storage
     */
    bool persistRollbackPoint(const RollbackPoint& point) const;

    /**
     * @brief Load rollback point from persistent storage
     */
    std::optional<RollbackPoint> loadRollbackPoint(const std::string& id) const;

    /**
     * @brief Generate a unique ID for a new rollback point
     */
    std::string generateRollbackId() const;

    /**
     * @brief Check if a rollback point should be retained
     */
    bool shouldRetainRollbackPoint(const RollbackPoint& point) const;

    /**
     * @brief Create an incremental snapshot if enabled
     */
    nlohmann::json createIncrementalSnapshot(
        const nlohmann::json& currentState,
        const nlohmann::json& previousState
    ) const;

    /**
     * @brief Apply an incremental snapshot to restore full state
     */
    nlohmann::json applyIncrementalSnapshot(
        const nlohmann::json& baseState,
        const nlohmann::json& incrementalState
    ) const;

    // Add declaration for collectEntries
    void collectEntries(std::shared_ptr<BTreeNode> node, std::vector<std::pair<std::string, std::string>>& entries);
};

} // namespace extensions
} // namespace xenocomm 
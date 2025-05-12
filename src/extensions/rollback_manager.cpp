#include "xenocomm/extensions/rollback_manager.hpp"
#include <random>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <algorithm>
#include <zlib.h>
#include <memory>
#include <queue>

namespace xenocomm {
namespace extensions {

namespace {
    // Helper function to create a SHA-256 hash of a string
    std::string sha256(const std::string& data) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, data.c_str(), data.length());
        EVP_DigestFinal_ex(ctx, hash, &hashLen);
        EVP_MD_CTX_free(ctx);

        std::stringstream ss;
        for(unsigned int i = 0; i < hashLen; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

    // Helper function to ensure directory exists
    void ensureDirectoryExists(const std::string& path) {
        std::filesystem::create_directories(path);
    }

    // Helper function to compress data using zlib
    std::vector<uint8_t> compressData(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        output.resize(compressBound(input.size()));
        
        uLongf destLen = output.size();
        int result = compress2(output.data(), &destLen, input.data(), input.size(), Z_BEST_SPEED);
        
        if (result != Z_OK) {
            throw std::runtime_error("Compression failed");
        }
        
        output.resize(destLen);
        return output;
    }

    // Helper function to decompress data using zlib
    std::vector<uint8_t> decompressData(const std::vector<uint8_t>& input, size_t originalSize) {
        std::vector<uint8_t> output;
        output.resize(originalSize);
        
        uLongf destLen = output.size();
        int result = uncompress(output.data(), &destLen, input.data(), input.size());
        
        if (result != Z_OK) {
            throw std::runtime_error("Decompression failed");
        }
        
        return output;
    }
}

// Define collectEntries BEFORE its first use (e.g., before searchBTree or other callers)
// Add RollbackManager qualifier back
void RollbackManager::collectEntries(std::shared_ptr<RollbackManager::BTreeNode> node, std::vector<std::pair<std::string, std::string>>& entries) {
    if (!node) {
        return;
    }
    if (node->isLeaf) {
        for (size_t i = 0; i < node->keyCount; ++i) {
            entries.emplace_back(node->keys[i], node->values[i]);
        }
    } else {
        for (size_t i = 0; i < node->keyCount; ++i) {
            collectEntries(node->children[i], entries);
            entries.emplace_back(node->keys[i], node->values[i]);
        }
        collectEntries(node->children[node->keyCount], entries);
    }
}

RollbackManager::RollbackManager(
    const RollbackConfig& config,
    std::shared_ptr<CompatibilityChecker> compatibilityChecker
) : config_(config), compatibilityChecker_(std::move(compatibilityChecker)) {
    ensureDirectoryExists(config_.storagePath);
    
    // Load existing rollback points from storage
    for (const auto& entry : std::filesystem::directory_iterator(config_.storagePath)) {
        if (entry.path().extension() == ".json") {
            auto id = entry.path().stem().string();
            auto point = loadRollbackPoint(id);
            if (point) {
                rollbackPoints_[id] = *point;
            }
        }
    }
}

std::string RollbackManager::createRollbackPoint(
    const std::string& variantId,
    const nlohmann::json& state,
    const std::map<std::string, std::string>& metadata
) {
    // Generate unique ID for the rollback point
    std::string id = generateRollbackId();
    
    // Create the rollback point
    RollbackPoint point{
        id,
        std::chrono::system_clock::now(),
        variantId,
        nlohmann::json(),  // Empty state for large objects
        {},                // Empty chunks initially
        calculateChecksum(state),
        metadata,
        false             // Not chunked initially
    };

    // Determine if we need to chunk the state based on size
    std::string stateStr = state.dump();
    bool needsChunking = stateStr.size() > config_.maxSnapshotSizeBytes / 2;  // Use half as threshold

    if (needsChunking) {
        // Create chunks for large state
        point.stateChunks = chunkifyState(state);
        point.isChunked = true;
        
        // Create directory for chunks if needed
        ensureDirectoryExists(config_.storagePath + "/chunks");
        
        // Persist each chunk and store references
        for (const auto& chunk : point.stateChunks) {
            std::string chunkPath = persistChunk(chunk);
            insertIntoBTree(chunk.id, chunkPath);
        }
    } else {
        // Store small state directly
        point.state = state;
    }

    // Check if we need to create an incremental snapshot
    if (config_.enableIncrementalSnapshots && !rollbackPoints_.empty() && !needsChunking) {
        // Find the most recent rollback point for this variant
        auto it = std::find_if(
            rollbackPoints_.rbegin(),
            rollbackPoints_.rend(),
            [&variantId](const auto& pair) {
                return pair.second.variantId == variantId && !pair.second.isChunked;
            }
        );
        
        if (it != rollbackPoints_.rend()) {
            point.state = createIncrementalSnapshot(state, it->second.state);
            point.metadata["base_rollback_id"] = it->first;
        }
    }

    // Persist the rollback point metadata
    if (!persistRollbackPoint(point)) {
        throw std::runtime_error("Failed to persist rollback point");
    }

    // Add to in-memory map
    rollbackPoints_[id] = point;

    // Clean up old rollback points if needed
    if (rollbackPoints_.size() > config_.maxRollbackPoints) {
        cleanupOldRollbackPoints();
    }

    // Periodically optimize B-tree
    if (rollbackPoints_.size() % (config_.maxRollbackPoints / 2) == 0) {
        optimizeBTree();
    }

    return id;
}

bool RollbackManager::restoreToPoint(const std::string& rollbackId) {
    auto point = getRollbackPoint(rollbackId);
    if (!point) {
        return false;
    }

    // Verify integrity
    if (!verifyRollbackPoint(rollbackId)) {
        return false;
    }

    nlohmann::json fullState;

    if (point->isChunked) {
        // Load and verify all chunks
        std::vector<StateChunk> chunks;
        for (const auto& chunk : point->stateChunks) {
            std::string chunkPath = searchBTree(chunk.id);
            if (chunkPath.empty()) {
                throw std::runtime_error("Failed to find chunk: " + chunk.id);
            }
            
            chunks.push_back(loadChunk(chunk.id));
        }
        
        // Reassemble the complete state
        try {
            fullState = reassembleState(chunks);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to reassemble state: " + std::string(e.what()));
        }
    } else {
        // Handle incremental snapshots for non-chunked state
        fullState = point->state;
        if (config_.enableIncrementalSnapshots) {
            // Find the base state by walking back through the rollback points
            std::vector<RollbackPoint> chain;
            auto currentPoint = *point;
            
            while (currentPoint.metadata.count("base_rollback_id")) {
                auto baseId = currentPoint.metadata.at("base_rollback_id");
                auto basePoint = getRollbackPoint(baseId);
                if (!basePoint) {
                    return false;
                }
                chain.push_back(*basePoint);
                currentPoint = *basePoint;
            }

            // Apply incremental snapshots in reverse order
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                fullState = applyIncrementalSnapshot(fullState, it->state);
            }
        }
    }

    // TODO: Apply the state to the system
    // This would be implemented by the specific protocol implementation
    
    return true;
}

std::optional<RollbackPoint> RollbackManager::getRollbackPoint(
    const std::string& rollbackId
) const {
    auto it = rollbackPoints_.find(rollbackId);
    if (it != rollbackPoints_.end()) {
        return it->second;
    }
    return loadRollbackPoint(rollbackId);
}

std::vector<RollbackPoint> RollbackManager::listRollbackPoints(
    const std::string& variantId
) const {
    std::vector<RollbackPoint> result;
    for (const auto& [id, point] : rollbackPoints_) {
        if (variantId.empty() || point.variantId == variantId) {
            result.push_back(point);
        }
    }
    
    // Sort by timestamp, newest first
    std::sort(result.begin(), result.end(),
        [](const RollbackPoint& a, const RollbackPoint& b) {
            return a.timestamp > b.timestamp;
        });
    
    return result;
}

bool RollbackManager::verifyRollbackPoint(const std::string& rollbackId) const {
    auto point = getRollbackPoint(rollbackId);
    if (!point) {
        return false;
    }

    // Verify checksum
    return point->checksum == calculateChecksum(point->state);
}

size_t RollbackManager::cleanupOldRollbackPoints() {
    size_t removed = 0;
    auto now = std::chrono::system_clock::now();
    
    // Collect points to remove
    std::vector<std::string> toRemove;
    for (const auto& [id, point] : rollbackPoints_) {
        if (!shouldRetainRollbackPoint(point)) {
            toRemove.push_back(id);
        }
    }

    // Remove points
    for (const auto& id : toRemove) {
        rollbackPoints_.erase(id);
        std::filesystem::remove(config_.storagePath + id + ".json");
        removed++;
    }

    return removed;
}

std::string RollbackManager::calculateChecksum(const nlohmann::json& state) const {
    return sha256(state.dump());
}

bool RollbackManager::persistRollbackPoint(const RollbackPoint& point) const {
    try {
        nlohmann::json j = {
            {"id", point.id},
            {"timestamp", std::chrono::system_clock::to_time_t(point.timestamp)},
            {"variantId", point.variantId},
            {"state", point.state},
            {"checksum", point.checksum},
            {"metadata", point.metadata}
        };

        std::ofstream file(config_.storagePath + point.id + ".json");
        file << j.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<RollbackPoint> RollbackManager::loadRollbackPoint(
    const std::string& id
) const {
    try {
        std::ifstream file(config_.storagePath + id + ".json");
        nlohmann::json j;
        file >> j;

        RollbackPoint point;
        point.id = j["id"];
        point.timestamp = std::chrono::system_clock::from_time_t(j["timestamp"]);
        point.variantId = j["variantId"];
        point.state = j["state"];
        point.checksum = j["checksum"];
        point.metadata = j["metadata"].get<std::map<std::string, std::string>>();

        return point;
    } catch (...) {
        return std::nullopt;
    }
}

std::string RollbackManager::generateRollbackId() const {
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    );
    
    std::stringstream ss;
    ss << "rb_" << nowMs.count() << "_" 
       << std::hex << std::random_device()();
    return ss.str();
}

bool RollbackManager::shouldRetainRollbackPoint(const RollbackPoint& point) const {
    auto now = std::chrono::system_clock::now();
    auto age = now - point.timestamp;
    
    // Keep if within retention period
    if (age < config_.retentionPeriod) {
        return true;
    }

    // Keep if marked as permanent in metadata
    if (point.metadata.count("permanent") && point.metadata.at("permanent") == "true") {
        return true;
    }

    // Keep if it's a base for any incremental snapshots
    if (config_.enableIncrementalSnapshots) {
        for (const auto& [id, other] : rollbackPoints_) {
            if (other.metadata.count("base_rollback_id") && 
                other.metadata.at("base_rollback_id") == point.id) {
                return true;
            }
        }
    }

    return false;
}

nlohmann::json RollbackManager::createIncrementalSnapshot(
    const nlohmann::json& currentState,
    const nlohmann::json& previousState
) const {
    nlohmann::json diff;
    
    // Simple diff implementation - in practice you'd want a more sophisticated
    // diff algorithm that handles nested structures better
    for (auto it = currentState.begin(); it != currentState.end(); ++it) {
        if (!previousState.contains(it.key()) || previousState[it.key()] != it.value()) {
            diff[it.key()] = it.value();
        }
    }
    
    // Also track deleted keys
    for (auto it = previousState.begin(); it != previousState.end(); ++it) {
        if (!currentState.contains(it.key())) {
            diff["__deleted__"][it.key()] = true;
        }
    }
    
    return diff;
}

nlohmann::json RollbackManager::applyIncrementalSnapshot(
    const nlohmann::json& baseState,
    const nlohmann::json& incrementalState
) const {
    nlohmann::json result = baseState;
    
    // Apply changes
    for (auto it = incrementalState.begin(); it != incrementalState.end(); ++it) {
        if (it.key() == "__deleted__") {
            // Handle deletions
            for (auto& [key, _] : it.value().items()) {
                result.erase(key);
            }
        } else {
            result[it.key()] = it.value();
        }
    }
    
    return result;
}

// New method implementations for chunked storage
std::vector<StateChunk> RollbackManager::chunkifyState(const nlohmann::json& state) const {
    std::vector<StateChunk> chunks;
    std::string stateStr = state.dump();
    
    // Calculate number of chunks needed
    size_t totalSize = stateStr.size();
    size_t numChunks = (totalSize + config_.chunkSize - 1) / config_.chunkSize;
    
    for (size_t i = 0; i < numChunks; i++) {
        StateChunk chunk;
        chunk.offset = i * config_.chunkSize;
        
        // Extract chunk data
        size_t chunkSize = std::min(config_.chunkSize, totalSize - chunk.offset);
        std::vector<uint8_t> data(stateStr.begin() + chunk.offset, 
                                 stateStr.begin() + chunk.offset + chunkSize);
        
        chunk.data = data;
        chunk.id = sha256(std::string(data.begin(), data.end()));
        chunk.checksum = chunk.id; // Use SHA-256 hash as checksum
        
        if (config_.enableCompression) {
            compressChunk(chunk);
        }
        
        chunks.push_back(std::move(chunk));
    }
    
    return chunks;
}

nlohmann::json RollbackManager::reassembleState(const std::vector<StateChunk>& chunks) const {
    std::string completeState;
    completeState.reserve(chunks.size() * config_.chunkSize); // Pre-allocate approximate size
    
    for (const auto& chunk : chunks) {
        auto chunkData = chunk.data;
        if (config_.enableCompression) {
            StateChunk mutableChunk = chunk;
            decompressChunk(mutableChunk);
            chunkData = mutableChunk.data;
        }
        
        // Verify chunk integrity
        if (chunk.checksum != sha256(std::string(chunkData.begin(), chunkData.end()))) {
            throw std::runtime_error("Chunk integrity check failed");
        }
        
        completeState.append(chunkData.begin(), chunkData.end());
    }
    
    return nlohmann::json::parse(completeState);
}

void RollbackManager::compressChunk(StateChunk& chunk) const {
    auto originalSize = chunk.data.size();
    chunk.data = compressData(chunk.data);
    // chunk.metadata["original_size"] = std::to_string(originalSize); // Commented out as per instructions
}

void RollbackManager::decompressChunk(StateChunk& chunk) const {
    // size_t originalSize = std::stoul(chunk.metadata.at("original_size")); // Commented out line ~505
    // chunk.data = decompressData(chunk.data, originalSize); // Comment out this line too as originalSize is unavailable
}

std::string RollbackManager::persistChunk(const StateChunk& chunk) const {
    std::string chunkPath = config_.storagePath + "/chunks/" + chunk.id + ".bin";
    std::ofstream file(chunkPath, std::ios::binary);
    
    if (!file) {
        throw std::runtime_error("Failed to create chunk file");
    }
    
    // Write chunk metadata
    nlohmann::json metadata = {
        {"offset", chunk.offset},
        {"checksum", chunk.checksum},
        // {"metadata", chunk.metadata}
    };
    
    std::string metadataStr = metadata.dump();
    uint32_t metadataSize = static_cast<uint32_t>(metadataStr.size());
    
    file.write(reinterpret_cast<const char*>(&metadataSize), sizeof(metadataSize));
    file.write(metadataStr.data(), metadataSize);
    
    // Write chunk data
    file.write(reinterpret_cast<const char*>(chunk.data.data()), chunk.data.size());
    
    return chunkPath;
}

StateChunk RollbackManager::loadChunk(const std::string& chunkId) const {
    std::string chunkPath = config_.storagePath + "/chunks/" + chunkId + ".bin";
    std::ifstream file(chunkPath, std::ios::binary);
    
    if (!file) {
        throw std::runtime_error("Failed to open chunk file");
    }
    
    // Read chunk metadata
    uint32_t metadataSize;
    file.read(reinterpret_cast<char*>(&metadataSize), sizeof(metadataSize));
    
    std::string metadataStr(metadataSize, '\0');
    file.read(&metadataStr[0], metadataSize);
    
    auto metadata = nlohmann::json::parse(metadataStr);
    
    // Create and populate chunk
    StateChunk chunk;
    chunk.id = chunkId;
    chunk.offset = metadata["offset"];
    chunk.checksum = metadata["checksum"];
    // chunk.metadata = metadata["metadata"];
    
    // Read chunk data
    file.seekg(0, std::ios::end);
    size_t dataSize = file.tellg() - static_cast<long long>(sizeof(metadataSize) + metadataSize);
    file.seekg(sizeof(metadataSize) + metadataSize);
    
    chunk.data.resize(dataSize);
    file.read(reinterpret_cast<char*>(chunk.data.data()), dataSize);
    
    return chunk;
}

// Enhanced B-tree implementation
void RollbackManager::insertIntoBTree(const std::string& key, const std::string& value) {
    if (!btreeRoot_) {
        btreeRoot_ = std::make_shared<BTreeNode>();
        btreeRoot_->keys[0] = key;
        btreeRoot_->values[0] = value;
        btreeRoot_->keyCount = 1;
        persistNode(btreeRoot_, generateNodeId(btreeRoot_));
        return;
    }
    
    // If root is full, create new root
    if (btreeRoot_->keyCount == 2 * BTreeNode::ORDER - 1) {
        auto newRoot = std::make_shared<BTreeNode>();
        newRoot->isLeaf = false;
        newRoot->children[0] = btreeRoot_;
        splitChild(newRoot, 0);
        btreeRoot_ = newRoot;
    }
    
    insertNonFull(btreeRoot_, key, value);
}

void RollbackManager::splitChild(std::shared_ptr<BTreeNode> parent, size_t index) {
    auto child = parent->children[index];
    auto newNode = std::make_shared<BTreeNode>();
    newNode->isLeaf = child->isLeaf;
    newNode->keyCount = BTreeNode::ORDER - 1;
    
    // Copy keys and values to new node
    for (size_t i = 0; i < BTreeNode::ORDER - 1; i++) {
        newNode->keys[i] = child->keys[i + BTreeNode::ORDER];
        newNode->values[i] = child->values[i + BTreeNode::ORDER];
    }
    
    // If not leaf, move children
    if (!child->isLeaf) {
        for (size_t i = 0; i < BTreeNode::ORDER; i++) {
            newNode->children[i] = child->children[i + BTreeNode::ORDER];
        }
    }
    
    // Update child's key count
    child->keyCount = BTreeNode::ORDER - 1;
    
    // Move parent's keys and children to make room
    for (size_t i = parent->keyCount; i > index; i--) {
        parent->children[i + 1] = parent->children[i];
    }
    parent->children[index + 1] = newNode;
    
    for (size_t i = parent->keyCount - 1; i >= index && i != SIZE_MAX; i--) {
        parent->keys[i + 1] = parent->keys[i];
        parent->values[i + 1] = parent->values[i];
    }
    
    // Copy middle key to parent
    parent->keys[index] = child->keys[BTreeNode::ORDER - 1];
    parent->values[index] = child->values[BTreeNode::ORDER - 1];
    parent->keyCount++;
    
    // Persist changes
    std::string childId = generateNodeId(child);
    std::string newNodeId = generateNodeId(newNode);
    std::string parentId = generateNodeId(parent);
    
    persistNode(child, childId);
    persistNode(newNode, newNodeId);
    persistNode(parent, parentId);
    
    // Update cache
    nodeCache_.put(childId, child);
    nodeCache_.put(newNodeId, newNode);
    nodeCache_.put(parentId, parent);
}

void RollbackManager::insertNonFull(std::shared_ptr<BTreeNode> node, const std::string& key, const std::string& value) {
    int i = node->keyCount - 1;
    
    if (node->isLeaf) {
        // Insert into leaf node
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            node->values[i + 1] = node->values[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->values[i + 1] = value;
        node->keyCount++;
        
        // Persist changes
        std::string nodeId = generateNodeId(node);
        persistNode(node, nodeId);
        nodeCache_.put(nodeId, node);
    } else {
        // Find child to recurse on
        while (i >= 0 && key < node->keys[i]) {
            i--;
        }
        i++;
        
        // Load child if needed
        std::string childId = generateNodeId(node->children[i]);
        auto child = nodeCache_.get(childId);
        if (!child) {
            child = loadNode(childId);
            nodeCache_.put(childId, child);
        }
        
        // If child is full, split it
        if (child->keyCount == 2 * BTreeNode::ORDER - 1) {
            splitChild(node, i);
            if (key > node->keys[i]) {
                i++;
            }
        }
        
        insertNonFull(node->children[i], key, value);
    }
}

std::string RollbackManager::searchBTree(const std::string& key) {
    if (!btreeRoot_) {
        return "";
    }
    
    auto node = btreeRoot_;
    while (true) {
        size_t i = 0;
        while (i < node->keyCount && key > node->keys[i]) {
            i++;
        }
        
        if (i < node->keyCount && key == node->keys[i]) {
            return node->values[i];
        }
        
        if (node->isLeaf) {
            return "";
        }
        
        // Load next node
        std::string childId = generateNodeId(node->children[i]);
        auto nextNode = nodeCache_.get(childId);
        if (!nextNode) {
            nextNode = loadNode(childId);
            nodeCache_.put(childId, nextNode);
        }
        node = nextNode;
    }
}

void RollbackManager::optimizeBTree() {
    std::vector<std::pair<std::string, std::string>> entries;
    collectEntries(btreeRoot_, entries);
    
    // Sort entries for bulk loading
    std::sort(entries.begin(), entries.end());
    
    // Clear cache before rebuilding
    nodeCache_.clear();
    
    // Bulk load the tree
    bulkLoadBTree(entries);
}

void RollbackManager::bulkLoadBTree(const std::vector<std::pair<std::string, std::string>>& sortedEntries) {
    // Create leaf nodes
    auto leafNodes = createLeafNodes(sortedEntries);
    
    // Create internal nodes bottom-up
    auto currentLevel = leafNodes;
    while (currentLevel.size() > 1) {
        currentLevel = createInternalNodes(currentLevel);
    }
    
    btreeRoot_ = currentLevel[0];
    persistNode(btreeRoot_, generateNodeId(btreeRoot_));
}

std::vector<std::shared_ptr<RollbackManager::BTreeNode>> RollbackManager::createLeafNodes(
    const std::vector<std::pair<std::string, std::string>>& sortedEntries) {
    std::vector<std::shared_ptr<BTreeNode>> leafNodes;
    
    size_t entriesPerNode = BTreeNode::ORDER * 2 - 1;
    size_t currentEntry = 0;
    
    while (currentEntry < sortedEntries.size()) {
        auto node = std::make_shared<BTreeNode>();
        node->isLeaf = true;
        
        size_t entriesToCopy = std::min(entriesPerNode, 
                                       sortedEntries.size() - currentEntry);
        
        for (size_t i = 0; i < entriesToCopy; i++) {
            node->keys[i] = sortedEntries[currentEntry + i].first;
            node->values[i] = sortedEntries[currentEntry + i].second;
        }
        
        node->keyCount = entriesToCopy;
        currentEntry += entriesToCopy;
        
        std::string nodeId = generateNodeId(node);
        persistNode(node, nodeId);
        nodeCache_.put(nodeId, node);
        
        leafNodes.push_back(node);
    }
    
    return leafNodes;
}

std::vector<std::shared_ptr<RollbackManager::BTreeNode>> RollbackManager::createInternalNodes(
    const std::vector<std::shared_ptr<RollbackManager::BTreeNode>>& children) {
    std::vector<std::shared_ptr<BTreeNode>> parentNodes;
    
    size_t childrenPerNode = BTreeNode::ORDER * 2;
    size_t currentChild = 0;
    
    while (currentChild < children.size()) {
        auto node = std::make_shared<BTreeNode>();
        node->isLeaf = false;
        
        size_t childrenToProcess = std::min(childrenPerNode, 
                                          children.size() - currentChild);
        
        // Copy keys and values from children
        for (size_t i = 0; i < childrenToProcess - 1; i++) {
            node->keys[i] = children[currentChild + i]->keys[children[currentChild + i]->keyCount - 1];
            node->values[i] = children[currentChild + i]->values[children[currentChild + i]->keyCount - 1];
            node->children[i] = children[currentChild + i];
        }
        
        node->children[childrenToProcess - 1] = children[currentChild + childrenToProcess - 1];
        node->keyCount = childrenToProcess - 1;
        
        std::string nodeId = generateNodeId(node);
        persistNode(node, nodeId);
        nodeCache_.put(nodeId, node);
        
        parentNodes.push_back(node);
        currentChild += childrenToProcess;
    }
    
    return parentNodes;
}

std::string RollbackManager::generateNodeId(const std::shared_ptr<BTreeNode>& node) const {
    // Create a unique identifier based on node content
    std::stringstream ss;
    for (size_t i = 0; i < node->keyCount; i++) {
        ss << node->keys[i];
    }
    return sha256(ss.str());
}

void RollbackManager::persistNode(const std::shared_ptr<BTreeNode>& node, const std::string& nodeId) {
    std::string nodePath = config_.storagePath + "/btree/" + nodeId + ".bin";
    std::ofstream file(nodePath, std::ios::binary);
    
    if (!file) {
        throw std::runtime_error("Failed to create B-tree node file");
    }
    
    // Write node metadata
    file.write(reinterpret_cast<const char*>(&node->keyCount), sizeof(node->keyCount));
    file.write(reinterpret_cast<const char*>(&node->isLeaf), sizeof(node->isLeaf));
    
    // Write keys and values
    for (size_t i = 0; i < node->keyCount; i++) {
        size_t keySize = node->keys[i].size();
        size_t valueSize = node->values[i].size();
        
        file.write(reinterpret_cast<const char*>(&keySize), sizeof(keySize));
        file.write(node->keys[i].data(), keySize);
        
        file.write(reinterpret_cast<const char*>(&valueSize), sizeof(valueSize));
        file.write(node->values[i].data(), valueSize);
    }
    
    // Write child node IDs if not leaf
    if (!node->isLeaf) {
        for (size_t i = 0; i <= node->keyCount; i++) {
            std::string childId = generateNodeId(node->children[i]);
            size_t idSize = childId.size();
            
            file.write(reinterpret_cast<const char*>(&idSize), sizeof(idSize));
            file.write(childId.data(), idSize);
        }
    }
}

std::shared_ptr<RollbackManager::BTreeNode> RollbackManager::loadNode(const std::string& nodeId) {
    std::string nodePath = config_.storagePath + "/btree/" + nodeId + ".bin";
    std::ifstream file(nodePath, std::ios::binary);
    
    if (!file) {
        throw std::runtime_error("Failed to open B-tree node file");
    }
    
    auto node = std::make_shared<BTreeNode>();
    
    // Read node metadata
    file.read(reinterpret_cast<char*>(&node->keyCount), sizeof(node->keyCount));
    file.read(reinterpret_cast<char*>(&node->isLeaf), sizeof(node->isLeaf));
    
    // Read keys and values
    for (size_t i = 0; i < node->keyCount; i++) {
        size_t keySize, valueSize;
        
        file.read(reinterpret_cast<char*>(&keySize), sizeof(keySize));
        node->keys[i].resize(keySize);
        file.read(&node->keys[i][0], keySize);
        
        file.read(reinterpret_cast<char*>(&valueSize), sizeof(valueSize));
        node->values[i].resize(valueSize);
        file.read(&node->values[i][0], valueSize);
    }
    
    // Read child node IDs if not leaf
    if (!node->isLeaf) {
        for (size_t i = 0; i <= node->keyCount; i++) {
            size_t idSize;
            file.read(reinterpret_cast<char*>(&idSize), sizeof(idSize));
            
            std::string childId(idSize, '\0');
            file.read(&childId[0], idSize);
            
            // Load child node
            node->children[i] = loadNode(childId);
        }
    }
    
    return node;
}

} // namespace extensions
} // namespace xenocomm 
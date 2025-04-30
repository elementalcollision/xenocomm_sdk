#pragma once

#include "xenocomm/core/capability_signaler.h"
#include <list>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <optional>
#include <cstddef>
#include <string>

namespace xenocomm {
namespace core {

/**
 * @brief Statistics about cache performance.
 */
struct CacheStats {
    size_t hits{0};           ///< Number of cache hits
    size_t misses{0};         ///< Number of cache misses
    size_t evictions{0};      ///< Number of entries evicted due to size/time limits
    size_t insertions{0};     ///< Number of entries inserted
};

/**
 * @brief Configuration for capability caching behavior.
 */
struct CacheConfig {
    /**
     * @brief Maximum number of entries to store in the cache.
     */
    std::size_t max_entries{1000};

    /**
     * @brief Time-to-live for cache entries.
     */
    std::chrono::seconds ttl{300}; // 5 minutes default

    /**
     * @brief Whether to enable cache statistics tracking.
     */
    bool track_stats{false};
};

/**
 * @brief Cache entry containing capability query results and metadata.
 */
struct CacheEntry {
    std::string value;
    std::chrono::steady_clock::time_point expiry;
};

/**
 * @brief LRU cache for capability query results to improve performance of repeated queries.
 * 
 * The CapabilityCache implements a least-recently-used (LRU) caching strategy for capability
 * discovery results. It maintains a fixed-size cache with configurable time-to-live (TTL)
 * for entries. The cache is thread-safe and provides statistics tracking.
 * 
 * Performance Characteristics:
 * - Cache lookup: O(1) average case
 * - Cache insertion: O(1) average case
 * - Cache eviction: O(1)
 * - Memory usage: O(n) where n is the configured cache size
 */
class CapabilityCache {
public:
    /**
     * @brief Constructs a CapabilityCache with the specified configuration.
     * 
     * @param config The cache configuration
     */
    explicit CapabilityCache(const CacheConfig& config);

    /**
     * @brief Default destructor.
     */
    ~CapabilityCache() = default;

    // Prevent copying
    CapabilityCache(const CapabilityCache&) = delete;
    CapabilityCache& operator=(const CapabilityCache&) = delete;

    // Allow moving
    CapabilityCache(CapabilityCache&&) = default;
    CapabilityCache& operator=(CapabilityCache&&) = default;

    /**
     * @brief Looks up a capability in the cache.
     * 
     * @param key The capability key to look up
     * @return The cached capability if found and not expired, std::nullopt otherwise
     */
    std::optional<std::string> get(const std::string& key);

    /**
     * @brief Stores a capability in the cache.
     * 
     * @param key The capability key
     * @param value The capability value to store
     */
    void put(const std::string& key, const std::string& value);

    /**
     * @brief Removes a capability from the cache.
     * 
     * @param key The capability key to remove
     * @return true if the key was found and removed, false otherwise
     */
    bool remove(const std::string& key);

    /**
     * @brief Clears all entries from the cache.
     */
    void clear();

    /**
     * @brief Gets the current cache statistics.
     * 
     * @return The cache statistics (only valid if track_stats is enabled)
     */
    CacheStats get_stats() const;

private:
    // Cache configuration
    CacheConfig config_;

    // Cache statistics
    CacheStats stats_;

    // Cache entry key type (hash of capabilities vector)
    using KeyType = std::string;

    // LRU list type (key ordered by access time)
    using LRUList = std::list<KeyType>;

    // Main cache storage
    std::unordered_map<KeyType, std::pair<CacheEntry, LRUList::iterator>> cache_;
    
    // LRU tracking list
    LRUList lruList_;

    // Thread safety
    mutable std::mutex mutex_;

    /**
     * @brief Remove the least recently used entry if cache is full.
     */
    void evictIfNeeded();

    /**
     * @brief Check if a cache entry has expired.
     */
    bool isExpired(const CacheEntry& entry) const;

    /**
     * @brief Update LRU status for a cache entry.
     */
    void updateLRU(const KeyType& key);

    void evict_expired_entries();
    void evict_lru_entry();
};

} // namespace core
} // namespace xenocomm 
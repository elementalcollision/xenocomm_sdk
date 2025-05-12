#include "xenocomm/core/capability_cache.h"
#include <functional>

namespace xenocomm {
namespace core {

CapabilityCache::CapabilityCache(const CacheConfig& config)
    : config_(config) {}

std::optional<std::string> CapabilityCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove expired entries first
    evict_expired_entries();

    auto it = cache_.find(key);
    if (it == cache_.end()) {
        if (config_.track_stats) {
            stats_.misses++;
        }
        return std::nullopt;
    }

    // Check if entry has expired
    if (isExpired(it->second.first)) {
        // Remove expired entry
        lruList_.erase(it->second.second);
        cache_.erase(it);
        if (config_.track_stats) {
            stats_.evictions++;
            stats_.misses++;
        }
        return std::nullopt;
    }

    // Move to front of LRU list
    lruList_.erase(it->second.second);
    lruList_.push_front(key);
    it->second.second = lruList_.begin();

    if (config_.track_stats) {
        stats_.hits++;
    }

    return it->second.first.value;
}

void CapabilityCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove expired entries first
    evict_expired_entries();

    // Remove existing entry if present
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        lruList_.erase(it->second.second);
        cache_.erase(it);
    }

    // Ensure we have space
    evictIfNeeded();

    // Create new entry
    CacheEntry entry;
    entry.value = value;
    entry.expiry = std::chrono::steady_clock::now() + config_.ttl;

    // Add to LRU list
    lruList_.push_front(key);
    cache_.emplace(key, std::make_pair(std::move(entry), lruList_.begin()));

    if (config_.track_stats) {
        stats_.insertions++;
    }
}

bool CapabilityCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_.find(key);
    if (it != cache_.end()) {
        lruList_.erase(it->second.second);
        cache_.erase(it);
        if (config_.track_stats) {
            stats_.evictions++;
        }
        return true;
    }
    return false;
}

void CapabilityCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (config_.track_stats) {
        stats_.evictions += cache_.size();
    }

    cache_.clear();
    lruList_.clear();
}

CacheStats CapabilityCache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void CapabilityCache::evict_expired_entries() {
    auto now = std::chrono::steady_clock::now();
    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (isExpired(it->second.first)) {
            lruList_.erase(it->second.second);
            if (config_.track_stats) {
                stats_.evictions++;
            }
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void CapabilityCache::evictIfNeeded() {
    // Assumes mutex_ is already held
    while (cache_.size() >= config_.max_entries) {
        if (lruList_.empty()) break; // Should not happen if cache is not empty
        std::string lruKey = lruList_.back();
        lruList_.pop_back();
        cache_.erase(lruKey);
        if (config_.track_stats) stats_.evictions++;
    }
}

bool CapabilityCache::isExpired(const CacheEntry& entry) const {
    return std::chrono::steady_clock::now() > entry.expiry;
}

} // namespace core
} // namespace xenocomm 
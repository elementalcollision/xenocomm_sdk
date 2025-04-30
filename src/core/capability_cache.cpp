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

    auto it = entries_.find(key);
    if (it == entries_.end()) {
        if (config_.track_stats) {
            stats_.misses++;
        }
        return std::nullopt;
    }

    // Check if entry has expired
    if (is_expired(it->second.first)) {
        // Remove expired entry
        lru_list_.erase(it->second.second);
        entries_.erase(it);
        if (config_.track_stats) {
            stats_.evictions++;
            stats_.misses++;
        }
        return std::nullopt;
    }

    // Move to front of LRU list
    lru_list_.erase(it->second.second);
    lru_list_.push_front(key);
    it->second.second = lru_list_.begin();

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
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        lru_list_.erase(it->second.second);
        entries_.erase(it);
    }

    // Ensure we have space
    while (entries_.size() >= config_.max_entries) {
        evict_lru_entry();
    }

    // Create new entry
    CacheEntry entry;
    entry.value = value;
    entry.expiry = std::chrono::steady_clock::now() + config_.ttl;

    // Add to LRU list
    lru_list_.push_front(key);
    entries_.emplace(key, std::make_pair(std::move(entry), lru_list_.begin()));

    if (config_.track_stats) {
        stats_.insertions++;
    }
}

bool CapabilityCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = entries_.find(key);
    if (it != entries_.end()) {
        lru_list_.erase(it->second.second);
        entries_.erase(it);
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
        stats_.evictions += entries_.size();
    }

    entries_.clear();
    lru_list_.clear();
}

CacheStats CapabilityCache::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void CapabilityCache::evict_expired_entries() {
    auto now = std::chrono::steady_clock::now();
    auto it = entries_.begin();
    while (it != entries_.end()) {
        if (is_expired(it->second.first)) {
            lru_list_.erase(it->second.second);
            if (config_.track_stats) {
                stats_.evictions++;
            }
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void CapabilityCache::evict_lru_entry() {
    if (!lru_list_.empty()) {
        auto lru_key = lru_list_.back();
        entries_.erase(lru_key);
        lru_list_.pop_back();
        if (config_.track_stats) {
            stats_.evictions++;
        }
    }
}

bool CapabilityCache::is_expired(const CacheEntry& entry) const {
    return std::chrono::steady_clock::now() > entry.expiry;
}

} // namespace core
} // namespace xenocomm 
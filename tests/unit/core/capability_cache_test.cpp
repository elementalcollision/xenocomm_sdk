#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "xenocomm/core/capability_cache.h"

using namespace xenocomm::core;
using namespace std::chrono_literals;

class CapabilityCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        CacheConfig config;
        config.max_entries = 10;
        config.ttl = std::chrono::seconds(1);  // Short TTL for testing
        config.track_stats = true;
        cache = std::make_unique<CapabilityCache>(config);
    }

    void TearDown() override {
        // Cleanup happens automatically via unique_ptr
    }

    std::unique_ptr<CapabilityCache> cache;
};

// Test basic cache operations
TEST_F(CapabilityCacheTest, BasicOperations) {
    // Test put and get
    ASSERT_FALSE(cache->get("key1").has_value());  // Should be empty initially
    cache->put("key1", "value1");
    auto result = cache->get("key1");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, "value1");

    // Test remove
    ASSERT_TRUE(cache->remove("key1"));
    ASSERT_FALSE(cache->get("key1").has_value());
    ASSERT_FALSE(cache->remove("key1"));  // Should return false for non-existent entry
}

// Test cache eviction due to size limit
TEST_F(CapabilityCacheTest, SizeEviction) {
    // Fill cache beyond its capacity
    for (int i = 0; i < 15; i++) {
        std::string key = "key" + std::to_string(i);
        std::string value = "value" + std::to_string(i);
        cache->put(key, value);
    }

    // First few entries should have been evicted
    ASSERT_FALSE(cache->get("key0").has_value());

    // Later entries should still be present
    ASSERT_TRUE(cache->get("key14").has_value());

    // Check eviction stats
    auto stats = cache->get_stats();
    ASSERT_GT(stats.evictions, 0);
}

// Test cache entry expiration
TEST_F(CapabilityCacheTest, TimeExpiration) {
    cache->put("key1", "value1");
    ASSERT_TRUE(cache->get("key1").has_value());

    // Wait for TTL to expire
    std::this_thread::sleep_for(1500ms);  // Wait longer than TTL

    ASSERT_FALSE(cache->get("key1").has_value());
}

// Test statistics tracking
TEST_F(CapabilityCacheTest, Statistics) {
    // Test miss
    cache->get("key1");
    auto stats = cache->get_stats();
    ASSERT_EQ(stats.misses, 1);
    ASSERT_EQ(stats.hits, 0);

    // Test hit
    cache->put("key1", "value1");
    cache->get("key1");
    stats = cache->get_stats();
    ASSERT_EQ(stats.hits, 1);

    // Test eviction
    cache->remove("key1");
    stats = cache->get_stats();
    ASSERT_EQ(stats.evictions, 1);

    // Clear all entries
    cache->clear();
    stats = cache->get_stats();
    ASSERT_EQ(stats.evictions, 1);  // No additional evictions since key1 was already removed
}

// Test concurrent access
TEST_F(CapabilityCacheTest, ConcurrentAccess) {
    const int numThreads = 10;
    const int opsPerThread = 100;
    std::vector<std::thread> threads;

    // Create threads that simultaneously read and write to the cache
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, opsPerThread]() {
            for (int j = 0; j < opsPerThread; ++j) {
                std::string key = "key" + std::to_string(i);
                std::string value = "value" + std::to_string(i);
                
                // Mix of operations
                if (j % 3 == 0) {
                    cache->put(key, value);
                } else if (j % 3 == 1) {
                    cache->get(key);
                } else {
                    cache->remove(key);
                }
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    // Verify that stats are non-zero and the cache is still functional
    auto stats = cache->get_stats();
    ASSERT_GT(stats.hits + stats.misses + stats.evictions, 0);

    // Test basic operation still works
    cache->put("test_key", "test_value");
    auto result = cache->get("test_key");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(*result, "test_value");
}

// Test clear operation
TEST_F(CapabilityCacheTest, Clear) {
    // Add some entries
    for (int i = 0; i < 5; i++) {
        std::string key = "key" + std::to_string(i);
        std::string value = "value" + std::to_string(i);
        cache->put(key, value);
    }

    // Clear the cache
    cache->clear();

    // Verify all entries are gone
    for (int i = 0; i < 5; i++) {
        std::string key = "key" + std::to_string(i);
        ASSERT_FALSE(cache->get(key).has_value());
    }

    // Verify eviction stats
    auto stats = cache->get_stats();
    ASSERT_EQ(stats.evictions, 5);
} 
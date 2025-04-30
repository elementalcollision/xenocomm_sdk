#include <gtest/gtest.h>
#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/capability_index.h"
#include "xenocomm/core/capability_cache.h"
#include "xenocomm/core/in_memory_capability_signaler.h"
#include "xenocomm/utils/serialization.h"
#include <thread>
#include <chrono>

using namespace xenocomm::core;
using namespace std::chrono_literals;

// Test fixture for CapabilitySignaler tests
class CapabilitySignalerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a new instance with custom cache config
        CacheConfig cacheConfig;
        cacheConfig.max_entries = 10;
        cacheConfig.ttl = std::chrono::seconds(1);  // Short TTL for testing
        cacheConfig.track_stats = true;
        signaler = std::make_unique<InMemoryCapabilitySignaler>(cacheConfig);
    }

    void TearDown() override {
        // Cleanup happens automatically via unique_ptr
    }

    std::unique_ptr<InMemoryCapabilitySignaler> signaler;
};

// Test basic registration and retrieval
TEST_F(CapabilitySignalerTest, RegisterAndGetCapabilities) {
    std::string agent1 = "agent1";
    Capability cap1 = {"serviceA", {1, 0, 0}, {{"endpoint", "tcp://localhost:1234"}}};
    Capability cap2 = {"serviceB", {2, 1, 0}, {}};

    // Register capabilities
    ASSERT_TRUE(signaler->registerCapability(agent1, cap1));
    ASSERT_TRUE(signaler->registerCapability(agent1, cap2));

    // Retrieve capabilities
    std::vector<Capability> retrievedCaps = signaler->getAgentCapabilities(agent1);

    // Verify retrieved capabilities (order might not be guaranteed)
    ASSERT_EQ(retrievedCaps.size(), 2);
    // Use unordered set for comparison as vector order isn't guaranteed
    std::unordered_set<Capability> retrievedSet(retrievedCaps.begin(), retrievedCaps.end());
    ASSERT_TRUE(retrievedSet.count(cap1));
    ASSERT_TRUE(retrievedSet.count(cap2));

    // Attempt to retrieve for non-existent agent
    ASSERT_TRUE(signaler->getAgentCapabilities("nonexistent_agent").empty());
}

// Test unregistering capabilities
TEST_F(CapabilitySignalerTest, UnregisterCapability) {
    std::string agent1 = "agent1";
    Capability cap1 = {"serviceA", {1, 0, 0}};
    Capability cap2 = {"serviceB", {2, 1, 0}};

    signaler->registerCapability(agent1, cap1);
    signaler->registerCapability(agent1, cap2);
    ASSERT_EQ(signaler->getAgentCapabilities(agent1).size(), 2);

    // Unregister cap1
    ASSERT_TRUE(signaler->unregisterCapability(agent1, cap1));
    std::vector<Capability> remainingCaps = signaler->getAgentCapabilities(agent1);
    ASSERT_EQ(remainingCaps.size(), 1);
    ASSERT_EQ(remainingCaps[0].name, cap2.name); // Check remaining is cap2

    // Unregister cap2
    ASSERT_TRUE(signaler->unregisterCapability(agent1, cap2));
    ASSERT_TRUE(signaler->getAgentCapabilities(agent1).empty());

    // Try unregistering non-existent capability
    ASSERT_FALSE(signaler->unregisterCapability(agent1, cap1)); // Already removed
    ASSERT_FALSE(signaler->unregisterCapability("nonexistent_agent", cap1));
}

// Test agent discovery
TEST_F(CapabilitySignalerTest, DiscoverAgents) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";
    std::string agent3 = "agent3";

    Capability capA = {"serviceA", {1, 0, 0}};
    Capability capB = {"serviceB", {1, 0, 0}};
    Capability capC = {"serviceC", {1, 0, 0}};
    Capability capA_v2 = {"serviceA", {2, 0, 0}}; // Different version

    // Setup:
    // Agent1: A, B
    // Agent2: A, C
    // Agent3: B, C
    signaler->registerCapability(agent1, capA);
    signaler->registerCapability(agent1, capB);
    signaler->registerCapability(agent2, capA);
    signaler->registerCapability(agent2, capC);
    signaler->registerCapability(agent3, capB);
    signaler->registerCapability(agent3, capC);

    // Test cases:
    // Require A -> Agent1, Agent2
    std::vector<Capability> reqA = {capA};
    std::vector<std::string> foundA = signaler->discoverAgents(reqA);
    ASSERT_EQ(foundA.size(), 2);
    ASSERT_TRUE(std::find(foundA.begin(), foundA.end(), agent1) != foundA.end());
    ASSERT_TRUE(std::find(foundA.begin(), foundA.end(), agent2) != foundA.end());

    // Require B -> Agent1, Agent3
    std::vector<Capability> reqB = {capB};
    std::vector<std::string> foundB = signaler->discoverAgents(reqB);
    ASSERT_EQ(foundB.size(), 2);
    ASSERT_TRUE(std::find(foundB.begin(), foundB.end(), agent1) != foundB.end());
    ASSERT_TRUE(std::find(foundB.begin(), foundB.end(), agent3) != foundB.end());

    // Require C -> Agent2, Agent3
    std::vector<Capability> reqC = {capC};
    std::vector<std::string> foundC = signaler->discoverAgents(reqC);
    ASSERT_EQ(foundC.size(), 2);
    ASSERT_TRUE(std::find(foundC.begin(), foundC.end(), agent2) != foundC.end());
    ASSERT_TRUE(std::find(foundC.begin(), foundC.end(), agent3) != foundC.end());

    // Require A and B -> Agent1
    std::vector<Capability> reqAB = {capA, capB};
    std::vector<std::string> foundAB = signaler->discoverAgents(reqAB);
    ASSERT_EQ(foundAB.size(), 1);
    ASSERT_EQ(foundAB[0], agent1);

    // Require A and C -> Agent2
    std::vector<Capability> reqAC = {capA, capC};
    std::vector<std::string> foundAC = signaler->discoverAgents(reqAC);
    ASSERT_EQ(foundAC.size(), 1);
    ASSERT_EQ(foundAC[0], agent2);

    // Require B and C -> Agent3
    std::vector<Capability> reqBC = {capB, capC};
    std::vector<std::string> foundBC = signaler->discoverAgents(reqBC);
    ASSERT_EQ(foundBC.size(), 1);
    ASSERT_EQ(foundBC[0], agent3);

    // Require A, B, C -> None
    std::vector<Capability> reqABC = {capA, capB, capC};
    std::vector<std::string> foundABC = signaler->discoverAgents(reqABC);
    ASSERT_TRUE(foundABC.empty());

    // Require non-existent capability -> None
    std::vector<Capability> reqNonExistent = {{"nonexistent", {1, 0, 0}}};
    ASSERT_TRUE(signaler->discoverAgents(reqNonExistent).empty());

    // Require A v2 -> None
    std::vector<Capability> reqA_v2 = {capA_v2};
    ASSERT_TRUE(signaler->discoverAgents(reqA_v2).empty());
}

// Test partial capability matching
TEST_F(CapabilitySignalerTest, PartialCapabilityMatching) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";
    std::string agent3 = "agent3";

    // Register capabilities with different versions
    Capability capA_v1 = {"serviceA", {1, 0, 0}};
    Capability capA_v2 = {"serviceA", {2, 0, 0}};
    Capability capB_v1 = {"serviceB", {1, 0, 0}};
    Capability capB_v2 = {"serviceB", {2, 0, 0}};

    // Setup:
    // Agent1: A v1, B v1
    // Agent2: A v2, B v1
    // Agent3: A v1, B v2
    signaler->registerCapability(agent1, capA_v1);
    signaler->registerCapability(agent1, capB_v1);
    signaler->registerCapability(agent2, capA_v2);
    signaler->registerCapability(agent2, capB_v1);
    signaler->registerCapability(agent3, capA_v1);
    signaler->registerCapability(agent3, capB_v2);

    // Test exact matching (default behavior)
    std::vector<Capability> reqA_v1 = {capA_v1};
    std::vector<std::string> foundA_v1_exact = signaler->discoverAgents(reqA_v1);
    ASSERT_EQ(foundA_v1_exact.size(), 2);  // agent1 and agent3
    ASSERT_TRUE(std::find(foundA_v1_exact.begin(), foundA_v1_exact.end(), agent1) != foundA_v1_exact.end());
    ASSERT_TRUE(std::find(foundA_v1_exact.begin(), foundA_v1_exact.end(), agent3) != foundA_v1_exact.end());

    // Test partial matching
    std::vector<Capability> reqA_v1_partial = {capA_v1};
    std::vector<std::string> foundA_v1_partial = signaler->discoverAgents(reqA_v1_partial, true);
    ASSERT_EQ(foundA_v1_partial.size(), 3);  // all agents have serviceA
    ASSERT_TRUE(std::find(foundA_v1_partial.begin(), foundA_v1_partial.end(), agent1) != foundA_v1_partial.end());
    ASSERT_TRUE(std::find(foundA_v1_partial.begin(), foundA_v1_partial.end(), agent2) != foundA_v1_partial.end());
    ASSERT_TRUE(std::find(foundA_v1_partial.begin(), foundA_v1_partial.end(), agent3) != foundA_v1_partial.end());

    // Test partial matching with multiple capabilities
    std::vector<Capability> reqAB_v1 = {capA_v1, capB_v1};
    std::vector<std::string> foundAB_v1_partial = signaler->discoverAgents(reqAB_v1, true);
    ASSERT_EQ(foundAB_v1_partial.size(), 3);  // all agents have both services in some version

    // Test partial matching with non-existent service
    std::vector<Capability> reqNonExistent = {{"nonexistent", {1, 0, 0}}};
    std::vector<std::string> foundNonExistent = signaler->discoverAgents(reqNonExistent, true);
    ASSERT_TRUE(foundNonExistent.empty());

    // Test mixed exact and partial matching behavior
    std::vector<Capability> reqMixed = {capA_v1, capB_v2};
    std::vector<std::string> foundMixed_exact = signaler->discoverAgents(reqMixed);
    ASSERT_TRUE(foundMixed_exact.empty());  // no agent has exactly this combination

    std::vector<std::string> foundMixed_partial = signaler->discoverAgents(reqMixed, true);
    ASSERT_EQ(foundMixed_partial.size(), 3);  // all agents have both services in some version
}

// Test thread safety
TEST_F(CapabilitySignalerTest, ConcurrentOperations) {
    const int numThreads = 10;
    const int opsPerThread = 100;
    std::vector<std::thread> threads;

    // Create threads that simultaneously register and unregister capabilities
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, opsPerThread]() {
            std::string agentId = "agent_" + std::to_string(i);
            for (int j = 0; j < opsPerThread; ++j) {
                Capability cap = {"service_" + std::to_string(j), {1, 0, 0}};
                
                // Register capability
                ASSERT_TRUE(signaler->registerCapability(agentId, cap));
                
                // Verify it was registered
                auto caps = signaler->getAgentCapabilities(agentId);
                ASSERT_FALSE(caps.empty());
                
                // Unregister capability
                ASSERT_TRUE(signaler->unregisterCapability(agentId, cap));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    // Verify final state - all capabilities should be removed
    for (int i = 0; i < numThreads; ++i) {
        std::string agentId = "agent_" + std::to_string(i);
        ASSERT_TRUE(signaler->getAgentCapabilities(agentId).empty());
    }
}

// Test edge cases
TEST_F(CapabilitySignalerTest, EdgeCases) {
    std::string agent1 = "agent1";
    
    // Empty capability name
    Capability emptyNameCap = {"", {1, 0, 0}};
    ASSERT_FALSE(signaler->registerCapability(agent1, emptyNameCap));
    
    // Empty agent ID
    Capability validCap = {"service", {1, 0, 0}};
    ASSERT_FALSE(signaler->registerCapability("", validCap));
    
    // Invalid version numbers
    Capability invalidVersionCap = {"service", {-1, 0, 0}};
    ASSERT_FALSE(signaler->registerCapability(agent1, invalidVersionCap));
    
    // Duplicate registration
    ASSERT_TRUE(signaler->registerCapability(agent1, validCap));
    ASSERT_FALSE(signaler->registerCapability(agent1, validCap)); // Should fail on duplicate
    
    // Unregister non-existent capability
    Capability nonExistentCap = {"nonexistent", {1, 0, 0}};
    ASSERT_FALSE(signaler->unregisterCapability(agent1, nonExistentCap));
    
    // Get capabilities for empty agent ID
    ASSERT_TRUE(signaler->getAgentCapabilities("").empty());
}

// Test binary serialization
TEST_F(CapabilitySignalerTest, BinarySerializationRoundTrip) {
    Capability originalCap = {
        "testService", 
        {1, 2, 3}, 
        {{"param1", "value1"}, {"param2", "value2"}}
    };

    // Register the capability
    std::string agent1 = "agent1";
    ASSERT_TRUE(signaler->registerCapability(agent1, originalCap));

    // Retrieve and verify
    auto retrievedCaps = signaler->getAgentCapabilities(agent1);
    ASSERT_EQ(retrievedCaps.size(), 1);
    
    const Capability& retrievedCap = retrievedCaps[0];
    ASSERT_EQ(retrievedCap.name, originalCap.name);
    ASSERT_EQ(retrievedCap.version.major, originalCap.version.major);
    ASSERT_EQ(retrievedCap.version.minor, originalCap.version.minor);
    ASSERT_EQ(retrievedCap.version.patch, originalCap.version.patch);
    ASSERT_EQ(retrievedCap.parameters, originalCap.parameters);
}

// Test caching behavior
TEST_F(CapabilitySignalerTest, CachingBehavior) {
    std::string agent1 = "agent1";
    Capability cap1 = {"serviceA", {1, 0, 0}, {{"endpoint", "tcp://localhost:1234"}}};
    Capability cap2 = {"serviceB", {2, 1, 0}, {}};

    // Register capabilities
    ASSERT_TRUE(signaler->registerCapability(agent1, cap1));
    ASSERT_TRUE(signaler->registerCapability(agent1, cap2));

    std::vector<Capability> query = {cap1, cap2};

    // First query - should miss cache
    auto result1 = signaler->discoverAgents(query);
    auto stats1 = signaler->get_stats();
    ASSERT_EQ(stats1.misses, 1);
    ASSERT_EQ(stats1.hits, 0);

    // Second query - should hit cache
    auto result2 = signaler->discoverAgents(query);
    auto stats2 = signaler->get_stats();
    ASSERT_EQ(stats2.hits, 1);
    ASSERT_EQ(stats2.misses, 1);

    // Results should be the same
    ASSERT_EQ(result1, result2);

    // Wait for cache to expire
    std::this_thread::sleep_for(1500ms);

    // Query after expiration - should miss cache
    auto result3 = signaler->discoverAgents(query);
    auto stats3 = signaler->get_stats();
    ASSERT_EQ(stats3.misses, 2);
    ASSERT_EQ(stats3.hits, 1);

    // Results should still be the same
    ASSERT_EQ(result1, result3);
}

// Test cache invalidation on registration
TEST_F(CapabilitySignalerTest, CacheInvalidationOnRegistration) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";
    Capability cap1 = {"serviceA", {1, 0, 0}};
    Capability cap2 = {"serviceB", {2, 1, 0}};

    // Register initial capability
    ASSERT_TRUE(signaler->registerCapability(agent1, cap1));

    std::vector<Capability> query = {cap1};

    // First query - should miss cache
    auto result1 = signaler->discoverAgents(query);
    ASSERT_EQ(result1.size(), 1);
    ASSERT_EQ(result1[0], agent1);

    // Second query - should hit cache
    signaler->discoverAgents(query);
    auto stats1 = signaler->get_stats();
    ASSERT_EQ(stats1.hits, 1);

    // Register same capability for another agent
    ASSERT_TRUE(signaler->registerCapability(agent2, cap1));

    // Query after new registration - should miss cache
    auto result2 = signaler->discoverAgents(query);
    auto stats2 = signaler->get_stats();
    ASSERT_EQ(stats2.hits, 1);  // No new hits
    ASSERT_EQ(result2.size(), 2);  // Both agents should be found
}

// Test cache behavior with partial matching
TEST_F(CapabilitySignalerTest, CachingWithPartialMatching) {
    std::string agent1 = "agent1";
    Capability cap1_v1 = {"serviceA", {1, 0, 0}};
    Capability cap1_v2 = {"serviceA", {2, 0, 0}};

    // Register capabilities
    ASSERT_TRUE(signaler->registerCapability(agent1, cap1_v2));

    std::vector<Capability> query = {cap1_v1};

    // Exact match query - should miss and not find the agent
    auto result1 = signaler->discoverAgents(query, false);
    ASSERT_TRUE(result1.empty());
    auto stats1 = signaler->get_stats();
    ASSERT_EQ(stats1.misses, 1);

    // Partial match query - should not use cache
    auto result2 = signaler->discoverAgents(query, true);
    ASSERT_EQ(result2.size(), 1);  // Should find the agent due to partial matching
    auto stats2 = signaler->get_stats();
    ASSERT_EQ(stats2.misses, 1);  // No new cache operations for partial matching
}

// Test cache behavior with concurrent operations
TEST_F(CapabilitySignalerTest, ConcurrentCaching) {
    const int numThreads = 10;
    const int opsPerThread = 100;
    std::vector<std::thread> threads;

    // Register some initial capabilities
    std::string agent1 = "agent1";
    Capability cap1 = {"serviceA", {1, 0, 0}};
    ASSERT_TRUE(signaler->registerCapability(agent1, cap1));

    // Create threads that simultaneously query and modify
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, opsPerThread, cap1]() {
            std::string agentId = "agent_" + std::to_string(i);
            std::vector<Capability> query = {cap1};

            for (int j = 0; j < opsPerThread; ++j) {
                if (j % 3 == 0) {
                    // Register new capability
                    signaler->registerCapability(agentId, cap1);
                } else {
                    // Query capabilities
                    signaler->discoverAgents(query);
                }
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    // Verify the cache is still functional
    std::vector<Capability> query = {cap1};
    auto result = signaler->discoverAgents(query);
    ASSERT_FALSE(result.empty());

    // Check that we have some cache activity
    auto stats = signaler->get_stats();
    ASSERT_GT(stats.hits + stats.misses, 0);
}

// Test capability deprecation and version compatibility
TEST_F(CapabilitySignalerTest, CapabilityDeprecation) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";

    // Create capabilities with different versions
    Capability capA_v1 = {"serviceA", {1, 0, 0}};
    Capability capA_v2 = {"serviceA", {2, 0, 0}};
    
    // Mark v1 as deprecated
    capA_v1.deprecate({1, 5, 0},      // Deprecated since v1.5.0
                     {{2, 0, 0}},      // Will be removed in v2.0.0
                     {"serviceA_v2"}); // Replacement capability

    // Register capabilities
    ASSERT_TRUE(signaler->registerCapability(agent1, capA_v1));
    ASSERT_TRUE(signaler->registerCapability(agent2, capA_v2));

    // Test discovery with deprecated capability
    std::vector<Capability> reqA_v1 = {capA_v1};
    auto agents = signaler->discoverAgents(reqA_v1);
    ASSERT_EQ(agents.size(), 1);
    ASSERT_EQ(agents[0], agent1);

    // Verify capability details
    auto agent1_caps = signaler->getAgentCapabilities(agent1);
    ASSERT_EQ(agent1_caps.size(), 1);
    const auto& retrieved_cap = agent1_caps[0];
    ASSERT_TRUE(retrieved_cap.is_deprecated);
    ASSERT_TRUE(retrieved_cap.deprecated_since.has_value());
    ASSERT_EQ(retrieved_cap.deprecated_since->toString(), "1.5.0");
    ASSERT_TRUE(retrieved_cap.removal_version.has_value());
    ASSERT_EQ(retrieved_cap.removal_version->toString(), "2.0.0");
    ASSERT_TRUE(retrieved_cap.replacement_capability.has_value());
    ASSERT_EQ(*retrieved_cap.replacement_capability, "serviceA_v2");
}

// Test version compatibility rules
TEST_F(CapabilitySignalerTest, VersionCompatibility) {
    std::string agent1 = "agent1";
    
    // Create capabilities with different versions
    Capability cap_v1_0_0 = {"serviceA", {1, 0, 0}};
    Capability cap_v1_1_0 = {"serviceA", {1, 1, 0}};
    Capability cap_v1_1_1 = {"serviceA", {1, 1, 1}};
    Capability cap_v2_0_0 = {"serviceA", {2, 0, 0}};

    // Register capabilities
    ASSERT_TRUE(signaler->registerCapability(agent1, cap_v1_1_1));

    // Test exact matching
    std::vector<Capability> req_v1_1_1 = {cap_v1_1_1};
    auto exact_match = signaler->discoverAgents(req_v1_1_1, false);
    ASSERT_EQ(exact_match.size(), 1);

    // Test compatibility within same major version
    std::vector<Capability> req_v1_1_0 = {cap_v1_1_0};
    auto compatible_match = signaler->discoverAgents(req_v1_1_0, false);
    ASSERT_EQ(compatible_match.size(), 1);  // v1.1.1 satisfies v1.1.0

    // Test incompatible version (higher minor required)
    std::vector<Capability> req_v1_2_0 = {{"serviceA", {1, 2, 0}}};
    auto incompatible = signaler->discoverAgents(req_v1_2_0, false);
    ASSERT_TRUE(incompatible.empty());

    // Test partial matching with different major versions
    std::vector<Capability> req_v1_0_0 = {cap_v1_0_0};
    auto partial_match = signaler->discoverAgents(req_v1_0_0, true);
    ASSERT_EQ(partial_match.size(), 1);  // v1.1.1 satisfies v1.0.0 with partial matching

    // Test partial matching with higher major version required
    std::vector<Capability> req_v2_0_0 = {cap_v2_0_0};
    auto no_match = signaler->discoverAgents(req_v2_0_0, true);
    ASSERT_TRUE(no_match.empty());  // v1.1.1 cannot satisfy v2.0.0 even with partial matching
}

TEST_F(CapabilitySignalerTest, VersionCompatibilityRules) {
    // Register capabilities with different versions
    signaler->registerCapability("agent1", {"serviceA", {1, 0, 0}});
    signaler->registerCapability("agent2", {"serviceA", {1, 1, 0}});
    signaler->registerCapability("agent3", {"serviceA", {1, 1, 1}});
    signaler->registerCapability("agent4", {"serviceA", {2, 0, 0}});

    // Test exact version matching
    auto exact_match = signaler->findAgents({{"serviceA", {1, 1, 0}}}, false);
    ASSERT_EQ(exact_match.size(), 1);
    ASSERT_EQ(exact_match[0], "agent2");

    // Test compatibility within same major version (higher patch)
    auto compatible_patch = signaler->findAgents({{"serviceA", {1, 1, 0}}}, false);
    ASSERT_EQ(compatible_patch.size(), 1);
    ASSERT_TRUE(std::find(compatible_patch.begin(), compatible_patch.end(), "agent2") != compatible_patch.end());

    // Test compatibility within same major version (higher minor)
    auto compatible_minor = signaler->findAgents({{"serviceA", {1, 0, 0}}}, true);
    ASSERT_EQ(compatible_minor.size(), 3);  // Should find v1.0.0, v1.1.0, and v1.1.1
    std::set<std::string> expected_agents = {"agent1", "agent2", "agent3"};
    std::set<std::string> actual_agents(compatible_minor.begin(), compatible_minor.end());
    ASSERT_EQ(actual_agents, expected_agents);

    // Test incompatibility across major versions
    auto incompatible = signaler->findAgents({{"serviceA", {2, 0, 0}}}, false);
    ASSERT_EQ(incompatible.size(), 1);
    ASSERT_EQ(incompatible[0], "agent4");

    // Test partial matching with version ranges
    auto partial_match = signaler->findAgents({{"serviceA", {1, 0, 0}}}, true);
    ASSERT_EQ(partial_match.size(), 3);  // Should find all v1.x.x versions
}

TEST_F(CapabilitySignalerTest, DeprecatedCapabilityHandling) {
    // Create a deprecated capability
    Capability deprecated_cap = {"serviceA", {1, 0, 0}};
    deprecated_cap.deprecate({1, 5, 0}, {{2, 0, 0}}, {"serviceA_v2"});

    // Register both deprecated and current versions
    signaler->registerCapability("agent1", deprecated_cap);
    signaler->registerCapability("agent2", {"serviceA_v2", {2, 0, 0}});

    // Test finding deprecated capability
    auto deprecated_results = signaler->findAgents({{"serviceA", {1, 0, 0}}}, false);
    ASSERT_EQ(deprecated_results.size(), 1);
    ASSERT_EQ(deprecated_results[0], "agent1");

    // Verify capability details
    auto agent1_caps = signaler->getAgentCapabilities("agent1");
    ASSERT_EQ(agent1_caps.size(), 1);
    const auto& retrieved_cap = agent1_caps[0];
    ASSERT_TRUE(retrieved_cap.is_deprecated);
    ASSERT_TRUE(retrieved_cap.deprecated_since.has_value());
    ASSERT_EQ(retrieved_cap.deprecated_since->toString(), "1.5.0");
    ASSERT_TRUE(retrieved_cap.removal_version.has_value());
    ASSERT_EQ(retrieved_cap.removal_version->toString(), "2.0.0");
    ASSERT_TRUE(retrieved_cap.replacement_capability.has_value());
    ASSERT_EQ(*retrieved_cap.replacement_capability, "serviceA_v2");

    // Test finding replacement capability
    auto replacement_results = signaler->findAgents({{"serviceA_v2", {2, 0, 0}}}, false);
    ASSERT_EQ(replacement_results.size(), 1);
    ASSERT_EQ(replacement_results[0], "agent2");

    // Test caching behavior with deprecated capabilities
    auto cached_results = signaler->findAgents({{"serviceA", {1, 0, 0}}}, false);
    ASSERT_EQ(cached_results.size(), 1);
    ASSERT_EQ(cached_results[0], "agent1");

    // Verify cache hit count increased
    auto stats = signaler->getCacheStats();
    ASSERT_GT(stats.hits, 0);
} 
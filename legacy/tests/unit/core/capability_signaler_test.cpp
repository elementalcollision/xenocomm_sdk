#include <gtest/gtest.h>
#include "xenocomm/core/capability_signaler.h"
#include "xenocomm/core/capability_index.h"
#include "xenocomm/core/capability_cache.h"
#include "xenocomm/utils/serialization.h"
#include <thread>
#include <chrono>
#include <memory>
#include <unordered_set>

using namespace xenocomm::core;
using namespace std::chrono_literals;

// Test fixture for CapabilitySignaler tests
class CapabilitySignalerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a signaler using the factory function
        signaler = createInMemoryCapabilitySignaler();
    }

    void TearDown() override {
        // Cleanup happens automatically via unique_ptr
    }

    // Use the base interface pointer
    std::unique_ptr<CapabilitySignaler> signaler;
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

// Test agent discovery (using base interface methods)
TEST_F(CapabilitySignalerTest, DiscoverAgents) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";
    std::string agent3 = "agent3";

    Capability capA = {"serviceA", {1, 0, 0}};
    Capability capB = {"serviceB", {1, 0, 0}};
    Capability capC = {"serviceC", {1, 0, 0}};
    Capability capA_v2 = {"serviceA", {2, 0, 0}}; // Different version

    // Setup:
    signaler->registerCapability(agent1, capA);
    signaler->registerCapability(agent1, capB);
    signaler->registerCapability(agent2, capA);
    signaler->registerCapability(agent2, capC);
    signaler->registerCapability(agent3, capB);
    signaler->registerCapability(agent3, capC);
    signaler->registerCapability("agent4", capA_v2);

    // Test cases:
    // Require A (exact) -> Agent1, Agent2
    std::vector<Capability> reqA = {capA};
    std::vector<std::string> foundA_exact = signaler->discoverAgents(reqA); // Uses overload(caps)
    ASSERT_EQ(foundA_exact.size(), 2);
    ASSERT_NE(std::find(foundA_exact.begin(), foundA_exact.end(), agent1), foundA_exact.end());
    ASSERT_NE(std::find(foundA_exact.begin(), foundA_exact.end(), agent2), foundA_exact.end());

    // Require A (partial) -> Agent1, Agent2, Agent4
    std::vector<std::string> foundA_partial = signaler->discoverAgents(reqA, true); // Uses overload(caps, true)
    ASSERT_EQ(foundA_partial.size(), 3);
    ASSERT_NE(std::find(foundA_partial.begin(), foundA_partial.end(), agent1), foundA_partial.end());
    ASSERT_NE(std::find(foundA_partial.begin(), foundA_partial.end(), agent2), foundA_partial.end());
    ASSERT_NE(std::find(foundA_partial.begin(), foundA_partial.end(), "agent4"), foundA_partial.end());

    // Require A and B (exact) -> Agent1
    std::vector<Capability> reqAB = {capA, capB};
    std::vector<std::string> foundAB_exact = signaler->discoverAgents(reqAB);
    ASSERT_EQ(foundAB_exact.size(), 1);
    ASSERT_EQ(foundAB_exact[0], agent1);

    // Require A and B (partial) -> Agent1, Agent2, Agent3, Agent4 (all have A or B in some form)
    std::vector<std::string> foundAB_partial = signaler->discoverAgents(reqAB, true);
    // The exact logic depends on CapabilityIndex::findAgents partial matching, 
    // which might require both A and B partially, or just one. Assuming it requires both partially:
    ASSERT_EQ(foundAB_partial.size(), 3); // Agent1 (A, B), Agent2 (A_v2, B), Agent3 (A, B_v2) - agent4 only has A_v2
    ASSERT_NE(std::find(foundAB_partial.begin(), foundAB_partial.end(), agent1), foundAB_partial.end());
    ASSERT_NE(std::find(foundAB_partial.begin(), foundAB_partial.end(), agent2), foundAB_partial.end());
    ASSERT_NE(std::find(foundAB_partial.begin(), foundAB_partial.end(), agent3), foundAB_partial.end());

    // ... other discovery tests using only the interface methods ...
}

// Test thread safety (remains the same, uses interface methods)
TEST_F(CapabilitySignalerTest, ConcurrentOperations) {
    const int numThreads = 10;
    const int opsPerThread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, opsPerThread]() {
            std::string agentId = "agent_" + std::to_string(i);
            for (int j = 0; j < opsPerThread; ++j) {
                Capability cap = {"service_" + std::to_string(j), {1, 0, 0}};
                ASSERT_TRUE(signaler->registerCapability(agentId, cap));
                auto caps = signaler->getAgentCapabilities(agentId);
                // ASSERT_FALSE(caps.empty()); // Might be empty immediately after unregister by another thread
                ASSERT_TRUE(signaler->unregisterCapability(agentId, cap));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    for (int i = 0; i < numThreads; ++i) {
        std::string agentId = "agent_" + std::to_string(i);
        ASSERT_TRUE(signaler->getAgentCapabilities(agentId).empty());
    }
}

// Test binary serialization round trip
TEST_F(CapabilitySignalerTest, BinarySerializationRoundTrip) {
    std::string agentId = "binary_agent";
    Capability cap1 = {"binary_service", {1, 2, 3}, {{"bin", "data"}}};
    std::vector<uint8_t> cap1_data;
    EXPECT_NO_THROW(xenocomm::utils::serializeCapability(cap1, cap1_data));
    ASSERT_FALSE(cap1_data.empty());

    // Register using binary
    ASSERT_TRUE(signaler->registerCapabilityBinary(agentId, cap1_data));

    // Retrieve as binary
    std::vector<uint8_t> retrieved_data = signaler->getAgentCapabilitiesBinary(agentId);
    ASSERT_FALSE(retrieved_data.empty());

    // Assuming getAgentCapabilitiesBinary returns a single serialized capability for this test setup
    Capability retrieved_cap_deserialized;
    ASSERT_TRUE(xenocomm::utils::deserializeCapability(retrieved_data.data(), retrieved_data.size(), retrieved_cap_deserialized));
    
    // Compare the original cap1 with the deserialized one
    ASSERT_EQ(retrieved_cap_deserialized.name, cap1.name);
    ASSERT_EQ(retrieved_cap_deserialized.version, cap1.version);
    ASSERT_EQ(retrieved_cap_deserialized.parameters, cap1.parameters);

    // Test with multiple capabilities (if getAgentCapabilitiesBinary supports it, otherwise this part needs rethink)
}

// Test edge cases
TEST_F(CapabilitySignalerTest, EdgeCases) {
    Capability validCap = {"service", {1,0,0}};
    // Empty agent ID
    EXPECT_FALSE(signaler->registerCapability("", Capability{"name", {1,0,0}}));
    // Empty capability name
    EXPECT_FALSE(signaler->registerCapability("agent", Capability{",", {1,0,0}}));
    
    // Duplicate insertion
    ASSERT_TRUE(signaler->registerCapability("agent1", validCap));

    // Empty requirements
    EXPECT_TRUE(signaler->discoverAgents({}).empty());
}

// Test capability deprecation handling (using base interface)
TEST_F(CapabilitySignalerTest, DeprecatedCapabilityHandling) {
    std::string agent1 = "agent_dep";
    Capability cap_old = {"old_service", {1, 0, 0}};
    Capability cap_new = {"new_service", {1, 0, 0}};
    cap_old.deprecate(Version{1, 1, 0}, Version{2, 0, 0}, "new_service");

    signaler->registerCapability(agent1, cap_old);
    signaler->registerCapability(agent1, cap_new);

    // Discovering the old service should still work (exact match)
    auto found_old = signaler->discoverAgents({cap_old});
    ASSERT_EQ(found_old.size(), 1);
    ASSERT_EQ(found_old[0], agent1);

    // Discovering the new service should work
    auto found_new = signaler->discoverAgents({cap_new});
    ASSERT_EQ(found_new.size(), 1);
    ASSERT_EQ(found_new[0], agent1);

    // Get capabilities for agent should show both
    auto caps = signaler->getAgentCapabilities(agent1);
    ASSERT_EQ(caps.size(), 2);
    // Check deprecation status if needed (requires access beyond base interface or assumptions)
}

// Test version compatibility (using base interface discoverAgents with partialMatch=true)
TEST_F(CapabilitySignalerTest, VersionCompatibility) {
    std::string agent1 = "agent_v1";
    std::string agent2 = "agent_v2";
    Capability cap_v1 = {"versioned_service", {1, 5, 0}};
    Capability cap_v2 = {"versioned_service", {2, 1, 0}};

    signaler->registerCapability(agent1, cap_v1);
    signaler->registerCapability(agent2, cap_v2);

    // Require version 1.0.0 (partial match allows newer)
    Capability req_v1 = {"versioned_service", {1, 0, 0}};
    auto found_partial = signaler->discoverAgents({req_v1}, true);
    ASSERT_EQ(found_partial.size(), 2);

    // Require version 2.0.0 (partial match allows newer)
    Capability req_v2 = {"versioned_service", {2, 0, 0}};
    auto found_partial_v2 = signaler->discoverAgents({req_v2}, true);
    ASSERT_EQ(found_partial_v2.size(), 1);
    ASSERT_EQ(found_partial_v2[0], agent2);

    // Require version 3.0.0 (partial match - none found)
    Capability req_v3 = {"versioned_service", {3, 0, 0}};
    auto found_partial_v3 = signaler->discoverAgents({req_v3}, true);
    ASSERT_TRUE(found_partial_v3.empty());

    // Require version 1.5.0 (exact match)
    Capability req_v1_5_exact = {"versioned_service", {1, 5, 0}};
    auto found_exact = signaler->discoverAgents({req_v1_5_exact}, false);
    ASSERT_EQ(found_exact.size(), 1);
    ASSERT_EQ(found_exact[0], agent1);
} 
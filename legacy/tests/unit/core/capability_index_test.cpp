#include <gtest/gtest.h>
#include <thread>
#include "xenocomm/core/capability_index.h"
#include "xenocomm/utils/serialization.h"

using namespace xenocomm::core;
using namespace std::string_literals;

class CapabilityIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        index = std::make_unique<CapabilityIndex>();
    }

    void TearDown() override {
        // Cleanup happens automatically via unique_ptr
    }

    std::unique_ptr<CapabilityIndex> index;
};

// Test basic insertion and retrieval
TEST_F(CapabilityIndexTest, InsertAndRetrieve) {
    std::string agent1 = "agent1";
    Capability cap1 = {"serviceA", {1, 0, 0}, {{"endpoint", "tcp://localhost:1234"}}};
    Capability cap2 = {"serviceB", {2, 1, 0}, {}};

    // Insert capabilities
    ASSERT_TRUE(index->addCapability(agent1, cap1));
    ASSERT_TRUE(index->addCapability(agent1, cap2));

    // Retrieve capabilities for agent
    auto agentCaps = index->getAgentCapabilities(agent1);
    ASSERT_EQ(agentCaps.size(), 2);
    std::unordered_set<Capability> capsSet(agentCaps.begin(), agentCaps.end());
    ASSERT_TRUE(capsSet.count(cap1));
    ASSERT_TRUE(capsSet.count(cap2));

    // Test non-existent agent
    ASSERT_TRUE(index->getAgentCapabilities("nonexistent").empty());
}

// Test capability removal
TEST_F(CapabilityIndexTest, RemoveCapability) {
    std::string agent1 = "agent1";
    Capability cap1 = {"serviceA", {1, 0, 0}};
    Capability cap2 = {"serviceB", {2, 1, 0}};

    // Setup
    index->addCapability(agent1, cap1);
    index->addCapability(agent1, cap2);
    ASSERT_EQ(index->getAgentCapabilities(agent1).size(), 2);

    // Remove first capability
    ASSERT_TRUE(index->removeCapability(agent1, cap1));
    auto remaining = index->getAgentCapabilities(agent1);
    ASSERT_EQ(remaining.size(), 1);
    ASSERT_EQ(remaining[0].name, cap2.name);

    // Remove second capability
    ASSERT_TRUE(index->removeCapability(agent1, cap2));
    ASSERT_TRUE(index->getAgentCapabilities(agent1).empty());

    // Try removing non-existent capabilities
    ASSERT_FALSE(index->removeCapability(agent1, cap1));
    ASSERT_FALSE(index->removeCapability("nonexistent", cap1));
}

// Test capability matching
TEST_F(CapabilityIndexTest, CapabilityMatching) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";
    std::string agent3 = "agent3";

    Capability capA1 = {"serviceA", {1, 0, 0}};
    Capability capA2 = {"serviceA", {1, 1, 0}};
    Capability capB1 = {"serviceB", {1, 0, 0}};
    Capability capC1 = {"serviceC", {1, 0, 0}};

    // Setup:
    // Agent1: A1, B1
    // Agent2: A2, B1, C1
    // Agent3: B1, C1
    index->addCapability(agent1, capA1);
    index->addCapability(agent1, capB1);
    index->addCapability(agent2, capA2);
    index->addCapability(agent2, capB1);
    index->addCapability(agent2, capC1);
    index->addCapability(agent3, capB1);
    index->addCapability(agent3, capC1);

    // Test single capability matching
    std::vector<Capability> reqB1 = {capB1};
    auto agentsB1 = index->findAgents(reqB1, false);
    ASSERT_EQ(agentsB1.size(), 3);
    std::sort(agentsB1.begin(), agentsB1.end());
    ASSERT_EQ(agentsB1[0], agent1.c_str());
    ASSERT_EQ(agentsB1[1], agent2.c_str());
    ASSERT_EQ(agentsB1[2], agent3.c_str());

    // Test multiple capability matching
    std::vector<Capability> reqA1B1 = {capA1, capB1};
    auto agentsA1B1 = index->findAgents(reqA1B1, false);
    ASSERT_EQ(agentsA1B1.size(), 1);
    ASSERT_EQ(agentsA1B1[0], agent1.c_str());

    // Test version-specific matching
    std::vector<Capability> reqA2C1 = {capA2, capC1};
    auto agentsA2C1 = index->findAgents(reqA2C1, false);
    ASSERT_EQ(agentsA2C1.size(), 1);
    ASSERT_EQ(agentsA2C1[0], agent2.c_str());

    // Test non-existent capability
    std::vector<Capability> reqNonExistent = {{"nonexistent", {1, 0, 0}}};
    ASSERT_TRUE(index->findAgents(reqNonExistent, false).empty());

    // Test impossible combination
    std::vector<Capability> reqA1C1 = {capA1, capC1};
    ASSERT_TRUE(index->findAgents(reqA1C1, false).empty());
}

// Test concurrent operations
TEST_F(CapabilityIndexTest, ConcurrentOperations) {
    const int numThreads = 10;
    const int opsPerThread = 100;
    std::vector<std::thread> threads;

    // Create threads that simultaneously insert and remove capabilities
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, opsPerThread]() {
            std::string agentId = "agent_" + std::to_string(i);
            for (int j = 0; j < opsPerThread; ++j) {
                Capability cap = {"service_" + std::to_string(j), {1, 0, 0}};
                
                // Insert capability
                ASSERT_TRUE(index->addCapability(agentId, cap));
                
                // Verify it was inserted
                auto caps = index->getAgentCapabilities(agentId);
                ASSERT_FALSE(caps.empty());
                
                // Remove capability
                ASSERT_TRUE(index->removeCapability(agentId, cap));
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
        ASSERT_TRUE(index->getAgentCapabilities(agentId).empty());
    }
}

// Test edge cases
TEST_F(CapabilityIndexTest, EdgeCases) {
    std::string agent1 = "agent1";
    
    // Empty capability name
    Capability emptyNameCap = {"", {1, 0, 0}};
    ASSERT_FALSE(index->addCapability(agent1, emptyNameCap));
    
    // Empty agent ID
    Capability validCap = {"service", {1, 0, 0}};
    ASSERT_FALSE(index->addCapability("", validCap));
    
    // Invalid version numbers
    Capability invalidVersionCap = {"service", {0, 0, 0}};
    ASSERT_FALSE(index->addCapability(agent1, invalidVersionCap));
    
    // Duplicate insertion
    ASSERT_TRUE(index->addCapability(agent1, validCap));
    ASSERT_FALSE(index->addCapability(agent1, validCap)); // Should fail on duplicate
    
    // Remove non-existent capability
    Capability nonExistentCap = {"nonexistent", {1, 0, 0}};
    ASSERT_FALSE(index->removeCapability(agent1, nonExistentCap));
    
    // Get capabilities for empty agent ID
    ASSERT_TRUE(index->getAgentCapabilities("").empty());
}

TEST_F(CapabilityIndexTest, VersionCompatibilityRules) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";
    std::string agent3 = "agent3";

    // Create capabilities with different versions
    Capability cap_v1_0_0 = {"serviceA", {1, 0, 0}};
    Capability cap_v1_1_0 = {"serviceA", {1, 1, 0}};
    Capability cap_v2_0_0 = {"serviceA", {2, 0, 0}};
    Capability cap_v2_1_0 = {"serviceA", {2, 1, 0}};
    Capability cap_v3_0_0 = {"serviceA", {3, 0, 0}};

    // Register capabilities
    ASSERT_TRUE(index->addCapability(agent1, cap_v1_1_0)); // Agent1 has v1.1.0
    ASSERT_TRUE(index->addCapability(agent2, cap_v2_1_0)); // Agent2 has v2.1.0
    ASSERT_TRUE(index->addCapability(agent3, cap_v3_0_0)); // Agent3 has v3.0.0

    // Test exact version matching
    std::vector<Capability> req_v1_1_0 = {cap_v1_1_0};
    auto exact_match = index->findAgents(req_v1_1_0, false);
    ASSERT_EQ(exact_match.size(), 1);
    ASSERT_EQ(exact_match[0], agent1);

    // Test backward compatibility within same major version
    std::vector<Capability> req_v1_0_0 = {cap_v1_0_0};
    auto backward_compat = index->findAgents(req_v1_0_0, true);
    ASSERT_EQ(backward_compat.size(), 1);
    ASSERT_EQ(backward_compat[0], agent1);

    // Test forward compatibility within same major version
    std::vector<Capability> req_v2_0_0 = {cap_v2_0_0};
    auto forward_compat = index->findAgents(req_v2_0_0, true);
    ASSERT_EQ(forward_compat.size(), 1);
    ASSERT_EQ(forward_compat[0], agent2);

    // Test incompatible major versions
    std::vector<Capability> req_v3_0_0 = {cap_v3_0_0};
    auto no_compat = index->findAgents(req_v2_0_0, false);
    ASSERT_TRUE(no_compat.empty());

    // Test partial matching with multiple capabilities
    Capability cap_b_v1_0_0 = {"serviceB", {1, 0, 0}};
    ASSERT_TRUE(index->addCapability(agent1, cap_b_v1_0_0));
    ASSERT_TRUE(index->addCapability(agent2, cap_b_v1_0_0));

    std::vector<Capability> req_multi = {cap_v1_0_0, cap_b_v1_0_0};
    auto multi_match = index->findAgents(req_multi, true);
    ASSERT_EQ(multi_match.size(), 1);
    ASSERT_EQ(multi_match[0], agent1);
}

TEST_F(CapabilityIndexTest, DeprecatedCapabilityHandling) {
    std::string agent1 = "agent1";
    std::string agent2 = "agent2";

    // Create capabilities with different versions
    Capability cap_v1_0_0 = {"serviceA", {1, 0, 0}};
    Capability cap_v2_0_0 = {"serviceA", {2, 0, 0}};

    // Mark v1.0.0 as deprecated
    cap_v1_0_0.deprecate({1, 5, 0},      // Deprecated since v1.5.0
                        {{2, 0, 0}},      // Will be removed in v2.0.0
                        {"serviceA_v2"}); // Replacement capability

    // Register capabilities
    ASSERT_TRUE(index->addCapability(agent1, cap_v1_0_0));
    ASSERT_TRUE(index->addCapability(agent2, cap_v2_0_0));

    // Test discovery with deprecated capability
    std::vector<Capability> req_v1_0_0 = {cap_v1_0_0};
    auto agents = index->findAgents(req_v1_0_0, false);
    ASSERT_EQ(agents.size(), 1);
    ASSERT_EQ(agents[0], agent1);

    // Verify capability details
    auto agent1_caps = index->getAgentCapabilities(agent1);
    ASSERT_EQ(agent1_caps.size(), 1);
    const auto& retrieved_cap = agent1_caps[0];
    ASSERT_TRUE(retrieved_cap.is_deprecated);
    ASSERT_TRUE(retrieved_cap.deprecated_since.has_value());
    ASSERT_EQ(retrieved_cap.deprecated_since->toString(), "1.5.0");
    ASSERT_TRUE(retrieved_cap.removal_version.has_value());
    ASSERT_EQ(retrieved_cap.removal_version->toString(), "2.0.0");
    ASSERT_TRUE(retrieved_cap.replacement_capability.has_value());
    ASSERT_EQ(*retrieved_cap.replacement_capability, "serviceA_v2");

    // Test discovery with partial matching
    auto partial_match = index->findAgents(req_v1_0_0, true);
    ASSERT_EQ(partial_match.size(), 2); // Should find both agents
    std::sort(partial_match.begin(), partial_match.end());
    ASSERT_EQ(partial_match[0], agent1);
    ASSERT_EQ(partial_match[1], agent2);
}

// TEST_F(CapabilityIndexTest, AddAndRetrieveCapability) {
//     index->addCapability("agent1", {"serviceA", {1, 0, 0}});
//     Capability retrievedCap = index->getAgentCapabilities("agent1")[0];
//     ASSERT_EQ(retrievedCap.name, "serviceA");
//     ASSERT_EQ(retrievedCap.version.major, 1);
// }
// 
// TEST_F(CapabilityIndexTest, HandleInvalidVersionInputOriginal) {
//     // Test with a version that might cause issues if not handled (e.g., large numbers)
//     // Using 0 instead of -1 for the major version to avoid narrowing conversion error.
//     Capability invalidVersionCap = {"service", {0, 0, 0}}; // Changed -1 to 0
//     // Depending on CapabilityIndex's validation, this might throw or return false.
//     // For now, let's assume it doesn't throw if validation is internal.
//     // If addCapability returns bool:
//     // EXPECT_FALSE(index->addCapability("agent_invalid_version", invalidVersionCap));
//     // If it might throw for truly malformed data, or if Version itself validates:
//     EXPECT_NO_THROW(index->addCapability("agent_invalid_version_original", invalidVersionCap));
//     // Add assertions here based on expected behavior for "invalid" versions.
//     // For example, check if the capability was actually added or rejected.
//     // auto caps = index->getAgentCapabilities("agent_invalid_version_original");
//     // EXPECT_TRUE(caps.empty()); // or EXPECT_FALSE, depending on handling
// }
// 
// // Test with a capability that has deprecated status
// TEST_F(CapabilityIndexTest, AddDeprecatedCapability) {
//     // ... existing code ...
// }
// 
// TEST_F(CapabilityIndexTest, AddCapabilityWithParameters) {
//     std::map<std::string, std::string> params = {{"key", "value"}};
//     index->addCapability("agent_params", {"service_params", {1,0,0}, params});
//     ASSERT_EQ(index->getAgentCapabilities("agent_params")[0].parameters["key"], "value");
// } 

// The actual end of the file or next valid test should follow here.
// If this is the end, the tool will handle it.
// If there are more tests, they would start un-commented below.

// Example of what might be the next actual test (if any)
// TEST_F(CapabilityIndexTest, SomeOtherRealTest)
// {
//    ...
// } 
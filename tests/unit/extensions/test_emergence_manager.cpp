#include <gtest/gtest.h>
#include "xenocomm/extensions/emergence_manager.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <thread>
#include <fstream>
#include <catch2/catch.hpp>
#include <chrono>

using namespace xenocomm::extensions;

class EmergenceManagerTest : public ::testing::Test {
protected:
    std::string tempPath = "/tmp";
    nlohmann::json evalMetrics = { {"metric", "latency"} };
    EmergenceManager* manager;

    void SetUp() override {
        manager = new EmergenceManager(tempPath, evalMetrics);
    }
    void TearDown() override {
        delete manager;
        // Optionally remove log file
        std::remove((tempPath + "/emergence_manager.log").c_str());
    }
};

TEST_F(EmergenceManagerTest, Construction) {
    EXPECT_NO_THROW(EmergenceManager m(tempPath, evalMetrics));
}

TEST_F(EmergenceManagerTest, ProposeAndRetrieveVariant) {
    ProtocolVariant v("v1", "desc", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v, "desc", nlohmann::json::object());
    auto retrieved = manager->getVariant("v1");
    EXPECT_EQ(retrieved.id, "v1");
    EXPECT_EQ(retrieved.description, "desc");
}

TEST_F(EmergenceManagerTest, DuplicateVariantThrows) {
    ProtocolVariant v("v1", "desc", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v, "desc", nlohmann::json::object());
    EXPECT_THROW(manager->proposeVariant("v1", v, "desc", nlohmann::json::object()), std::invalid_argument);
}

TEST_F(EmergenceManagerTest, GetMissingVariantThrows) {
    EXPECT_THROW(manager->getVariant("missing"), std::out_of_range);
}

TEST_F(EmergenceManagerTest, ListVariantsByStatus) {
    ProtocolVariant v1("v1", "desc1", nlohmann::json::object(), nlohmann::json::object());
    ProtocolVariant v2("v2", "desc2", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v1, "desc1", nlohmann::json::object());
    manager->proposeVariant("v2", v2, "desc2", nlohmann::json::object());
    auto proposed = manager->listVariants(VariantStatus::Proposed);
    EXPECT_EQ(proposed.size(), 2);
    manager->setVariantStatus("v1", VariantStatus::Adopted);
    auto adopted = manager->listVariants(VariantStatus::Adopted);
    EXPECT_EQ(adopted.size(), 1);
    EXPECT_EQ(adopted.begin()->second.id, "v1");
}

TEST_F(EmergenceManagerTest, SetStatusMissingVariantThrows) {
    EXPECT_THROW(manager->setVariantStatus("missing", VariantStatus::Adopted), std::out_of_range);
}

// Performance Logging Tests
TEST_F(EmergenceManagerTest, LogAndRetrievePerformance) {
    // Create a test variant
    ProtocolVariant v1("v1", "test variant", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v1, "test variant", nlohmann::json::object());

    // Create test performance record
    PerformanceMetrics metrics;
    metrics.successRate = 0.95;
    metrics.latencyMs = 100.0;
    metrics.resourceUsage = 0.5;
    metrics.throughput = 1000.0;
    metrics.customMetrics["errorRate"] = 0.02;

    PerformanceRecord record;
    record.timestamp = std::chrono::system_clock::now();
    record.metrics = metrics;
    record.notes = "Test performance record";

    // Log performance
    EXPECT_NO_THROW(manager->logPerformance("v1", record));

    // Retrieve and verify performance history
    auto history = manager->getVariantPerformance("v1");
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history[0].metrics.successRate, 0.95);
    EXPECT_EQ(history[0].metrics.latencyMs, 100.0);
    EXPECT_EQ(history[0].metrics.resourceUsage, 0.5);
    EXPECT_EQ(history[0].metrics.throughput, 1000.0);
    EXPECT_EQ(history[0].metrics.customMetrics["errorRate"], 0.02);
    EXPECT_EQ(history[0].notes, "Test performance record");
}

TEST_F(EmergenceManagerTest, LogPerformanceForNonexistentVariant) {
    PerformanceRecord record;
    EXPECT_THROW(manager->logPerformance("nonexistent", record), std::out_of_range);
}

TEST_F(EmergenceManagerTest, GetPerformanceForNonexistentVariant) {
    EXPECT_THROW(manager->getVariantPerformance("nonexistent"), std::out_of_range);
}

// Evaluation Criteria Tests
TEST_F(EmergenceManagerTest, SetAndGetEvaluationCriteria) {
    EvaluationCriteria criteria;
    criteria.weights = {
        {"successRate", 0.4},
        {"latencyMs", 0.3},
        {"resourceUsage", 0.2},
        {"throughput", 0.1}
    };
    criteria.minSuccessRate = 0.9;
    criteria.maxLatencyMs = 200.0;
    criteria.significanceThreshold = 0.1;

    EXPECT_NO_THROW(manager->setEvaluationCriteria(criteria));
    
    auto retrieved = manager->getEvaluationCriteria();
    EXPECT_EQ(retrieved.weights["successRate"], 0.4);
    EXPECT_EQ(retrieved.weights["latencyMs"], 0.3);
    EXPECT_EQ(retrieved.weights["resourceUsage"], 0.2);
    EXPECT_EQ(retrieved.weights["throughput"], 0.1);
    EXPECT_EQ(retrieved.minSuccessRate, 0.9);
    EXPECT_EQ(retrieved.maxLatencyMs, 200.0);
    EXPECT_EQ(retrieved.significanceThreshold, 0.1);
}

// Best Performing Variant Tests
TEST_F(EmergenceManagerTest, GetBestPerformingVariant) {
    // Create test variants
    ProtocolVariant v1("v1", "variant 1", nlohmann::json::object(), nlohmann::json::object());
    ProtocolVariant v2("v2", "variant 2", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v1, "variant 1", nlohmann::json::object());
    manager->proposeVariant("v2", v2, "variant 2", nlohmann::json::object());

    // Set evaluation criteria
    EvaluationCriteria criteria;
    criteria.weights = {
        {"successRate", 1.0}  // Only consider success rate for simplicity
    };
    criteria.minSuccessRate = 0.0;
    manager->setEvaluationCriteria(criteria);

    // Log performance for both variants
    PerformanceRecord record1;
    record1.metrics.successRate = 0.95;
    manager->logPerformance("v1", record1);

    PerformanceRecord record2;
    record2.metrics.successRate = 0.85;
    manager->logPerformance("v2", record2);

    // Get best performing variant
    auto bestVariant = manager->getBestPerformingVariant(criteria);
    ASSERT_TRUE(bestVariant.has_value());
    EXPECT_EQ(bestVariant.value(), "v1");
}

TEST_F(EmergenceManagerTest, GetBestPerformingVariantWithNoData) {
    EvaluationCriteria criteria;
    auto bestVariant = manager->getBestPerformingVariant(criteria);
    EXPECT_FALSE(bestVariant.has_value());
}

// Significance Testing
TEST_F(EmergenceManagerTest, IsSignificantlyBetter) {
    // Create test variants
    ProtocolVariant v1("v1", "baseline", nlohmann::json::object(), nlohmann::json::object());
    ProtocolVariant v2("v2", "improved", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v1, "baseline", nlohmann::json::object());
    manager->proposeVariant("v2", v2, "improved", nlohmann::json::object());

    // Set evaluation criteria with significance threshold
    EvaluationCriteria criteria;
    criteria.weights = {
        {"successRate", 1.0}
    };
    criteria.significanceThreshold = 0.1;
    manager->setEvaluationCriteria(criteria);

    // Log significantly different performance
    PerformanceRecord baseline;
    baseline.metrics.successRate = 0.80;
    manager->logPerformance("v1", baseline);

    PerformanceRecord improved;
    improved.metrics.successRate = 0.95;
    manager->logPerformance("v2", improved);

    EXPECT_TRUE(manager->isSignificantlyBetter("v2", "v1", criteria));
    EXPECT_FALSE(manager->isSignificantlyBetter("v1", "v2", criteria));
}

// Performance Report Generation
TEST_F(EmergenceManagerTest, GeneratePerformanceReport) {
    // Create test variants
    ProtocolVariant v1("v1", "variant 1", nlohmann::json::object(), nlohmann::json::object());
    ProtocolVariant v2("v2", "variant 2", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v1, "variant 1", nlohmann::json::object());
    manager->proposeVariant("v2", v2, "variant 2", nlohmann::json::object());

    // Log performance data
    PerformanceRecord record1;
    record1.metrics.successRate = 0.95;
    record1.metrics.latencyMs = 100.0;
    record1.notes = "Good performance";
    manager->logPerformance("v1", record1);

    PerformanceRecord record2;
    record2.metrics.successRate = 0.85;
    record2.metrics.latencyMs = 150.0;
    record2.notes = "Average performance";
    manager->logPerformance("v2", record2);

    // Generate and verify report
    std::string report = manager->generatePerformanceReport({"v1", "v2"});
    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("v1"), std::string::npos);
    EXPECT_NE(report.find("v2"), std::string::npos);
    EXPECT_NE(report.find("0.95"), std::string::npos);
    EXPECT_NE(report.find("0.85"), std::string::npos);
}

TEST_F(EmergenceManagerTest, GeneratePerformanceReportWithInvalidVariant) {
    std::vector<std::string> variantIds = {"nonexistent"};
    EXPECT_THROW(manager->generatePerformanceReport(variantIds), std::out_of_range);
}

// Persistence and Sharing Tests
TEST_F(EmergenceManagerTest, SaveAndLoadState) {
    // Create test data
    ProtocolVariant v1("v1", "test variant 1", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v1, "test variant 1", nlohmann::json::object());
    
    PerformanceMetrics metrics;
    metrics.successRate = 0.95;
    metrics.latencyMs = 100.0;
    PerformanceRecord record;
    record.metrics = metrics;
    record.timestamp = std::chrono::system_clock::now();
    manager->logPerformance("v1", record);
    
    // Save state
    manager->saveState();
    
    // Create new manager instance
    auto newManager = std::make_unique<EmergenceManager>(tempPath, nlohmann::json::object());
    
    // Verify loaded state
    EXPECT_NO_THROW({
        auto variant = newManager->getVariant("v1");
        EXPECT_EQ(variant.id, "v1");
        EXPECT_EQ(variant.description, "test variant 1");
        
        auto perf = newManager->getVariantPerformance("v1");
        EXPECT_EQ(perf.size(), 1);
        EXPECT_DOUBLE_EQ(perf[0].metrics.successRate, 0.95);
        EXPECT_DOUBLE_EQ(perf[0].metrics.latencyMs, 100.0);
    });
}

TEST_F(EmergenceManagerTest, ExportAndImportVariants) {
    // Create test variants
    ProtocolVariant v1("v1", "test variant 1", nlohmann::json::object(), {{"timestamp", 100}});
    ProtocolVariant v2("v2", "test variant 2", nlohmann::json::object(), {{"timestamp", 200}});
    manager->proposeVariant("v1", v1, "test variant 1", {{"timestamp", 100}});
    manager->proposeVariant("v2", v2, "test variant 2", {{"timestamp", 200}});
    
    // Add performance data
    PerformanceMetrics metrics1;
    metrics1.successRate = 0.95;
    PerformanceRecord record1;
    record1.metrics = metrics1;
    record1.timestamp = std::chrono::system_clock::now();
    manager->logPerformance("v1", record1);
    
    // Export variants
    std::string exportPath = tempPath + "/export_test.json";
    manager->exportVariants(exportPath, {"v1", "v2"});
    
    // Create new manager and import
    auto importManager = std::make_unique<EmergenceManager>(tempPath, nlohmann::json::object());
    EXPECT_NO_THROW(importManager->importVariants(exportPath));
    
    // Verify imported data
    EXPECT_NO_THROW({
        auto v1Imported = importManager->getVariant("v1");
        EXPECT_EQ(v1Imported.id, "v1");
        EXPECT_EQ(v1Imported.description, "test variant 1");
        
        auto v2Imported = importManager->getVariant("v2");
        EXPECT_EQ(v2Imported.id, "v2");
        EXPECT_EQ(v2Imported.description, "test variant 2");
        
        auto perf = importManager->getVariantPerformance("v1");
        EXPECT_EQ(perf.size(), 1);
        EXPECT_DOUBLE_EQ(perf[0].metrics.successRate, 0.95);
    });
}

TEST_F(EmergenceManagerTest, AutosaveFunctionality) {
    // Enable autosave with a short interval
    manager->enableAutosave(std::chrono::seconds(1));
    
    // Create test variant
    ProtocolVariant v1("v1", "test variant", nlohmann::json::object(), nlohmann::json::object());
    manager->proposeVariant("v1", v1, "test variant", nlohmann::json::object());
    
    // Wait for autosave
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Create new manager to verify autosaved state
    auto newManager = std::make_unique<EmergenceManager>(tempPath, nlohmann::json::object());
    EXPECT_NO_THROW({
        auto variant = newManager->getVariant("v1");
        EXPECT_EQ(variant.id, "v1");
        EXPECT_EQ(variant.description, "test variant");
    });
    
    // Disable autosave
    manager->disableAutosave();
}

TEST_F(EmergenceManagerTest, ConflictResolution) {
    // Create original variant
    ProtocolVariant v1("v1", "original", nlohmann::json::object(), {{"timestamp", 100}});
    manager->proposeVariant("v1", v1, "original", {{"timestamp", 100}});
    
    // Create newer variant for import
    ProtocolVariant v1New("v1", "updated", nlohmann::json::object(), {{"timestamp", 200}});
    
    // Export newer variant
    std::string exportPath = tempPath + "/conflict_test.json";
    nlohmann::json exportData;
    exportData["variants"]["v1"] = v1New.to_json();
    std::ofstream file(exportPath);
    file << exportData.dump();
    file.close();
    
    // Import and verify conflict resolution
    manager->importVariants(exportPath);
    auto importedVariant = manager->getVariant("v1");
    EXPECT_EQ(importedVariant.description, "updated");
}

TEST_F(EmergenceManagerTest, ValidateVariant) {
    // Create valid variant
    ProtocolVariant valid("v1", "valid", nlohmann::json::object(), {{"metric1", 1.0}});
    nlohmann::json evalMetrics = {{"metric1", "description"}};
    auto validationManager = std::make_unique<EmergenceManager>(tempPath, evalMetrics);
    
    // Create invalid variant (missing required metric)
    ProtocolVariant invalid("v2", "invalid", nlohmann::json::object(), nlohmann::json::object());
    
    // Test validation
    EXPECT_TRUE(validationManager->validateVariant(valid));
    EXPECT_FALSE(validationManager->validateVariant(invalid));
}

// Helper functions for tests
ProtocolVariant createTestVariant(const std::string& id = "test_variant") {
    nlohmann::json changes = {
        {"parameter", "value"},
        {"setting", 42}
    };
    nlohmann::json metadata = {
        {"requiredCapabilities", {
            {"feature1", "required"},
            {"feature2", "optional"}
        }},
        {"characteristics", {
            {"latency", 0.8},
            {"throughput", 0.9},
            {"reliability", 0.95}
        }}
    };
    return ProtocolVariant(id, "Test variant description", changes, metadata);
}

AgentContext createTestAgentContext(const std::string& agentId) {
    AgentContext context;
    context.agentId = agentId;
    context.capabilities = {
        {"feature1", "enabled"},
        {"feature2", "enabled"}
    };
    context.preferences = {
        {"latency", 0.7},
        {"throughput", 0.3}
    };
    return context;
}

TEST_CASE("Agent registration and context management", "[emergence][agents]") {
    EmergenceManager manager("test_persistence", nlohmann::json());
    
    SECTION("Register new agent") {
        auto context = createTestAgentContext("agent1");
        REQUIRE_NOTHROW(manager.registerAgent("agent1", context));
        
        auto stored = manager.getAgentContext("agent1");
        REQUIRE(stored.agentId == "agent1");
        REQUIRE(stored.capabilities == context.capabilities);
        REQUIRE(stored.preferences == context.preferences);
    }
    
    SECTION("Update existing agent") {
        auto context = createTestAgentContext("agent1");
        manager.registerAgent("agent1", context);
        
        context.preferences["latency"] = 0.9;
        REQUIRE_NOTHROW(manager.updateAgentContext("agent1", context));
        
        auto stored = manager.getAgentContext("agent1");
        REQUIRE(stored.preferences["latency"] == 0.9);
    }
    
    SECTION("Get non-existent agent") {
        REQUIRE_THROWS_AS(manager.getAgentContext("nonexistent"), std::runtime_error);
    }
    
    SECTION("Register duplicate agent") {
        auto context = createTestAgentContext("agent1");
        manager.registerAgent("agent1", context);
        REQUIRE_THROWS_AS(manager.registerAgent("agent1", context), std::runtime_error);
    }
}

TEST_CASE("Variant proposal and voting", "[emergence][agents]") {
    EmergenceManager manager("test_persistence", nlohmann::json());
    auto agent1Context = createTestAgentContext("agent1");
    auto agent2Context = createTestAgentContext("agent2");
    manager.registerAgent("agent1", agent1Context);
    manager.registerAgent("agent2", agent2Context);
    
    SECTION("Agent proposes variant") {
        auto variant = createTestVariant();
        auto variantId = manager.proposeVariantAsAgent(
            "agent1",
            variant,
            "Improved latency characteristics"
        );
        
        auto storedVariant = manager.getVariant(variantId);
        REQUIRE(storedVariant.metadata["proposingAgent"] == "agent1");
        REQUIRE(storedVariant.metadata.contains("proposalTimestamp"));
    }
    
    SECTION("Voting process") {
        auto variant = createTestVariant();
        auto variantId = manager.proposeVariantAsAgent(
            "agent1",
            variant,
            "Test proposal"
        );
        
        // Configure consensus for testing
        ConsensusConfig config;
        config.requiredMajority = 0.75;
        config.minimumVotes = 2;
        config.votingPeriod = std::chrono::seconds(1);
        config.requirePerformanceEvidence = false;
        manager.setConsensusConfig(config);
        
        // Second agent votes in favor
        REQUIRE_NOTHROW(manager.voteOnVariant("agent2", variantId, true, "Looks good"));
        
        // Wait for voting period
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Check if variant was adopted
        auto status = manager.getVariant(variantId);
        REQUIRE(status.metadata["proposingAgent"] == "agent1");
    }
    
    SECTION("Invalid voting attempts") {
        auto variant = createTestVariant();
        auto variantId = manager.proposeVariantAsAgent(
            "agent1",
            variant,
            "Test proposal"
        );
        
        // Try to vote with non-existent agent
        REQUIRE_THROWS_AS(
            manager.voteOnVariant("nonexistent", variantId, true, "Invalid"),
            std::runtime_error
        );
        
        // Try to vote on non-existent variant
        REQUIRE_THROWS_AS(
            manager.voteOnVariant("agent1", "nonexistent", true, "Invalid"),
            std::runtime_error
        );
    }
}

TEST_CASE("Variant recommendations", "[emergence][agents]") {
    EmergenceManager manager("test_persistence", nlohmann::json());
    auto agent1Context = createTestAgentContext("agent1");
    manager.registerAgent("agent1", agent1Context);
    
    // Create and adopt some variants
    auto variant1 = createTestVariant("variant1");
    auto variant2 = createTestVariant("variant2");
    
    manager.proposeVariant("variant1", variant1, "Test variant 1", variant1.metadata);
    manager.proposeVariant("variant2", variant2, "Test variant 2", variant2.metadata);
    
    manager.setVariantStatus("variant1", VariantStatus::Adopted);
    manager.setVariantStatus("variant2", VariantStatus::Adopted);
    
    SECTION("Get recommendations") {
        auto recommendations = manager.getRecommendedVariants("agent1", 5);
        REQUIRE(recommendations.size() > 0);
    }
    
    SECTION("Recommendations affected by experience") {
        // Report successful experience with variant1
        manager.reportVariantExperience("agent1", "variant1", true, "Worked well");
        
        auto recommendations = manager.getRecommendedVariants("agent1", 5);
        REQUIRE(recommendations.size() > 0);
        REQUIRE(recommendations[0] == "variant1"); // Should be first due to successful experience
    }
}

TEST_CASE("Variant adoption notifications", "[emergence][agents]") {
    EmergenceManager manager("test_persistence", nlohmann::json());
    auto agent1Context = createTestAgentContext("agent1");
    manager.registerAgent("agent1", agent1Context);
    
    auto startTime = std::chrono::system_clock::now();
    
    // Create and adopt a variant
    auto variant = createTestVariant();
    manager.proposeVariant("test_variant", variant, "Test variant", variant.metadata);
    manager.setVariantStatus("test_variant", VariantStatus::Adopted);
    
    SECTION("Get newly adopted variants") {
        auto newVariants = manager.getNewlyAdoptedVariants("agent1", startTime);
        REQUIRE(newVariants.size() == 1);
        REQUIRE(newVariants[0] == "test_variant");
    }
    
    SECTION("No new variants before timestamp") {
        auto futureTime = std::chrono::system_clock::now() + std::chrono::hours(1);
        auto newVariants = manager.getNewlyAdoptedVariants("agent1", futureTime);
        REQUIRE(newVariants.empty());
    }
}

TEST_CASE("Consensus configuration", "[emergence][agents]") {
    EmergenceManager manager("test_persistence", nlohmann::json());
    
    SECTION("Set and get consensus config") {
        ConsensusConfig config;
        config.requiredMajority = 0.8;
        config.minimumVotes = 5;
        config.votingPeriod = std::chrono::seconds(3600);
        config.requirePerformanceEvidence = true;
        
        REQUIRE_NOTHROW(manager.setConsensusConfig(config));
        
        auto stored = manager.getConsensusConfig();
        REQUIRE(stored.requiredMajority == config.requiredMajority);
        REQUIRE(stored.minimumVotes == config.minimumVotes);
        REQUIRE(stored.votingPeriod == config.votingPeriod);
        REQUIRE(stored.requirePerformanceEvidence == config.requirePerformanceEvidence);
    }
    
    SECTION("Invalid consensus config") {
        ConsensusConfig config;
        
        config.requiredMajority = 1.5; // Invalid: > 1.0
        REQUIRE_THROWS_AS(manager.setConsensusConfig(config), std::invalid_argument);
        
        config.requiredMajority = 0.75;
        config.minimumVotes = 0; // Invalid: must be > 0
        REQUIRE_THROWS_AS(manager.setConsensusConfig(config), std::invalid_argument);
    }
}

TEST_CASE("Persistence of agent-related data", "[emergence][agents]") {
    const std::string testPath = "test_persistence";
    
    // Create and populate a manager
    {
        EmergenceManager manager(testPath, nlohmann::json());
        
        // Add an agent
        auto context = createTestAgentContext("agent1");
        manager.registerAgent("agent1", context);
        
        // Add a variant and some votes
        auto variant = createTestVariant();
        auto variantId = manager.proposeVariantAsAgent(
            "agent1",
            variant,
            "Test persistence"
        );
        
        // Configure consensus
        ConsensusConfig config;
        config.requiredMajority = 0.75;
        config.minimumVotes = 2;
        manager.setConsensusConfig(config);
        
        // Save state
        manager.saveState();
    }
    
    // Create new manager and load state
    {
        EmergenceManager manager(testPath, nlohmann::json());
        manager.loadState();
        
        // Verify agent context was restored
        auto context = manager.getAgentContext("agent1");
        REQUIRE(context.agentId == "agent1");
        REQUIRE(context.capabilities.size() > 0);
        
        // Verify consensus config was restored
        auto config = manager.getConsensusConfig();
        REQUIRE(config.requiredMajority == 0.75);
        REQUIRE(config.minimumVotes == 2);
    }
} 
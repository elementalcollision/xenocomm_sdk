#include <gtest/gtest.h>
#include "xenocomm/extensions/emergence_manager.hpp"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <thread>
#include <fstream>
#include <chrono>

// Add namespace usage for xenocomm::extensions
using namespace xenocomm::extensions;

class EmergenceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string testPath = "test_persistence";
        manager = std::make_unique<EmergenceManager>(testPath, nlohmann::json::object());
    }

    void TearDown() override {
        // Optionally remove log file
        std::remove("test_persistence/emergence_manager.log");
    }

    std::unique_ptr<EmergenceManager> manager;
};

TEST_F(EmergenceManagerTest, Construction) {
    // Successfully constructed in SetUp
    EXPECT_TRUE(manager != nullptr);
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
    record.context = "Test performance record";
    record.sampleSize = 100;

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
    EXPECT_EQ(history[0].context, "Test performance record");
    EXPECT_EQ(history[0].sampleSize, 100);
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
    criteria.metricWeights = {
        {"successRate", 0.4},
        {"latencyMs", 0.3},
        {"resourceUsage", 0.2},
        {"throughput", 0.1}
    };
    criteria.improvementThreshold = 0.05;
    criteria.minSampleSize = 50;
    criteria.requireStatisticalSignificance = true;

    EXPECT_NO_THROW(manager->setEvaluationCriteria(criteria));
    
    auto retrieved = manager->getEvaluationCriteria();
    EXPECT_EQ(retrieved.metricWeights["successRate"], 0.4);
    EXPECT_EQ(retrieved.metricWeights["latencyMs"], 0.3);
    EXPECT_EQ(retrieved.metricWeights["resourceUsage"], 0.2);
    EXPECT_EQ(retrieved.metricWeights["throughput"], 0.1);
    EXPECT_EQ(retrieved.improvementThreshold, 0.05);
    EXPECT_EQ(retrieved.minSampleSize, 50);
    EXPECT_EQ(retrieved.requireStatisticalSignificance, true);
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
    criteria.metricWeights = {
        {"successRate", 1.0}  // Only consider success rate for simplicity
    };
    criteria.improvementThreshold = 0.0;
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
    criteria.metricWeights = {
        {"successRate", 1.0}
    };
    criteria.improvementThreshold = 0.1;
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
    record1.context = "Good performance";
    record1.sampleSize = 200;
    manager->logPerformance("v1", record1);

    PerformanceRecord record2;
    record2.metrics.successRate = 0.85;
    record2.metrics.latencyMs = 150.0;
    record2.context = "Average performance";
    record2.sampleSize = 200;
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
    record.context = "Good performance";
    record.sampleSize = 200;
    manager->logPerformance("v1", record);
    
    // Save state
    manager->saveState();
    
    // Create new manager instance
    auto newManager = std::make_unique<EmergenceManager>(std::string("test_persistence"), nlohmann::json::object());
    
    // Verify loaded state
    EXPECT_NO_THROW({
        auto variant = newManager->getVariant("v1");
        EXPECT_EQ(variant.id, "v1");
        EXPECT_EQ(variant.description, "test variant 1");
        
        auto perf = newManager->getVariantPerformance("v1");
        EXPECT_EQ(perf.size(), 1);
        EXPECT_DOUBLE_EQ(perf[0].metrics.successRate, 0.95);
        EXPECT_DOUBLE_EQ(perf[0].metrics.latencyMs, 100.0);
        EXPECT_EQ(perf[0].context, "Good performance");
        EXPECT_EQ(perf[0].sampleSize, 200);
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
    std::string exportPath = "test_persistence/export_test.json";
    manager->exportVariants(exportPath, {"v1", "v2"});
    
    // Create new manager and import
    auto importManager = std::make_unique<EmergenceManager>(std::string("test_persistence"), nlohmann::json::object());
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
    auto newManager = std::make_unique<EmergenceManager>(std::string("test_persistence"), nlohmann::json::object());
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
    std::string exportPath = "test_persistence/conflict_test.json";
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
    auto validationManager = std::make_unique<EmergenceManager>(std::string("test_persistence"), evalMetrics);
    
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

TEST_F(EmergenceManagerTest, AgentRegistrationAndManagement) {
    // Create a temporary file for persistence
    std::string tempPath = "test_agent_management.json";
    
    // Initialize manager
    EmergenceManager manager(tempPath, nlohmann::json::object());
    
    // Create agent context
    AgentContext context;
    context.agentId = "agent1";
    context.capabilities = {{"protocol_v1", "supported"}, {"protocol_v2", "unsupported"}};
    context.preferences = {{"latency", 0.8}, {"throughput", 0.2}};
    
    // Register agent
    EXPECT_NO_THROW(manager.registerAgent("agent1", context));
    
    // Retrieve and verify
    auto retrieved = manager.getAgentContext("agent1");
    EXPECT_EQ(retrieved.agentId, "agent1");
    EXPECT_EQ(retrieved.capabilities.size(), 2);
    EXPECT_EQ(retrieved.capabilities["protocol_v1"], "supported");
    EXPECT_EQ(retrieved.preferences["latency"], 0.8);
    
    // Update context
    context.capabilities["protocol_v2"] = "supported";
    EXPECT_NO_THROW(manager.updateAgentContext("agent1", context));
    
    // Verify update
    retrieved = manager.getAgentContext("agent1");
    EXPECT_EQ(retrieved.capabilities["protocol_v2"], "supported");
    
    // Clean up
    std::remove(tempPath.c_str());
}

TEST_F(EmergenceManagerTest, VariantProposalAndVoting) {
    // Create a temporary file for persistence
    std::string tempPath = "test_voting.json";
    
    // Initialize manager with consensus config that requires just 2 votes
    EmergenceManager manager(tempPath, nlohmann::json::object());
    ConsensusConfig config;
    config.requiredMajority = 0.5;
    config.minimumVotes = 2;
    config.requirePerformanceEvidence = false;
    manager.setConsensusConfig(config);
    
    // Register two agents
    AgentContext agentContext1, agentContext2;
    agentContext1.agentId = "agent1";
    agentContext2.agentId = "agent2";
    manager.registerAgent("agent1", agentContext1);
    manager.registerAgent("agent2", agentContext2);
    
    // Create a protocol variant
    ProtocolVariant variant;
    variant.id = "test-variant";
    variant.description = "Test variant for voting";
    
    // Agent1 proposes the variant
    std::string variantId = manager.proposeVariantAsAgent("agent1", variant, "Initial proposal");
    EXPECT_FALSE(variantId.empty());
    
    // Verify status is Proposed
    ProtocolVariant retrieved = manager.getVariant(variantId);
    EXPECT_EQ(retrieved.description, "Test variant for voting");
    
    // Both agents vote in favor
    manager.voteOnVariant("agent1", variantId, true, "Support own proposal");
    manager.voteOnVariant("agent2", variantId, true, "Good idea");
    
    // Verify the variant is now adopted
    auto adoptedVariants = manager.listVariants(VariantStatus::Adopted);
    EXPECT_TRUE(adoptedVariants.find(variantId) != adoptedVariants.end());
    
    // Clean up
    std::remove(tempPath.c_str());
}

TEST_F(EmergenceManagerTest, VariantRecommendations) {
    // Create a temporary file for persistence
    std::string tempPath = "test_recommendations.json";
    
    // Initialize manager
    EmergenceManager manager(tempPath, nlohmann::json::object());
    
    // Register agent
    AgentContext context;
    context.agentId = "agent1";
    context.preferences = {{"latency", 0.9}, {"throughput", 0.1}};
    manager.registerAgent("agent1", context);
    
    // Create and register variants with different statuses
    ProtocolVariant variant1, variant2, variant3;
    variant1.id = "variant1";
    variant1.description = "Adopted variant";
    variant2.id = "variant2";
    variant2.description = "Testing variant";
    variant3.id = "variant3";
    variant3.description = "Proposed variant";
    
    manager.proposeVariant("variant1", variant1, "Variant 1", {});
    manager.proposeVariant("variant2", variant2, "Variant 2", {});
    manager.proposeVariant("variant3", variant3, "Variant 3", {});
    
    manager.setVariantStatus("variant1", VariantStatus::Adopted);
    manager.setVariantStatus("variant2", VariantStatus::InTesting);
    manager.setVariantStatus("variant3", VariantStatus::Proposed);
    
    // Get recommendations
    auto recommendations = manager.getRecommendedVariants("agent1");
    EXPECT_FALSE(recommendations.empty());
    
    // Clean up
    std::remove(tempPath.c_str());
}

TEST_F(EmergenceManagerTest, VariantAdoptionNotifications) {
    // Create a temporary file for persistence
    std::string tempPath = "test_notifications.json";
    
    // Initialize manager
    EmergenceManager manager(tempPath, nlohmann::json::object());
    
    // Register agent
    AgentContext context;
    context.agentId = "agent1";
    manager.registerAgent("agent1", context);
    
    // Create a variant and mark as adopted
    ProtocolVariant variant;
    variant.id = "adopted-variant";
    variant.description = "Newly adopted variant";
    
    manager.proposeVariant("adopted-variant", variant, "New variant", {});
    manager.setVariantStatus("adopted-variant", VariantStatus::Adopted);
    
    // Check for notifications
    auto since = std::chrono::system_clock::now() - std::chrono::hours(24); // Last 24 hours
    auto adoptedVariants = manager.getNewlyAdoptedVariants("agent1", since);
    
    EXPECT_FALSE(adoptedVariants.empty());
    EXPECT_TRUE(std::find(adoptedVariants.begin(), adoptedVariants.end(), "adopted-variant") != adoptedVariants.end());
    
    // Clean up
    std::remove(tempPath.c_str());
}

TEST_F(EmergenceManagerTest, ConsensusConfiguration) {
    // Create a temporary file for persistence
    std::string tempPath = "test_consensus_config.json";
    
    // Initialize manager
    EmergenceManager manager(tempPath, nlohmann::json::object());
    
    // Set custom consensus config
    ConsensusConfig config;
    config.requiredMajority = 0.6;          // 60% majority
    config.minimumVotes = 5;                // At least 5 votes
    config.votingPeriod = std::chrono::hours(48); // 48 hour voting window
    config.requirePerformanceEvidence = true;
    
    manager.setConsensusConfig(config);
    
    // Retrieve and verify
    auto retrieved = manager.getConsensusConfig();
    EXPECT_DOUBLE_EQ(retrieved.requiredMajority, 0.6);
    EXPECT_EQ(retrieved.minimumVotes, 5);
    EXPECT_EQ(retrieved.votingPeriod, std::chrono::hours(48));
    EXPECT_TRUE(retrieved.requirePerformanceEvidence);
    
    // Clean up
    std::remove(tempPath.c_str());
}

TEST_F(EmergenceManagerTest, PersistenceOfAgentRelatedData) {
    // Create a temporary file for persistence
    std::string tempPath = "test_agent_persistence.json";
    
    // Part 1: Create and save state
    {
        EmergenceManager manager(tempPath, nlohmann::json::object());
        
        // Set consensus config
        ConsensusConfig config;
        config.requiredMajority = 0.7;
        manager.setConsensusConfig(config);
        
        // Register agent
        AgentContext context;
        context.agentId = "persistent-agent";
        context.capabilities = {{"feature1", "yes"}};
        manager.registerAgent("persistent-agent", context);
        
        // Save state
        manager.saveState();
    }
    
    // Part 2: Load and verify state
    {
        EmergenceManager manager(tempPath, nlohmann::json::object());
        
        // Load state
        manager.loadState();
        
        // Verify consensus config
        auto config = manager.getConsensusConfig();
        EXPECT_DOUBLE_EQ(config.requiredMajority, 0.7);
        
        // Verify agent data
        auto context = manager.getAgentContext("persistent-agent");
        EXPECT_EQ(context.agentId, "persistent-agent");
        EXPECT_EQ(context.capabilities["feature1"], "yes");
    }
    
    // Clean up
    std::remove(tempPath.c_str());
} 
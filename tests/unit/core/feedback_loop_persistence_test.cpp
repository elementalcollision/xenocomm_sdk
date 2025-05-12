#include <gtest/gtest.h>
#include "xenocomm/core/feedback_loop.h"
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace xenocomm {
namespace core {
namespace {

// Helper to remove directory and its contents
void remove_directory_recursive(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    // Ignore error if path doesn't exist or cannot be removed, test will fail later if needed
}

class FeedbackLoopPersistenceTest : public ::testing::Test {
protected:
    std::string testDir = "./test_feedback_data";

    void SetUp() override {
        remove_directory_recursive(testDir);
        std::filesystem::create_directories(testDir);
        feedbackLoop_ = std::make_unique<FeedbackLoop>(createConfig(testDir));
    }

    void TearDown() override {
        feedbackLoop_.reset();
        remove_directory_recursive(testDir);
    }

    FeedbackLoopConfig createConfig(const std::string& dataDir) {
        FeedbackLoopConfig config;
        config.enablePersistence = true;
        config.persistence.dataDirectory = dataDir;
        config.persistence.retentionPeriod = std::chrono::hours(1);
        config.persistence.enableBackup = false;
        return config;
    }

    std::unique_ptr<FeedbackLoop> feedbackLoop_;
};

TEST_F(FeedbackLoopPersistenceTest, SaveAndLoadData) {
    CommunicationOutcome outcome = {
        true, std::chrono::milliseconds(100), 1024, 0, 0, "", std::chrono::system_clock::now()
    };
    feedbackLoop_->reportOutcome(outcome);
    feedbackLoop_->recordMetric("rtt_ms", 100.0);

    ASSERT_TRUE(feedbackLoop_->saveData().has_value());

    FeedbackLoopConfig new_loop_config = createConfig(testDir);
    FeedbackLoop newLoop(new_loop_config);
    
    ASSERT_TRUE(newLoop.loadData().has_value());

    auto stats_result = newLoop.getCurrentMetrics();
    ASSERT_TRUE(stats_result.has_value());
    auto stats = stats_result.value();
    EXPECT_EQ(stats.totalTransactions, 1);

    auto rtt_result = newLoop.getMetricValue("rtt_ms");
    ASSERT_TRUE(rtt_result.has_value());
    EXPECT_DOUBLE_EQ(rtt_result.value(), 100.0);
}

TEST_F(FeedbackLoopPersistenceTest, DataRetention) {
    FeedbackLoopConfig config = createConfig(testDir);
    config.persistence.retentionPeriod = std::chrono::duration_cast<std::chrono::hours>(std::chrono::seconds(1));
    FeedbackLoop loop(config);

    CommunicationOutcome outcome1 = {
        true, std::chrono::milliseconds(100), 1024, 0, 0, "", 
        std::chrono::system_clock::now() - std::chrono::seconds(2)
    };
    loop.reportOutcome(outcome1);

    CommunicationOutcome outcome2 = {
        true, std::chrono::milliseconds(150), 2048, 1, 0, "", 
        std::chrono::system_clock::now()
    };
    loop.reportOutcome(outcome2);

    ASSERT_TRUE(loop.saveData().has_value());
    ASSERT_TRUE(loop.loadData().has_value());

    auto current_metrics_result = loop.getCurrentMetrics();
    ASSERT_TRUE(current_metrics_result.has_value());
}

TEST_F(FeedbackLoopPersistenceTest, CorruptedDataHandling) {
    CommunicationOutcome outcome = {
        true, std::chrono::milliseconds(50), 512, 0, 0, "", std::chrono::system_clock::now()
    };
    feedbackLoop_->reportOutcome(outcome);
    ASSERT_TRUE(feedbackLoop_->saveData().has_value());

    std::string mainFile = testDir + "/feedback_main.dat";
    std::ofstream ofs(mainFile, std::ios::trunc | std::ios::binary);
    if (ofs.is_open()) {
        ofs << "corrupted_data_far_beyond_repair";
        ofs.close();
    }

    FeedbackLoopConfig new_config = createConfig(testDir);
    FeedbackLoop corruptedLoop(new_config);
    auto load_result = corruptedLoop.loadData();
    EXPECT_FALSE(load_result.has_value());

    auto metrics_after_corrupt_load = corruptedLoop.getCurrentMetrics();
    ASSERT_TRUE(metrics_after_corrupt_load.has_value());
    EXPECT_EQ(metrics_after_corrupt_load.value().totalTransactions, 0);
}

} // namespace
} // namespace core
} // namespace xenocomm 
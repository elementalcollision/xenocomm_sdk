#include "xenocomm/core/feedback_loop.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <thread>
#include <chrono>

namespace xenocomm {
namespace {

class FeedbackLoopPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary test directory
        testDir_ = std::filesystem::temp_directory_path() / "feedback_test";
        std::filesystem::create_directories(testDir_);
        
        // Configure persistence
        PersistenceConfig config;
        config.dataDirectory = testDir_.string();
        config.retentionPeriod = std::chrono::hours(1);
        config.maxStorageSizeBytes = 1024 * 1024;  // 1MB for testing
        config.enableCompression = true;
        
        feedbackLoop_ = std::make_unique<FeedbackLoop>(config);
    }
    
    void TearDown() override {
        feedbackLoop_.reset();
        std::filesystem::remove_all(testDir_);
    }
    
    std::filesystem::path testDir_;
    std::unique_ptr<FeedbackLoop> feedbackLoop_;
};

TEST_F(FeedbackLoopPersistenceTest, SaveAndLoadBasicMetrics) {
    // Add some test data
    feedbackLoop_->recordCommunicationOutcome({
        .success = true,
        .latencyMicros = 100,
        .bytesTransferred = 1024,
        .retryCount = 0,
        .errorCount = 0
    });
    
    feedbackLoop_->recordMetric("throughput", 1024.0);
    feedbackLoop_->recordMetric("error_rate", 0.01);
    
    // Wait briefly to ensure metrics are processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Save the data
    ASSERT_TRUE(feedbackLoop_->saveData().isOk());
    
    // Create a new FeedbackLoop instance and load the data
    PersistenceConfig config;
    config.dataDirectory = testDir_.string();
    FeedbackLoop newLoop(config);
    
    ASSERT_TRUE(newLoop.loadData().isOk());
    
    // Verify the loaded data
    auto stats = newLoop.getStatistics();
    EXPECT_EQ(stats.totalCommunications, 1);
    EXPECT_EQ(stats.successfulCommunications, 1);
    EXPECT_NEAR(stats.averageLatencyMicros, 100.0, 0.1);
    
    auto metrics = newLoop.getMetrics();
    EXPECT_TRUE(metrics.find("throughput") != metrics.end());
    EXPECT_TRUE(metrics.find("error_rate") != metrics.end());
    EXPECT_NEAR(metrics["throughput"].back(), 1024.0, 0.1);
    EXPECT_NEAR(metrics["error_rate"].back(), 0.01, 0.001);
}

TEST_F(FeedbackLoopPersistenceTest, DataRetention) {
    // Configure with short retention period
    PersistenceConfig config;
    config.dataDirectory = testDir_.string();
    config.retentionPeriod = std::chrono::seconds(1);
    FeedbackLoop loop(config);
    
    // Add old data
    loop.recordMetric("test", 1.0);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Add new data
    loop.recordMetric("test", 2.0);
    
    // Save and reload
    ASSERT_TRUE(loop.saveData().isOk());
    ASSERT_TRUE(loop.loadData().isOk());
    
    // Verify only recent data is retained
    auto metrics = loop.getMetrics();
    ASSERT_TRUE(metrics.find("test") != metrics.end());
    EXPECT_EQ(metrics["test"].size(), 1);
    EXPECT_NEAR(metrics["test"].back(), 2.0, 0.1);
}

TEST_F(FeedbackLoopPersistenceTest, StorageLimitEnforcement) {
    // Configure with small storage limit
    PersistenceConfig config;
    config.dataDirectory = testDir_.string();
    config.maxStorageSizeBytes = 1024;  // 1KB limit
    FeedbackLoop loop(config);
    
    // Add lots of data
    for (int i = 0; i < 1000; i++) {
        loop.recordCommunicationOutcome({
            .success = true,
            .latencyMicros = i,
            .bytesTransferred = 1024,
            .retryCount = 0,
            .errorCount = 0
        });
    }
    
    // Save data
    ASSERT_TRUE(loop.saveData().isOk());
    
    // Verify storage limit is respected
    std::uintmax_t totalSize = 0;
    for (const auto& entry : std::filesystem::directory_iterator(testDir_)) {
        totalSize += std::filesystem::file_size(entry.path());
    }
    EXPECT_LE(totalSize, config.maxStorageSizeBytes);
}

TEST_F(FeedbackLoopPersistenceTest, BackupAndRecovery) {
    // Configure with backups enabled
    PersistenceConfig config;
    config.dataDirectory = testDir_.string();
    config.enableBackup = true;
    config.backupIntervalHours = 0;  // Force immediate backup
    FeedbackLoop loop(config);
    
    // Add some data
    loop.recordMetric("test", 1.0);
    ASSERT_TRUE(loop.saveData().isOk());
    
    // Verify backup was created
    bool foundBackup = false;
    for (const auto& entry : std::filesystem::directory_iterator(testDir_)) {
        if (entry.path().string().find("backup") != std::string::npos) {
            foundBackup = true;
            break;
        }
    }
    EXPECT_TRUE(foundBackup);
    
    // Corrupt the main data file
    auto mainFile = std::filesystem::directory_iterator(testDir_)->path();
    std::ofstream(mainFile, std::ios::trunc) << "corrupted";
    
    // Load should succeed by recovering from backup
    ASSERT_TRUE(loop.loadData().isOk());
    
    auto metrics = loop.getMetrics();
    ASSERT_TRUE(metrics.find("test") != metrics.end());
    EXPECT_NEAR(metrics["test"].back(), 1.0, 0.1);
}

} // namespace
} // namespace xenocomm 
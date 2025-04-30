#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include "xenocomm/extensions/rollback_manager.hpp"
#include "xenocomm/extensions/compatibility_checker.hpp"

using namespace xenocomm::extensions;
using json = nlohmann::json;

// Helper function to compute SHA-256 hash for verification
std::string computeSHA256(const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.c_str(), data.length());
    EVP_DigestFinal_ex(ctx, hash, &hashLen);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for(unsigned int i = 0; i < hashLen; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

class RollbackManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for rollback points
        testDir_ = std::filesystem::temp_directory_path() / "rollback_test";
        std::filesystem::create_directories(testDir_);
        
        // Configure the rollback manager
        config_.storagePath = (testDir_ / "rollbacks/").string();
        config_.maxRollbackPoints = 5;
        config_.retentionPeriod = std::chrono::hours(1);
        config_.enableIncrementalSnapshots = true;
        config_.maxSnapshotSizeBytes = 1024 * 1024; // 1MB
        
        // Create compatibility checker
        compatibilityChecker_ = std::make_shared<CompatibilityChecker>();
        
        // Create rollback manager
        manager_ = std::make_unique<RollbackManager>(config_, compatibilityChecker_);
    }

    void TearDown() override {
        // Clean up test directory
        std::filesystem::remove_all(testDir_);
    }

    // Helper to create a test state
    json createTestState(int version, const std::string& data) {
        return {
            {"version", version},
            {"data", data},
            {"timestamp", std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())}
        };
    }

    std::filesystem::path testDir_;
    RollbackConfig config_;
    std::shared_ptr<CompatibilityChecker> compatibilityChecker_;
    std::unique_ptr<RollbackManager> manager_;
};

TEST_F(RollbackManagerTest, CreateAndRetrieveRollbackPoint) {
    // Create a test state
    auto state = createTestState(1, "test data");
    std::map<std::string, std::string> metadata{{"test", "value"}};

    // Create rollback point
    auto id = manager_->createRollbackPoint("test_variant", state, metadata);
    ASSERT_FALSE(id.empty());

    // Retrieve the rollback point
    auto point = manager_->getRollbackPoint(id);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->variantId, "test_variant");
    EXPECT_EQ(point->state, state);
    EXPECT_EQ(point->metadata, metadata);
}

TEST_F(RollbackManagerTest, ListRollbackPoints) {
    // Create multiple rollback points
    auto state1 = createTestState(1, "data1");
    auto state2 = createTestState(2, "data2");
    auto state3 = createTestState(3, "data3");

    auto id1 = manager_->createRollbackPoint("variant1", state1);
    auto id2 = manager_->createRollbackPoint("variant2", state2);
    auto id3 = manager_->createRollbackPoint("variant1", state3);

    // List all points
    auto allPoints = manager_->listRollbackPoints();
    EXPECT_EQ(allPoints.size(), 3);

    // List points for variant1
    auto variant1Points = manager_->listRollbackPoints("variant1");
    EXPECT_EQ(variant1Points.size(), 2);

    // Verify points are sorted by timestamp (newest first)
    EXPECT_GT(variant1Points[0].timestamp, variant1Points[1].timestamp);
}

TEST_F(RollbackManagerTest, VerifyRollbackPointIntegrity) {
    // Create a rollback point
    auto state = createTestState(1, "test data");
    auto id = manager_->createRollbackPoint("test_variant", state);

    // Verify integrity
    EXPECT_TRUE(manager_->verifyRollbackPoint(id));

    // Corrupt the file
    std::ofstream file(config_.storagePath + id + ".json", std::ios::app);
    file << "corrupted";
    file.close();

    // Verify should now fail
    EXPECT_FALSE(manager_->verifyRollbackPoint(id));
}

TEST_F(RollbackManagerTest, IncrementalSnapshots) {
    // Create initial state
    json state1 = {
        {"key1", "value1"},
        {"key2", "value2"}
    };
    auto id1 = manager_->createRollbackPoint("test_variant", state1);

    // Create modified state
    json state2 = {
        {"key1", "value1"},      // unchanged
        {"key2", "new_value2"},  // modified
        {"key3", "value3"}       // added
    };
    auto id2 = manager_->createRollbackPoint("test_variant", state2);

    // Get the second rollback point
    auto point2 = manager_->getRollbackPoint(id2);
    ASSERT_TRUE(point2.has_value());

    // The stored state should be an incremental snapshot
    EXPECT_TRUE(point2->state.contains("key2"));  // modified key
    EXPECT_TRUE(point2->state.contains("key3"));  // new key
    EXPECT_FALSE(point2->state.contains("key1")); // unchanged key
}

TEST_F(RollbackManagerTest, RestoreToPoint) {
    // Create a sequence of states
    json state1 = {{"data", "initial"}};
    json state2 = {{"data", "modified"}};
    json state3 = {{"data", "final"}};

    auto id1 = manager_->createRollbackPoint("test_variant", state1);
    auto id2 = manager_->createRollbackPoint("test_variant", state2);
    auto id3 = manager_->createRollbackPoint("test_variant", state3);

    // Restore to middle state
    EXPECT_TRUE(manager_->restoreToPoint(id2));

    // Verify the restored state
    auto point = manager_->getRollbackPoint(id2);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->state["data"], "modified");
}

TEST_F(RollbackManagerTest, CleanupOldRollbackPoints) {
    // Create more points than the maximum
    for (int i = 0; i < 7; i++) {
        auto state = createTestState(i, "data" + std::to_string(i));
        manager_->createRollbackPoint("test_variant", state);
    }

    // List points - should be limited to maxRollbackPoints
    auto points = manager_->listRollbackPoints();
    EXPECT_LE(points.size(), config_.maxRollbackPoints);
}

TEST_F(RollbackManagerTest, RetentionPolicy) {
    // Create a rollback point marked as permanent
    auto state = createTestState(1, "permanent data");
    std::map<std::string, std::string> metadata{{"permanent", "true"}};
    auto id = manager_->createRollbackPoint("test_variant", state, metadata);

    // Force cleanup
    size_t removed = manager_->cleanupOldRollbackPoints();

    // Permanent point should still exist
    auto point = manager_->getRollbackPoint(id);
    EXPECT_TRUE(point.has_value());
}

TEST_F(RollbackManagerTest, HandleLargeState) {
    // Create a state that exceeds the size limit
    std::string largeData(2 * 1024 * 1024, 'x'); // 2MB
    auto state = createTestState(1, largeData);

    // Attempt to create a rollback point should throw
    EXPECT_THROW(
        manager_->createRollbackPoint("test_variant", state),
        std::runtime_error
    );
}

TEST_F(RollbackManagerTest, PersistenceAcrossRestarts) {
    // Create a rollback point
    auto state = createTestState(1, "test data");
    auto id = manager_->createRollbackPoint("test_variant", state);

    // Create a new manager instance (simulating restart)
    auto newManager = std::make_unique<RollbackManager>(config_, compatibilityChecker_);

    // Try to retrieve the point from the new instance
    auto point = newManager->getRollbackPoint(id);
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->state, state);
}

TEST_F(RollbackManagerTest, InvalidRollbackPoint) {
    // Try to restore a non-existent point
    EXPECT_FALSE(manager_->restoreToPoint("non_existent_id"));

    // Try to verify a non-existent point
    EXPECT_FALSE(manager_->verifyRollbackPoint("non_existent_id"));
}

TEST_F(RollbackManagerTest, ConfigurationAccess) {
    // Verify configuration is accessible and matches
    const auto& config = manager_->getConfig();
    EXPECT_EQ(config.maxRollbackPoints, config_.maxRollbackPoints);
    EXPECT_EQ(config.storagePath, config_.storagePath);
    EXPECT_EQ(config.enableIncrementalSnapshots, config_.enableIncrementalSnapshots);
}

TEST_F(RollbackManagerTest, ChecksumVerification) {
    // Create a test state with known content
    json state = {
        {"key1", "value1"},
        {"key2", 42},
        {"key3", {"nested", "object"}},
        {"key4", true}
    };

    // Create a rollback point
    auto id = manager_->createRollbackPoint("test_variant", state);
    ASSERT_FALSE(id.empty());

    // Get the rollback point
    auto point = manager_->getRollbackPoint(id);
    ASSERT_TRUE(point.has_value());

    // Verify that the stored checksum matches our independent calculation
    std::string expectedChecksum = computeSHA256(state.dump());
    EXPECT_EQ(point->checksum, expectedChecksum);

    // Verify using the manager's built-in verification
    EXPECT_TRUE(manager_->verifyRollbackPoint(id));

    // Modify the state and verify that checksums no longer match
    json modifiedState = state;
    modifiedState["key2"] = 43;
    std::string modifiedChecksum = computeSHA256(modifiedState.dump());
    EXPECT_NE(point->checksum, modifiedChecksum);
}

TEST_F(RollbackManagerTest, ChecksumConsistency) {
    // Create multiple rollback points and verify their checksums
    for (int i = 0; i < 5; i++) {
        json state = createTestState(i, "data" + std::to_string(i));
        auto id = manager_->createRollbackPoint("test_variant", state);
        auto point = manager_->getRollbackPoint(id);
        ASSERT_TRUE(point.has_value());

        // Verify checksum
        std::string expectedChecksum = computeSHA256(state.dump());
        EXPECT_EQ(point->checksum, expectedChecksum);
        EXPECT_TRUE(manager_->verifyRollbackPoint(id));
    }
} 
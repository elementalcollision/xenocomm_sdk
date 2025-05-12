#include "xenocomm/core/feedback_integration.h"
#include "xenocomm/core/feedback_loop.h"
#include "xenocomm/core/transmission_manager.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

namespace xenocomm {
namespace {

class MockConnectionManager : public core::ConnectionManager {
public:
    bool is_connected() const { return true; }
    Result<void> send(const std::vector<uint8_t>& data) {
        last_sent_data_ = data;
        return Result<void>();
    }
    Result<std::vector<uint8_t>> receive() {
        return Result<std::vector<uint8_t>>(last_sent_data_);
    }
private:
    std::vector<uint8_t> last_sent_data_;
};

class FeedbackIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        connection_mgr_ = std::make_unique<MockConnectionManager>();
        transmission_mgr_ = std::make_unique<core::TransmissionManager>(*connection_mgr_);
        feedback_loop_ = std::make_unique<FeedbackLoop>();
        
        // Configure integration with shorter intervals for testing
        FeedbackIntegration::Config config;
        config.strategy_update_interval = std::chrono::milliseconds(100);
        config.error_rate_threshold = 0.05;  // More sensitive for testing
        config.latency_increase_threshold = 0.3;
        config.throughput_decrease_threshold = 0.2;
        
        integration_ = std::make_unique<FeedbackIntegration>(
            *feedback_loop_,
            *transmission_mgr_,
            config);
    }

    void TearDown() override {
        if (integration_) {
            integration_->stop();
        }
    }

    void SimulateErrorCondition() {
        // Report several failed outcomes
        CommunicationOutcome outcome;
        outcome.success = false;
        outcome.timestamp = std::chrono::system_clock::now();
        outcome.retryCount = 2;
        outcome.errorType = "test_error";
        outcome.errorCount = 1;

        for (int i = 0; i < 5; i++) {
            feedback_loop_->reportOutcome(outcome);
        }
    }

    void SimulateLatencyIncrease() {
        // Report increasing latency values
        for (int i = 1; i <= 5; i++) {
            feedback_loop_->recordMetric("rtt_ms", 100.0 * i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void SimulateThroughputDecrease() {
        // Report decreasing throughput values
        for (int i = 5; i >= 1; i--) {
            feedback_loop_->recordMetric("throughput_bps", 1000000.0 * i);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::unique_ptr<MockConnectionManager> connection_mgr_;
    std::unique_ptr<core::TransmissionManager> transmission_mgr_;
    std::unique_ptr<FeedbackLoop> feedback_loop_;
    std::unique_ptr<FeedbackIntegration> integration_;
};

TEST_F(FeedbackIntegrationTest, StartAndStopIntegration) {
    ASSERT_TRUE(integration_->start().has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    integration_->stop();
}

TEST_F(FeedbackIntegrationTest, ErrorRateTriggersStrategyUpdate) {
    ASSERT_TRUE(integration_->start().has_value());
    
    // Get initial recommendation
    auto initial_result = integration_->get_latest_recommendation();
    ASSERT_TRUE(initial_result.has_value());
    auto initial_rec = initial_result.value();
    
    // Simulate error condition
    SimulateErrorCondition();
    
    // Force strategy update
    ASSERT_TRUE(integration_->update_strategy().has_value());
    
    // Get new recommendation
    auto updated_result = integration_->get_latest_recommendation();
    ASSERT_TRUE(updated_result.has_value());
    auto updated_rec = updated_result.value();
    
    // Verify error correction was increased
    EXPECT_NE(updated_rec.error_mode, initial_rec.error_mode);
    EXPECT_FALSE(updated_rec.explanation.empty());
    
    integration_->stop();
}

TEST_F(FeedbackIntegrationTest, LatencyTriggersFragmentSizeAdjustment) {
    ASSERT_TRUE(integration_->start().has_value());
    
    // Get initial recommendation
    auto initial_result = integration_->get_latest_recommendation();
    ASSERT_TRUE(initial_result.has_value());
    auto initial_rec = initial_result.value();
    
    // Simulate latency increase
    SimulateLatencyIncrease();
    
    // Force strategy update
    ASSERT_TRUE(integration_->update_strategy().has_value());
    
    // Get new recommendation
    auto updated_result = integration_->get_latest_recommendation();
    ASSERT_TRUE(updated_result.has_value());
    auto updated_rec = updated_result.value();
    
    // Verify fragment size was adjusted
    EXPECT_LT(updated_rec.fragment_config.max_fragment_size,
              initial_rec.fragment_config.max_fragment_size);
    EXPECT_FALSE(updated_rec.explanation.empty());
    
    integration_->stop();
}

TEST_F(FeedbackIntegrationTest, ThroughputTriggersWindowSizeAdjustment) {
    ASSERT_TRUE(integration_->start().has_value());
    
    // Get initial recommendation
    auto initial_result = integration_->get_latest_recommendation();
    ASSERT_TRUE(initial_result.has_value());
    auto initial_rec = initial_result.value();
    
    // Simulate throughput decrease
    SimulateThroughputDecrease();
    
    // Force strategy update
    ASSERT_TRUE(integration_->update_strategy().has_value());
    
    // Get new recommendation
    auto updated_result = integration_->get_latest_recommendation();
    ASSERT_TRUE(updated_result.has_value());
    auto updated_rec = updated_result.value();
    
    // Verify window size was adjusted
    EXPECT_NE(updated_rec.flow_config.initial_window_size,
              initial_rec.flow_config.initial_window_size);
    EXPECT_FALSE(updated_rec.explanation.empty());
    
    integration_->stop();
}

TEST_F(FeedbackIntegrationTest, StrategyCallbackIsInvoked) {
    bool callback_invoked = false;
    FeedbackIntegration::StrategyRecommendation received_rec;
    
    integration_->set_strategy_callback(
        [&](const FeedbackIntegration::StrategyRecommendation& rec) {
            callback_invoked = true;
            received_rec = rec;
        });
    
    ASSERT_TRUE(integration_->start().has_value());
    
    // Simulate conditions that should trigger an update
    SimulateErrorCondition();
    SimulateLatencyIncrease();
    SimulateThroughputDecrease();
    
    // Force strategy update
    ASSERT_TRUE(integration_->update_strategy().has_value());
    
    // Verify callback was invoked
    EXPECT_TRUE(callback_invoked);
    EXPECT_FALSE(received_rec.explanation.empty());
    
    integration_->stop();
}

TEST_F(FeedbackIntegrationTest, ConfigurationUpdate) {
    ASSERT_TRUE(integration_->start().has_value());
    
    // Create new configuration
    FeedbackIntegration::Config new_config;
    new_config.strategy_update_interval = std::chrono::milliseconds(200);
    new_config.error_rate_threshold = 0.1;
    new_config.latency_increase_threshold = 0.4;
    new_config.throughput_decrease_threshold = 0.3;
    
    // Update configuration
    integration_->set_config(new_config);
    
    // Verify configuration was updated
    const auto& current_config = integration_->get_config();
    EXPECT_EQ(current_config.strategy_update_interval, new_config.strategy_update_interval);
    EXPECT_EQ(current_config.error_rate_threshold, new_config.error_rate_threshold);
    EXPECT_EQ(current_config.latency_increase_threshold, new_config.latency_increase_threshold);
    EXPECT_EQ(current_config.throughput_decrease_threshold, new_config.throughput_decrease_threshold);
    
    integration_->stop();
}

} // namespace
} // namespace xenocomm 
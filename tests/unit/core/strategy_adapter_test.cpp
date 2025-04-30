#include "xenocomm/core/strategy_adapter.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <memory>
#include <thread>

using namespace xenocomm;
using Catch::Matchers::WithinRel;

class MockFeedbackLoop : public FeedbackLoop {
public:
    explicit MockFeedbackLoop() : FeedbackLoop() {}

    Result<DetailedMetrics> getDetailedMetrics() const override {
        if (shouldFailMetrics) {
            return Result<DetailedMetrics>::error("Mock error");
        }

        DetailedMetrics metrics;
        metrics.basic.successRate = mockSuccessRate;
        metrics.basic.totalTransactions = mockTotalTransactions;
        metrics.latencyStats.mean = mockLatencyMean;
        metrics.throughputStats.mean = mockThroughputMean;
        metrics.basic.errorRate = mockErrorRate;
        metrics.latencyTrend.trendSlope = mockLatencyTrendSlope;
        metrics.throughputTrend.isStationary = mockThroughputIsStationary;
        
        if (!mockErrorTypes.empty()) {
            metrics.errorTypeFrequency = mockErrorTypes;
        }

        return Result<DetailedMetrics>::ok(metrics);
    }

    // Mock control
    bool shouldFailMetrics{false};
    double mockSuccessRate{0.98};
    uint32_t mockTotalTransactions{1000};
    double mockLatencyMean{50.0};
    double mockThroughputMean{2048.0};
    double mockErrorRate{0.02};
    double mockLatencyTrendSlope{0.0};
    bool mockThroughputIsStationary{true};
    std::map<std::string, uint32_t> mockErrorTypes;
};

TEST_CASE("StrategyAdapter basic functionality", "[strategy_adapter]") {
    auto feedback = std::make_shared<MockFeedbackLoop>();
    StrategyAdapter adapter(feedback);

    SECTION("Default thresholds") {
        const auto& thresholds = adapter.getAdaptationThresholds();
        REQUIRE_THAT(thresholds.minSuccessRate, WithinRel(0.95));
        REQUIRE_THAT(thresholds.maxLatencyMs, WithinRel(200.0));
        REQUIRE_THAT(thresholds.minThroughputBps, WithinRel(1024.0));
        REQUIRE_THAT(thresholds.maxErrorRate, WithinRel(0.05));
    }

    SECTION("Custom thresholds") {
        AdaptationThresholds custom;
        custom.minSuccessRate = 0.99;
        custom.maxLatencyMs = 100.0;
        adapter.setAdaptationThresholds(custom);

        const auto& thresholds = adapter.getAdaptationThresholds();
        REQUIRE_THAT(thresholds.minSuccessRate, WithinRel(0.99));
        REQUIRE_THAT(thresholds.maxLatencyMs, WithinRel(100.0));
    }
}

TEST_CASE("StrategyAdapter recommendations", "[strategy_adapter]") {
    auto feedback = std::make_shared<MockFeedbackLoop>();
    StrategyAdapter adapter(feedback);

    SECTION("Good performance") {
        feedback->mockSuccessRate = 0.98;
        feedback->mockLatencyMean = 50.0;
        feedback->mockThroughputMean = 2048.0;
        feedback->mockErrorRate = 0.02;

        auto result = adapter.evaluateAndRecommend();
        REQUIRE(result.is_ok());
        REQUIRE_THAT(result.value().confidenceScore, WithinRel(1.0, 0.1));
    }

    SECTION("Poor performance") {
        feedback->mockSuccessRate = 0.85;
        feedback->mockLatencyMean = 300.0;
        feedback->mockThroughputMean = 512.0;
        feedback->mockErrorRate = 0.15;

        auto result = adapter.evaluateAndRecommend();
        REQUIRE(result.is_ok());
        REQUIRE_THAT(result.value().confidenceScore, WithinRel(0.5, 0.1));

        const auto& config = result.value().config;
        REQUIRE(config.errorCorrection == ErrorCorrectionMode::REED_SOLOMON);
        REQUIRE(config.enableInterleaving);
        REQUIRE(config.windowSize < 16); // Should be reduced
    }

    SECTION("Insufficient samples") {
        feedback->mockTotalTransactions = 10; // Below minimum required

        auto result = adapter.evaluateAndRecommend();
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error().find("Insufficient") != std::string::npos);
    }

    SECTION("Metrics failure") {
        feedback->shouldFailMetrics = true;

        auto result = adapter.evaluateAndRecommend();
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error().find("Failed to get metrics") != std::string::npos);
    }
}

TEST_CASE("StrategyAdapter A/B testing", "[strategy_adapter]") {
    auto feedback = std::make_shared<MockFeedbackLoop>();
    StrategyAdapter adapter(feedback);

    SECTION("Basic A/B test flow") {
        // Start test
        auto result = adapter.startABTest("strategy_a", "strategy_b", 
                                        std::chrono::seconds(10));
        REQUIRE(result.is_ok());

        // Record some outcomes
        CommunicationOutcome goodOutcome{
            true, std::chrono::microseconds(100), 1024, 0, 0, "", 
            std::chrono::system_clock::now()
        };
        CommunicationOutcome badOutcome{
            false, std::chrono::microseconds(200), 512, 2, 1, "timeout",
            std::chrono::system_clock::now()
        };

        result = adapter.recordABTestOutcome("strategy_a", goodOutcome);
        REQUIRE(result.is_ok());
        result = adapter.recordABTestOutcome("strategy_b", badOutcome);
        REQUIRE(result.is_ok());

        // Try to start another test (should fail)
        result = adapter.startABTest("strategy_c", "strategy_d", 
                                   std::chrono::seconds(10));
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error().find("already in progress") != std::string::npos);

        // Wait for test to complete
        std::this_thread::sleep_for(std::chrono::seconds(11));

        // Get results
        auto testResult = adapter.getABTestResults();
        REQUIRE(testResult.is_ok());
        REQUIRE(testResult.value().recommendedStrategy == "strategy_a");
        REQUIRE(testResult.value().isSignificant);
    }

    SECTION("Invalid strategy name") {
        auto result = adapter.startABTest("strategy_a", "strategy_b", 
                                        std::chrono::seconds(10));
        REQUIRE(result.is_ok());

        CommunicationOutcome outcome{
            true, std::chrono::microseconds(100), 1024, 0, 0, "",
            std::chrono::system_clock::now()
        };

        result = adapter.recordABTestOutcome("invalid_strategy", outcome);
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error().find("Unknown strategy") != std::string::npos);
    }
}

TEST_CASE("StrategyAdapter performance insights", "[strategy_adapter]") {
    auto feedback = std::make_shared<MockFeedbackLoop>();
    StrategyAdapter adapter(feedback);

    SECTION("Latency trend insights") {
        feedback->mockLatencyTrendSlope = 0.2; // Increasing trend
        
        auto result = adapter.getPerformanceInsights();
        REQUIRE(result.is_ok());
        REQUIRE(std::find(result.value().begin(), result.value().end(),
                         "Latency is showing an increasing trend") != 
                result.value().end());
    }

    SECTION("Error pattern insights") {
        feedback->mockErrorTypes["timeout"] = 10;
        feedback->mockErrorTypes["connection_reset"] = 5;

        auto result = adapter.getPerformanceInsights();
        REQUIRE(result.is_ok());
        REQUIRE(std::find_if(result.value().begin(), result.value().end(),
                            [](const std::string& s) {
                                return s.find("timeout") != std::string::npos;
                            }) != result.value().end());
    }

    SECTION("Throughput stability insights") {
        feedback->mockThroughputIsStationary = false;

        auto result = adapter.getPerformanceInsights();
        REQUIRE(result.is_ok());
        REQUIRE(std::find(result.value().begin(), result.value().end(),
                         "Throughput shows significant variability") != 
                result.value().end());
    }
}

TEST_CASE("StrategyAdapter strategy effectiveness", "[strategy_adapter]") {
    auto feedback = std::make_shared<MockFeedbackLoop>();
    StrategyAdapter adapter(feedback);

    SECTION("Calculate effectiveness scores") {
        feedback->mockSuccessRate = 0.95;
        feedback->mockLatencyMean = 100.0;
        feedback->mockThroughputMean = 2048.0;
        feedback->mockErrorRate = 0.03;

        auto result = adapter.getStrategyEffectiveness();
        REQUIRE(result.is_ok());
        
        const auto& scores = result.value();
        REQUIRE_THAT(scores.at("success_rate"), WithinRel(0.95));
        REQUIRE_THAT(scores.at("latency_score"), WithinRel(0.5, 0.1));
        REQUIRE_THAT(scores.at("throughput_score"), WithinRel(2.0, 0.1));
        REQUIRE_THAT(scores.at("error_handling"), WithinRel(0.4, 0.1));
    }
}

TEST_CASE("StrategyAdapter adaptation triggers", "[strategy_adapter]") {
    auto feedback = std::make_shared<MockFeedbackLoop>();
    StrategyAdapter adapter(feedback);

    SECTION("Should adapt - poor performance") {
        feedback->mockSuccessRate = 0.85; // Below threshold
        feedback->mockLatencyMean = 300.0; // Above threshold
        feedback->mockTotalTransactions = 1000;

        auto result = adapter.shouldAdaptStrategy(feedback->getDetailedMetrics().value());
        REQUIRE(result.is_ok());
        REQUIRE(result.value());
    }

    SECTION("Should not adapt - good performance") {
        feedback->mockSuccessRate = 0.98;
        feedback->mockLatencyMean = 50.0;
        feedback->mockTotalTransactions = 1000;

        auto result = adapter.shouldAdaptStrategy(feedback->getDetailedMetrics().value());
        REQUIRE(result.is_ok());
        REQUIRE_FALSE(result.value());
    }

    SECTION("Should not adapt - insufficient samples") {
        feedback->mockTotalTransactions = 10;

        auto result = adapter.shouldAdaptStrategy(feedback->getDetailedMetrics().value());
        REQUIRE(result.is_ok());
        REQUIRE_FALSE(result.value());
    }
}

TEST_CASE("StrategyAdapter optimal configuration", "[strategy_adapter]") {
    auto feedback = std::make_shared<MockFeedbackLoop>();
    StrategyAdapter adapter(feedback);

    SECTION("High error rate configuration") {
        feedback->mockErrorRate = 0.15;
        feedback->mockTotalTransactions = 1000;

        auto result = adapter.getOptimalConfig(feedback->getDetailedMetrics().value());
        REQUIRE(result.is_ok());
        
        const auto& config = result.value();
        REQUIRE(config.errorCorrection == ErrorCorrectionMode::REED_SOLOMON);
        REQUIRE(config.enableInterleaving);
        REQUIRE(config.windowSize < 16); // Should be reduced
        REQUIRE(config.maxRetries > 3); // Should be increased
    }

    SECTION("High latency configuration") {
        feedback->mockLatencyMean = 300.0;
        feedback->mockTotalTransactions = 1000;

        auto result = adapter.getOptimalConfig(feedback->getDetailedMetrics().value());
        REQUIRE(result.is_ok());
        
        const auto& config = result.value();
        REQUIRE(config.fragmentSize < 1024); // Should be reduced
    }

    SECTION("Low throughput configuration") {
        feedback->mockThroughputMean = 512.0;
        feedback->mockTotalTransactions = 1000;

        auto result = adapter.getOptimalConfig(feedback->getDetailedMetrics().value());
        REQUIRE(result.is_ok());
        
        const auto& config = result.value();
        REQUIRE(config.fragmentSize > 1024); // Should be increased
    }
} 
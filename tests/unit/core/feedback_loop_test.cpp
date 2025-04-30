#include "xenocomm/core/feedback_loop.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <thread>
#include <random>

using namespace xenocomm;
using Catch::Matchers::WithinRel;

TEST_CASE("FeedbackLoop basic functionality", "[feedback_loop]") {
    FeedbackLoopConfig config;
    config.metricsWindowSize = std::chrono::seconds(10);
    config.maxStoredOutcomes = 100;
    config.enablePersistence = false;

    FeedbackLoop feedback(config);

    SECTION("Configuration management") {
        const auto& currentConfig = feedback.getConfig();
        REQUIRE(currentConfig.metricsWindowSize == std::chrono::seconds(10));
        REQUIRE(currentConfig.maxStoredOutcomes == 100);
        REQUIRE_FALSE(currentConfig.enablePersistence);
    }

    SECTION("Basic outcome reporting") {
        auto result = feedback.addCommunicationResult(
            true, std::chrono::microseconds(100), 1024, 0, 0);
        REQUIRE(result.is_ok());

        auto outcomes = feedback.getRecentOutcomes(1);
        REQUIRE(outcomes.is_ok());
        REQUIRE(outcomes.value().size() == 1);
        REQUIRE(outcomes.value()[0].success);
        REQUIRE(outcomes.value()[0].latency == std::chrono::microseconds(100));
        REQUIRE(outcomes.value()[0].bytesTransferred == 1024);
    }

    SECTION("Metric recording and retrieval") {
        auto result = feedback.recordMetric("test_metric", 42.0);
        REQUIRE(result.is_ok());

        auto value = feedback.getMetricValue("test_metric");
        REQUIRE(value.is_ok());
        REQUIRE_THAT(value.value(), WithinRel(42.0));

        auto nonexistent = feedback.getMetricValue("nonexistent");
        REQUIRE_FALSE(nonexistent.is_ok());
    }
}

TEST_CASE("FeedbackLoop metrics calculation", "[feedback_loop]") {
    FeedbackLoopConfig config;
    config.metricsWindowSize = std::chrono::seconds(10);
    FeedbackLoop feedback(config);

    // Add some test data
    for (int i = 0; i < 10; ++i) {
        bool success = (i % 3 != 0); // 7 successes, 3 failures
        auto result = feedback.addCommunicationResult(
            success,
            std::chrono::microseconds(100 * (i + 1)),
            1024,
            i % 2, // Alternating retry counts
            success ? 0 : 1 // Errors on failures
        );
        REQUIRE(result.is_ok());
    }

    auto metrics = feedback.getCurrentMetrics();
    REQUIRE(metrics.is_ok());
    
    const auto& summary = metrics.value();
    REQUIRE_THAT(summary.successRate, WithinRel(0.7)); // 7/10 success rate
    REQUIRE(summary.totalTransactions == 10);
    REQUIRE(summary.errorRate > 0.0);
    REQUIRE(summary.throughputBytesPerSecond > 0.0);
}

TEST_CASE("FeedbackLoop data pruning", "[feedback_loop]") {
    FeedbackLoopConfig config;
    config.metricsWindowSize = std::chrono::seconds(1);
    config.maxStoredOutcomes = 5;
    FeedbackLoop feedback(config);

    // Add more outcomes than the maximum
    for (int i = 0; i < 10; ++i) {
        auto result = feedback.addCommunicationResult(
            true, std::chrono::microseconds(100), 1024);
        REQUIRE(result.is_ok());
    }

    auto outcomes = feedback.getRecentOutcomes(100);
    REQUIRE(outcomes.is_ok());
    REQUIRE(outcomes.value().size() == 5); // Should be limited by maxStoredOutcomes

    // Wait for window to expire
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Add one more outcome to trigger pruning
    feedback.addCommunicationResult(true, std::chrono::microseconds(100), 1024);

    outcomes = feedback.getRecentOutcomes(100);
    REQUIRE(outcomes.is_ok());
    REQUIRE(outcomes.value().size() == 1); // Only the newest outcome should remain
}

TEST_CASE("FeedbackLoop thread safety", "[feedback_loop]") {
    FeedbackLoopConfig config;
    FeedbackLoop feedback(config);

    constexpr int numThreads = 4;
    constexpr int operationsPerThread = 1000;
    std::vector<std::thread> threads;

    // Launch multiple threads to concurrently add outcomes and metrics
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&feedback, i]() {
            for (int j = 0; j < operationsPerThread; ++j) {
                feedback.addCommunicationResult(
                    true, std::chrono::microseconds(100), 1024);
                feedback.recordMetric("metric_" + std::to_string(i), j);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify that all operations were recorded
    auto outcomes = feedback.getRecentOutcomes(numThreads * operationsPerThread);
    REQUIRE(outcomes.is_ok());
    REQUIRE(outcomes.value().size() <= config.maxStoredOutcomes);

    // Verify that metrics were recorded for each thread
    for (int i = 0; i < numThreads; ++i) {
        auto value = feedback.getMetricValue("metric_" + std::to_string(i));
        REQUIRE(value.is_ok());
    }
}

TEST_CASE("FeedbackLoop error handling", "[feedback_loop]") {
    FeedbackLoop feedback;

    SECTION("Empty metric name") {
        auto result = feedback.recordMetric("", 42.0);
        REQUIRE_FALSE(result.is_ok());
    }

    SECTION("Nonexistent metric retrieval") {
        auto result = feedback.getMetricValue("nonexistent");
        REQUIRE_FALSE(result.is_ok());
    }

    SECTION("Zero limit for recent outcomes") {
        auto result = feedback.getRecentOutcomes(0);
        REQUIRE(result.is_ok());
        REQUIRE(result.value().empty());
    }
}

TEST_CASE("FeedbackLoop statistical analysis", "[feedback_loop]") {
    FeedbackLoopConfig config;
    config.metricsWindowSize = std::chrono::seconds(10);
    config.maxStoredOutcomes = 1000;
    config.enableDetailedAnalysis = true;
    config.outlierThreshold = 3.0;
    
    FeedbackLoop feedback(config);

    // Generate test data with known statistical properties
    std::mt19937 rng(42); // Fixed seed for reproducibility
    std::normal_distribution<double> latencyDist(100.0, 20.0); // mean=100ms, std=20ms
    std::normal_distribution<double> throughputDist(1024.0, 200.0); // mean=1024 B/s, std=200 B/s

    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < 100; ++i) {
        double latency = std::max(1.0, latencyDist(rng));
        double throughput = std::max(1.0, throughputDist(rng));
        uint32_t bytes = static_cast<uint32_t>(throughput * (latency / 1000.0));
        
        feedback.addCommunicationResult(
            true,
            std::chrono::microseconds(static_cast<int64_t>(latency * 1000)),
            bytes,
            i % 3, // Some retry variation
            i % 5 == 0 ? 1 : 0, // Occasional errors
            i % 5 == 0 ? "timeout" : ""
        );
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    SECTION("Detailed metrics calculation") {
        auto result = feedback.getDetailedMetrics();
        REQUIRE(result.is_ok());
        
        const auto& metrics = result.value();
        REQUIRE(metrics.basic.totalTransactions == 100);
        REQUIRE_THAT(metrics.basic.successRate, WithinRel(0.8, 0.1)); // 80% success rate
        
        // Verify latency statistics
        REQUIRE_THAT(metrics.latencyStats.mean, WithinRel(100.0, 0.2)); // Within 20% of expected mean
        REQUIRE(metrics.latencyStats.min > 0);
        REQUIRE(metrics.latencyStats.max < 200); // Should be within reasonable bounds
        REQUIRE(metrics.latencyStats.standardDeviation > 0);
        
        // Verify throughput statistics
        REQUIRE(metrics.throughputStats.min > 0);
        REQUIRE(metrics.throughputStats.mean > 0);
        REQUIRE(metrics.peakThroughput > metrics.sustainedThroughput);
        
        // Verify error analysis
        REQUIRE(metrics.errorTypeFrequency.count("timeout") > 0);
        REQUIRE_THAT(metrics.retryStats.mean, WithinRel(1.0, 0.2)); // Average retry count
    }

    SECTION("Latency distribution analysis") {
        auto result = feedback.analyzeLatencyDistribution();
        REQUIRE(result.is_ok());
        
        const auto& stats = result.value();
        REQUIRE_THAT(stats.mean, WithinRel(100.0, 0.2));
        REQUIRE_THAT(stats.standardDeviation, WithinRel(20.0, 0.3));
        REQUIRE(stats.percentile90 > stats.median);
        REQUIRE(stats.percentile95 > stats.percentile90);
        REQUIRE(stats.percentile99 > stats.percentile95);
    }

    SECTION("Throughput distribution analysis") {
        auto result = feedback.analyzeThroughputDistribution();
        REQUIRE(result.is_ok());
        
        const auto& stats = result.value();
        REQUIRE(stats.min > 0);
        REQUIRE(stats.max > stats.mean);
        REQUIRE(stats.standardDeviation > 0);
    }

    SECTION("Latency trend analysis") {
        auto result = feedback.analyzeLatencyTrend();
        REQUIRE(result.is_ok());
        
        const auto& analysis = result.value();
        REQUIRE(analysis.forecast.size() == config.forecastHorizon);
        REQUIRE(std::abs(analysis.autocorrelation) <= 1.0);
    }

    SECTION("Error type distribution") {
        auto result = feedback.getErrorTypeDistribution();
        REQUIRE(result.is_ok());
        
        const auto& distribution = result.value();
        REQUIRE(distribution.count("timeout") > 0);
        REQUIRE(distribution.at("timeout") == 20); // 20% of samples (i % 5 == 0)
    }

    SECTION("Outlier detection") {
        // Add some obvious outliers
        feedback.addCommunicationResult(
            false,
            std::chrono::microseconds(500000), // 500ms
            1024,
            5,
            1,
            "extreme_latency"
        );

        auto result = feedback.getOutliers();
        REQUIRE(result.is_ok());
        REQUIRE(!result.value().empty());
        
        // Verify the outlier properties
        const auto& outliers = result.value();
        bool foundExtreme = false;
        for (const auto& outlier : outliers) {
            if (outlier.latency == std::chrono::microseconds(500000)) {
                foundExtreme = true;
                break;
            }
        }
        REQUIRE(foundExtreme);
    }
}

TEST_CASE("FeedbackLoop configuration validation", "[feedback_loop]") {
    SECTION("Disabled detailed analysis") {
        FeedbackLoopConfig config;
        config.enableDetailedAnalysis = false;
        FeedbackLoop feedback(config);

        auto result = feedback.getDetailedMetrics();
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error().find("disabled") != std::string::npos);
    }

    SECTION("Custom outlier threshold") {
        FeedbackLoopConfig config;
        config.outlierThreshold = 2.0; // More sensitive outlier detection
        FeedbackLoop feedback(config);

        // Add mostly normal data
        for (int i = 0; i < 50; ++i) {
            feedback.addCommunicationResult(
                true,
                std::chrono::microseconds(100000), // 100ms
                1024
            );
        }

        // Add one moderate outlier
        feedback.addCommunicationResult(
            true,
            std::chrono::microseconds(250000), // 250ms
            1024
        );

        auto result = feedback.getOutliers();
        REQUIRE(result.is_ok());
        REQUIRE(!result.value().empty()); // Should detect the moderate outlier
    }
} 
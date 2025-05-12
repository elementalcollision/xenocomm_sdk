#pragma once

#include "xenocomm/core/feedback_loop.h"
#include "xenocomm/utils/result.hpp"
#include "xenocomm/core/error_correction_mode.h"
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xenocomm {

/**
 * @brief Configuration parameters for a communication strategy
 */
struct StrategyConfig {
    uint32_t fragmentSize{1024};
    uint32_t windowSize{16};
    uint32_t maxRetries{3};
    std::chrono::milliseconds timeout{1000};
    core::ErrorCorrectionMode errorCorrection{core::ErrorCorrectionMode::CHECKSUM_ONLY};
    bool enableInterleaving{false};
    uint16_t interleavingDepth{8};
};

/**
 * @brief Performance thresholds for strategy adaptation
 */
struct AdaptationThresholds {
    double minSuccessRate{0.95};
    double maxLatencyMs{200.0};
    double minThroughputBps{1024.0};
    double maxErrorRate{0.05};
    uint32_t minSamplesRequired{100};
    std::chrono::seconds evaluationWindow{300};
};

/**
 * @brief Results from A/B testing of different strategies
 */
struct ABTestResult {
    std::string strategyA;
    std::string strategyB;
    double successRateDiff;
    double latencyDiff;
    double throughputDiff;
    double errorRateDiff;
    bool isSignificant;
    std::string recommendedStrategy;
    std::string explanation;
};

/**
 * @brief Strategy recommendation with explanation
 */
struct StrategyRecommendation {
    StrategyConfig config;
    double confidenceScore;
    std::string explanation;
    std::vector<std::string> insights;
    std::chrono::system_clock::time_point validUntil;
};

/**
 * @brief Interface for strategy adaptation based on feedback data
 */
class StrategyAdapter {
public:
    explicit StrategyAdapter(std::shared_ptr<FeedbackLoop> feedback);
    ~StrategyAdapter();

    // Strategy evaluation and recommendation
    Result<StrategyRecommendation> evaluateAndRecommend() const;
    Result<StrategyRecommendation> getRecommendationForCondition(
        const DetailedMetrics& metrics) const;

    // A/B testing management
    Result<void> startABTest(const std::string& strategyA,
                            const std::string& strategyB,
                            std::chrono::seconds duration);
    Result<void> recordABTestOutcome(const std::string& strategy,
                                    const CommunicationOutcome& outcome);
    Result<ABTestResult> getABTestResults() const;

    // Configuration and thresholds
    void setAdaptationThresholds(const AdaptationThresholds& thresholds);
    const AdaptationThresholds& getAdaptationThresholds() const;
    
    // Strategy insights
    Result<std::vector<std::string>> getPerformanceInsights() const;
    Result<std::map<std::string, double>> getStrategyEffectiveness() const;

    // Real-time adaptation
    Result<bool> shouldAdaptStrategy(const DetailedMetrics& currentMetrics) const;
    Result<StrategyConfig> getOptimalConfig(const DetailedMetrics& metrics) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Prevent copying
    StrategyAdapter(const StrategyAdapter&) = delete;
    StrategyAdapter& operator=(const StrategyAdapter&) = delete;
};

} // namespace xenocomm 
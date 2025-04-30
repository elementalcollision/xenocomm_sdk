#include "xenocomm/core/strategy_adapter.h"
#include "xenocomm/utils/logging.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <sstream>

namespace xenocomm {

struct StrategyAdapter::Impl {
    std::shared_ptr<FeedbackLoop> feedback;
    AdaptationThresholds thresholds;
    
    // A/B testing state
    struct ABTestState {
        std::string strategyA;
        std::string strategyB;
        std::chrono::system_clock::time_point startTime;
        std::chrono::seconds duration;
        std::vector<CommunicationOutcome> outcomesA;
        std::vector<CommunicationOutcome> outcomesB;
        bool isActive{false};
    };
    
    ABTestState abTest;
    mutable std::mutex mutex;

    explicit Impl(std::shared_ptr<FeedbackLoop> fb)
        : feedback(std::move(fb)) {}

    bool meetsMinimumSamples(const DetailedMetrics& metrics) const {
        return metrics.basic.totalTransactions >= thresholds.minSamplesRequired;
    }

    double calculateConfidenceScore(const DetailedMetrics& metrics) const {
        if (!meetsMinimumSamples(metrics)) {
            return 0.0;
        }

        // Calculate confidence based on multiple factors
        double successScore = metrics.basic.successRate >= thresholds.minSuccessRate ? 1.0 : 0.5;
        double latencyScore = metrics.latencyStats.mean <= thresholds.maxLatencyMs ? 1.0 : 0.5;
        double throughputScore = metrics.throughputStats.mean >= thresholds.minThroughputBps ? 1.0 : 0.5;
        double errorScore = metrics.basic.errorRate <= thresholds.maxErrorRate ? 1.0 : 0.5;

        // Weight the scores (can be adjusted based on priorities)
        return (successScore * 0.4 + latencyScore * 0.3 + 
                throughputScore * 0.2 + errorScore * 0.1);
    }

    std::vector<std::string> generateInsights(const DetailedMetrics& metrics) const {
        std::vector<std::string> insights;

        // Analyze latency trends
        if (metrics.latencyTrend.trendSlope > 0.1) {
            insights.push_back("Latency is showing an increasing trend");
        } else if (metrics.latencyTrend.trendSlope < -0.1) {
            insights.push_back("Latency is improving over time");
        }

        // Analyze error patterns
        if (!metrics.errorTypeFrequency.empty()) {
            std::stringstream ss;
            ss << "Most common error type: " 
               << std::max_element(metrics.errorTypeFrequency.begin(),
                                 metrics.errorTypeFrequency.end(),
                                 [](const auto& a, const auto& b) {
                                     return a.second < b.second;
                                 })->first;
            insights.push_back(ss.str());
        }

        // Analyze throughput stability
        if (!metrics.throughputTrend.isStationary) {
            insights.push_back("Throughput shows significant variability");
        }

        // Check for outliers
        if (metrics.latencyStats.percentile99 > 
            metrics.latencyStats.mean + 3 * metrics.latencyStats.standardDeviation) {
            insights.push_back("Significant latency spikes detected");
        }

        return insights;
    }

    StrategyConfig optimizeConfig(const DetailedMetrics& metrics) const {
        StrategyConfig config;

        // Adjust fragment size based on latency and throughput
        if (metrics.latencyStats.mean > thresholds.maxLatencyMs) {
            config.fragmentSize = std::max(512u, config.fragmentSize / 2);
        } else if (metrics.throughputStats.mean < thresholds.minThroughputBps) {
            config.fragmentSize = std::min(4096u, config.fragmentSize * 2);
        }

        // Adjust window size based on network conditions
        if (metrics.basic.errorRate > thresholds.maxErrorRate) {
            config.windowSize = std::max(4u, config.windowSize / 2);
        } else if (metrics.basic.successRate > thresholds.minSuccessRate) {
            config.windowSize = std::min(32u, config.windowSize * 2);
        }

        // Adjust error correction based on error patterns
        if (metrics.basic.errorRate > thresholds.maxErrorRate * 2) {
            config.errorCorrection = ErrorCorrectionMode::REED_SOLOMON;
            config.enableInterleaving = true;
        } else if (metrics.basic.errorRate > thresholds.maxErrorRate) {
            config.errorCorrection = ErrorCorrectionMode::REED_SOLOMON;
            config.enableInterleaving = false;
        }

        // Adjust retry settings based on success rate
        config.maxRetries = static_cast<uint32_t>(
            std::ceil(3.0 * (1.0 - metrics.basic.successRate)));

        return config;
    }

    bool isSignificantDifference(double valueA, double valueB, 
                                size_t samplesA, size_t samplesB) const {
        // Simple statistical significance test (t-test approximation)
        double diff = std::abs(valueA - valueB);
        double combinedVariance = 1.0 / samplesA + 1.0 / samplesB;
        return diff > 2.0 * std::sqrt(combinedVariance); // 95% confidence
    }
};

StrategyAdapter::StrategyAdapter(std::shared_ptr<FeedbackLoop> feedback)
    : impl_(std::make_unique<Impl>(std::move(feedback))) {}

StrategyAdapter::~StrategyAdapter() = default;

Result<StrategyRecommendation> StrategyAdapter::evaluateAndRecommend() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        auto metricsResult = impl_->feedback->getDetailedMetrics();
        if (!metricsResult.is_ok()) {
            return Result<StrategyRecommendation>::error(
                "Failed to get metrics: " + metricsResult.error());
        }

        return getRecommendationForCondition(metricsResult.value());
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to evaluate and recommend strategy: " + std::string(e.what()));
        return Result<StrategyRecommendation>::error(
            "Failed to evaluate and recommend strategy: " + std::string(e.what()));
    }
}

Result<StrategyRecommendation> StrategyAdapter::getRecommendationForCondition(
    const DetailedMetrics& metrics) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        if (!impl_->meetsMinimumSamples(metrics)) {
            return Result<StrategyRecommendation>::error(
                "Insufficient samples for recommendation");
        }

        StrategyRecommendation recommendation;
        recommendation.config = impl_->optimizeConfig(metrics);
        recommendation.confidenceScore = impl_->calculateConfidenceScore(metrics);
        recommendation.insights = impl_->generateInsights(metrics);
        
        // Set recommendation validity period
        recommendation.validUntil = std::chrono::system_clock::now() + 
            impl_->thresholds.evaluationWindow;

        // Generate explanation
        std::stringstream explanation;
        explanation << "Recommendation based on: "
                   << metrics.basic.totalTransactions << " transactions, "
                   << "success rate: " << metrics.basic.successRate * 100 << "%, "
                   << "avg latency: " << metrics.latencyStats.mean << "ms";
        recommendation.explanation = explanation.str();

        return Result<StrategyRecommendation>::ok(std::move(recommendation));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get recommendation: " + std::string(e.what()));
        return Result<StrategyRecommendation>::error(
            "Failed to get recommendation: " + std::string(e.what()));
    }
}

Result<void> StrategyAdapter::startABTest(
    const std::string& strategyA, const std::string& strategyB,
    std::chrono::seconds duration) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        if (impl_->abTest.isActive) {
            return Result<void>::error("A/B test already in progress");
        }

        impl_->abTest = Impl::ABTestState{
            strategyA,
            strategyB,
            std::chrono::system_clock::now(),
            duration,
            {},
            {},
            true
        };

        return Result<void>::ok();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start A/B test: " + std::string(e.what()));
        return Result<void>::error(
            "Failed to start A/B test: " + std::string(e.what()));
    }
}

Result<void> StrategyAdapter::recordABTestOutcome(
    const std::string& strategy, const CommunicationOutcome& outcome) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        if (!impl_->abTest.isActive) {
            return Result<void>::error("No active A/B test");
        }

        auto now = std::chrono::system_clock::now();
        if (now > impl_->abTest.startTime + impl_->abTest.duration) {
            impl_->abTest.isActive = false;
            return Result<void>::error("A/B test period has ended");
        }

        if (strategy == impl_->abTest.strategyA) {
            impl_->abTest.outcomesA.push_back(outcome);
        } else if (strategy == impl_->abTest.strategyB) {
            impl_->abTest.outcomesB.push_back(outcome);
        } else {
            return Result<void>::error("Unknown strategy: " + strategy);
        }

        return Result<void>::ok();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to record A/B test outcome: " + std::string(e.what()));
        return Result<void>::error(
            "Failed to record A/B test outcome: " + std::string(e.what()));
    }
}

Result<ABTestResult> StrategyAdapter::getABTestResults() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        if (impl_->abTest.isActive) {
            return Result<ABTestResult>::error("A/B test still in progress");
        }

        if (impl_->abTest.outcomesA.empty() || impl_->abTest.outcomesB.empty()) {
            return Result<ABTestResult>::error("Insufficient data for comparison");
        }

        ABTestResult result;
        result.strategyA = impl_->abTest.strategyA;
        result.strategyB = impl_->abTest.strategyB;

        // Calculate metrics for strategy A
        double successRateA = std::count_if(impl_->abTest.outcomesA.begin(),
            impl_->abTest.outcomesA.end(),
            [](const auto& o) { return o.success; }) / 
            static_cast<double>(impl_->abTest.outcomesA.size());

        double avgLatencyA = std::accumulate(impl_->abTest.outcomesA.begin(),
            impl_->abTest.outcomesA.end(), 0.0,
            [](double sum, const auto& o) {
                return sum + o.latency.count();
            }) / impl_->abTest.outcomesA.size();

        // Calculate metrics for strategy B
        double successRateB = std::count_if(impl_->abTest.outcomesB.begin(),
            impl_->abTest.outcomesB.end(),
            [](const auto& o) { return o.success; }) / 
            static_cast<double>(impl_->abTest.outcomesB.size());

        double avgLatencyB = std::accumulate(impl_->abTest.outcomesB.begin(),
            impl_->abTest.outcomesB.end(), 0.0,
            [](double sum, const auto& o) {
                return sum + o.latency.count();
            }) / impl_->abTest.outcomesB.size();

        // Calculate differences
        result.successRateDiff = successRateA - successRateB;
        result.latencyDiff = avgLatencyA - avgLatencyB;

        // Determine significance
        result.isSignificant = impl_->isSignificantDifference(
            successRateA, successRateB,
            impl_->abTest.outcomesA.size(),
            impl_->abTest.outcomesB.size());

        // Make recommendation
        if (result.isSignificant) {
            if (successRateA > successRateB) {
                result.recommendedStrategy = impl_->abTest.strategyA;
                result.explanation = "Strategy A shows significantly better success rate";
            } else {
                result.recommendedStrategy = impl_->abTest.strategyB;
                result.explanation = "Strategy B shows significantly better success rate";
            }
        } else {
            result.recommendedStrategy = impl_->abTest.strategyA; // Default to A
            result.explanation = "No significant difference between strategies";
        }

        return Result<ABTestResult>::ok(std::move(result));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get A/B test results: " + std::string(e.what()));
        return Result<ABTestResult>::error(
            "Failed to get A/B test results: " + std::string(e.what()));
    }
}

void StrategyAdapter::setAdaptationThresholds(const AdaptationThresholds& thresholds) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->thresholds = thresholds;
}

const AdaptationThresholds& StrategyAdapter::getAdaptationThresholds() const {
    return impl_->thresholds;
}

Result<std::vector<std::string>> StrategyAdapter::getPerformanceInsights() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        auto metricsResult = impl_->feedback->getDetailedMetrics();
        if (!metricsResult.is_ok()) {
            return Result<std::vector<std::string>>::error(
                "Failed to get metrics: " + metricsResult.error());
        }

        return Result<std::vector<std::string>>::ok(
            impl_->generateInsights(metricsResult.value()));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get performance insights: " + std::string(e.what()));
        return Result<std::vector<std::string>>::error(
            "Failed to get performance insights: " + std::string(e.what()));
    }
}

Result<std::map<std::string, double>> StrategyAdapter::getStrategyEffectiveness() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        auto metricsResult = impl_->feedback->getDetailedMetrics();
        if (!metricsResult.is_ok()) {
            return Result<std::map<std::string, double>>::error(
                "Failed to get metrics: " + metricsResult.error());
        }

        const auto& metrics = metricsResult.value();
        std::map<std::string, double> effectiveness;
        
        // Calculate effectiveness scores for different aspects
        effectiveness["success_rate"] = metrics.basic.successRate;
        effectiveness["latency_score"] = 
            std::max(0.0, 1.0 - metrics.latencyStats.mean / impl_->thresholds.maxLatencyMs);
        effectiveness["throughput_score"] = 
            metrics.throughputStats.mean / impl_->thresholds.minThroughputBps;
        effectiveness["error_handling"] = 
            std::max(0.0, 1.0 - metrics.basic.errorRate / impl_->thresholds.maxErrorRate);
        
        return Result<std::map<std::string, double>>::ok(std::move(effectiveness));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get strategy effectiveness: " + std::string(e.what()));
        return Result<std::map<std::string, double>>::error(
            "Failed to get strategy effectiveness: " + std::string(e.what()));
    }
}

Result<bool> StrategyAdapter::shouldAdaptStrategy(
    const DetailedMetrics& currentMetrics) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        if (!impl_->meetsMinimumSamples(currentMetrics)) {
            return Result<bool>::ok(false);
        }

        // Check if any threshold is violated
        bool shouldAdapt = 
            currentMetrics.basic.successRate < impl_->thresholds.minSuccessRate ||
            currentMetrics.latencyStats.mean > impl_->thresholds.maxLatencyMs ||
            currentMetrics.throughputStats.mean < impl_->thresholds.minThroughputBps ||
            currentMetrics.basic.errorRate > impl_->thresholds.maxErrorRate;

        return Result<bool>::ok(shouldAdapt);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to determine adaptation need: " + std::string(e.what()));
        return Result<bool>::error(
            "Failed to determine adaptation need: " + std::string(e.what()));
    }
}

Result<StrategyConfig> StrategyAdapter::getOptimalConfig(
    const DetailedMetrics& metrics) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        if (!impl_->meetsMinimumSamples(metrics)) {
            return Result<StrategyConfig>::error("Insufficient samples for optimization");
        }

        return Result<StrategyConfig>::ok(impl_->optimizeConfig(metrics));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to get optimal config: " + std::string(e.what()));
        return Result<StrategyConfig>::error(
            "Failed to get optimal config: " + std::string(e.what()));
    }
}

} // namespace xenocomm 
#ifndef XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_ALIGNMENT_METRICS_HPP
#define XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_ALIGNMENT_METRICS_HPP

#include <memory>
#include <chrono>
#include <vector>
#include <string>
#include <set>
#include <mutex>
#include "metric_types.hpp"

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

struct MetricsConfig; // Forward declaration

// Forward declarations for negotiation integration
namespace xenocomm { namespace core {
    struct NegotiableParams;
    enum class NegotiationState;
}}

class AlignmentMetrics {
public:
    AlignmentMetrics(const MetricsConfig& config);
    // Core metrics collection
    void recordAlignmentAttempt(const AlignmentContext& context, const AlignmentResult& result, const AlignmentMetadata& metadata);
    void recordStrategyExecution(const std::string& strategyId, const ExecutionStats& stats);
    // Metric queries
    double getAlignmentSuccessRate(const TimeRange& range = {}) const;
    std::chrono::milliseconds getAverageConvergenceTime(const TimeRange& range = {}) const;
    double getStrategyEffectiveness(const std::string& strategyId, const TimeRange& range = {}) const;
    // Analysis
    AlignmentTrends analyzeTrends(const TimeRange& range = {}) const;
    StrategyComparison compareStrategies(const std::vector<std::string>& strategyIds) const;
    // Integration
    void syncWithFeedbackLoop(std::shared_ptr<class FeedbackLoop> feedbackLoop);
    // Record a negotiation event for metrics
    void recordNegotiationEvent(const std::string& sessionId,
                               const xenocomm::core::NegotiableParams& params,
                               xenocomm::core::NegotiationState state,
                               std::chrono::milliseconds duration,
                               bool success);
private:
    MetricsConfig config_;
    std::unique_ptr<class MetricStorage> storage_;
    std::shared_ptr<class FeedbackLoop> feedbackLoop_;

    // In-memory metric storage for fast access
    std::vector<MetricData> inMemoryMetrics_;
    // Counters for quick success rate calculation
    size_t totalAlignmentAttempts_ = 0;
    size_t successfulAlignments_ = 0;
    // Mutex for thread safety
    mutable std::mutex metricsMutex_;
    // Registered metric categories
    std::set<std::string> registeredCategories_;
    // Background aggregation thread (future implementation)
    // std::thread backgroundThread_;
    // bool stopBackgroundThread_ = false;

    void persistMetrics(const class MetricData& data);
    void updateFeedbackLoop(const class MetricData& data);
    // Helper for registering metric categories
    void registerMetricCategory(const std::string& category);
    // Helper for generating unique metric IDs
    std::string generateMetricId();
    // Helper for sampling
    bool shouldSampleMetric(double samplingRate);
    // Helper for outcome to string
    std::string outcomeToString(AlignmentOutcome outcome);
};

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm

#endif // XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_ALIGNMENT_METRICS_HPP

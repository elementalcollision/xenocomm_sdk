#include "xenocomm/extensions/common_ground/metrics/alignment_metrics.hpp"
#include "xenocomm/extensions/common_ground/metrics/metric_storage.hpp"
#include <set>
#include <mutex>
#include <string>
#include <atomic>
#include <iostream>
#include "xenocomm/core/negotiation_protocol.h"

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

AlignmentMetrics::AlignmentMetrics(const MetricsConfig& config)
    : config_(config),
      storage_(std::make_unique<MetricStorage>()),
      feedbackLoop_(nullptr),
      totalAlignmentAttempts_(0),
      successfulAlignments_(0) {
    // Register default metric categories
    registerMetricCategory("alignment.success_rate");
    registerMetricCategory("alignment.convergence_time");
    registerMetricCategory("alignment.resource_usage");
    registerMetricCategory("strategy.execution_time");
    registerMetricCategory("strategy.effectiveness");
}

void AlignmentMetrics::registerMetricCategory(const std::string& category) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    registeredCategories_.insert(category);
}

std::string AlignmentMetrics::generateMetricId() {
    static std::atomic<uint64_t> counter{0};
    return "metric_" + std::to_string(counter++);
}

void AlignmentMetrics::recordAlignmentAttempt(
    const AlignmentContext& context,
    const AlignmentResult& result,
    const AlignmentMetadata& metadata) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    totalAlignmentAttempts_++;
    if (result.outcome == AlignmentOutcome::Success) {
        successfulAlignments_++;
    }

    // Create MetricData for success rate
    MetricData successMetric;
    successMetric.metricId = generateMetricId();
    successMetric.category = "alignment.success_rate";
    successMetric.timestamp = metadata.timestamp;
    successMetric.value = (result.outcome == AlignmentOutcome::Success) ? 1.0 : 0.0;
    successMetric.sessionId = metadata.sessionId;
    successMetric.labels = {
        {"agent_id", context.agentId},
        {"target_id", context.targetId},
        {"domain", context.domainContext},
        {"outcome", outcomeToString(result.outcome)}
    };

    // Create MetricData for convergence time
    MetricData timeMetric;
    timeMetric.metricId = generateMetricId();
    timeMetric.category = "alignment.convergence_time";
    timeMetric.timestamp = metadata.timestamp;
    timeMetric.value = static_cast<double>(result.convergenceTime.count());
    timeMetric.sessionId = metadata.sessionId;
    timeMetric.labels = successMetric.labels;

    // Store metrics in memory
    inMemoryMetrics_.push_back(successMetric);
    inMemoryMetrics_.push_back(timeMetric);

    // Store metrics in persistent storage
    persistMetrics(successMetric);
    persistMetrics(timeMetric);

    // Update feedback loop if available
    if (feedbackLoop_) {
        updateFeedbackLoop(successMetric);
        updateFeedbackLoop(timeMetric);
    }
}

void AlignmentMetrics::recordStrategyExecution(const std::string& strategyId, const ExecutionStats& stats) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    MetricData execMetric;
    execMetric.metricId = generateMetricId();
    execMetric.category = "strategy.execution_time";
    execMetric.timestamp = std::chrono::system_clock::now();
    execMetric.value = static_cast<double>(stats.executionTime.count());
    execMetric.labels = {
        {"strategy_id", strategyId},
        {"success", stats.successful ? "true" : "false"}
    };
    // Store in memory
    inMemoryMetrics_.push_back(execMetric);
    persistMetrics(execMetric);
    if (feedbackLoop_) {
        updateFeedbackLoop(execMetric);
    }
}

double AlignmentMetrics::getAlignmentSuccessRate(const TimeRange& range) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    if (inMemoryMetrics_.empty()) return 0.0;
    size_t total = 0;
    size_t successful = 0;
    for (const auto& metric : inMemoryMetrics_) {
        if (metric.category == "alignment.success_rate" &&
            (!range.start || metric.timestamp >= *range.start) &&
            (!range.end || metric.timestamp <= *range.end)) {
            total++;
            if (metric.value > 0.5) successful++;
        }
    }
    return total > 0 ? static_cast<double>(successful) / total : 0.0;
}

std::chrono::milliseconds AlignmentMetrics::getAverageConvergenceTime(const TimeRange& range) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    size_t count = 0;
    double totalTime = 0.0;
    for (const auto& metric : inMemoryMetrics_) {
        if (metric.category == "alignment.convergence_time" &&
            (!range.start || metric.timestamp >= *range.start) &&
            (!range.end || metric.timestamp <= *range.end)) {
            totalTime += metric.value;
            count++;
        }
    }
    return count > 0 ? std::chrono::milliseconds(static_cast<int64_t>(totalTime / count)) : std::chrono::milliseconds(0);
}

double AlignmentMetrics::getStrategyEffectiveness(const std::string& strategyId, const TimeRange& range) const {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    size_t total = 0;
    size_t successful = 0;
    for (const auto& metric : inMemoryMetrics_) {
        if (metric.category == "strategy.execution_time" &&
            metric.labels.count("strategy_id") &&
            metric.labels.at("strategy_id") == strategyId &&
            (!range.start || metric.timestamp >= *range.start) &&
            (!range.end || metric.timestamp <= *range.end)) {
            total++;
            if (metric.labels.count("success") && metric.labels.at("success") == "true") {
                successful++;
            }
        }
    }
    return total > 0 ? static_cast<double>(successful) / total : 0.0;
}

AlignmentTrends AlignmentMetrics::analyzeTrends(const TimeRange& range) const {
    // Stub: To be implemented with more advanced analysis
    return AlignmentTrends{};
}

StrategyComparison AlignmentMetrics::compareStrategies(const std::vector<std::string>& strategyIds) const {
    // Stub: To be implemented with more advanced comparison
    return StrategyComparison{};
}

void AlignmentMetrics::syncWithFeedbackLoop(std::shared_ptr<class FeedbackLoop> feedbackLoop) {
    feedbackLoop_ = feedbackLoop;
    // TODO: Implement synchronization logic if needed
}

void AlignmentMetrics::updateFeedbackLoop(const MetricData& data) {
    if (feedbackLoop_) {
        auto result = feedbackLoop_->recordMetric(data.category, data.value);
        if (!result) {
            std::cerr << "[AlignmentMetrics] Failed to update FeedbackLoop for metric '"
                      << data.category << "' with value " << data.value << std::endl;
        }
        // TODO: Use reportOutcome or addCommunicationResult for richer data if available
    }
}

void AlignmentMetrics::persistMetrics(const MetricData& data) {
    if (storage_) {
        storage_->saveMetric(data);
    }
}

std::string AlignmentMetrics::outcomeToString(AlignmentOutcome outcome) {
    switch (outcome) {
        case AlignmentOutcome::Success: return "success";
        case AlignmentOutcome::PartialSuccess: return "partial_success";
        case AlignmentOutcome::Failure: return "failure";
        case AlignmentOutcome::Timeout: return "timeout";
        case AlignmentOutcome::Error: return "error";
        default: return "unknown";
    }
}

void AlignmentMetrics::recordNegotiationEvent(const std::string& sessionId,
                                             const xenocomm::core::NegotiableParams& params,
                                             xenocomm::core::NegotiationState state,
                                             std::chrono::milliseconds duration,
                                             bool success) {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    // Metric: negotiation outcome (success/failure)
    MetricData outcomeMetric;
    outcomeMetric.metricId = generateMetricId();
    outcomeMetric.category = "negotiation.outcome";
    outcomeMetric.timestamp = std::chrono::system_clock::now();
    outcomeMetric.value = success ? 1.0 : 0.0;
    outcomeMetric.sessionId = sessionId;
    outcomeMetric.labels = {
        {"state", std::to_string(static_cast<int>(state))},
        {"protocol_version", params.protocolVersion},
        {"security_version", params.securityVersion}
    };

    // Metric: negotiation duration
    MetricData durationMetric;
    durationMetric.metricId = generateMetricId();
    durationMetric.category = "negotiation.duration_ms";
    durationMetric.timestamp = outcomeMetric.timestamp;
    durationMetric.value = static_cast<double>(duration.count());
    durationMetric.sessionId = sessionId;
    durationMetric.labels = outcomeMetric.labels;

    // Optionally: add more parameter details as labels
    durationMetric.labels["data_format"] = std::to_string(static_cast<int>(params.dataFormat));
    durationMetric.labels["encryption_algorithm"] = std::to_string(static_cast<int>(params.encryptionAlgorithm));
    durationMetric.labels["compression_algorithm"] = std::to_string(static_cast<int>(params.compressionAlgorithm));

    // Store metrics in memory
    inMemoryMetrics_.push_back(outcomeMetric);
    inMemoryMetrics_.push_back(durationMetric);

    // Store metrics in persistent storage
    persistMetrics(outcomeMetric);
    persistMetrics(durationMetric);

    // Update feedback loop if available
    if (feedbackLoop_) {
        updateFeedbackLoop(outcomeMetric);
        updateFeedbackLoop(durationMetric);
    }
}

// TODO: Implement other methods

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm

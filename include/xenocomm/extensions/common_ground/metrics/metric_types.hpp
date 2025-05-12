#ifndef XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_TYPES_HPP
#define XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_TYPES_HPP

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <any>
#include <chrono>

namespace xenocomm {
namespace extensions {
namespace common_ground {
namespace metrics {

struct MetricsConfig {
    bool enablePersistence = true;
    std::string storageLocation = "./metrics_data";
    std::chrono::seconds aggregationInterval = std::chrono::seconds(300); // 5 minutes
    size_t maxInMemoryEntries = 10000;
    bool enableRealTimeAnalysis = false;
    double samplingRate = 1.0; // 1.0 = 100% of events
};

struct TimeRange {
    std::optional<std::chrono::system_clock::time_point> start;
    std::optional<std::chrono::system_clock::time_point> end;
};

struct AlignmentMetadata {
    std::string sessionId;
    std::chrono::system_clock::time_point timestamp;
    std::vector<std::string> appliedStrategies;
    std::optional<std::string> negotiationId;
    std::map<std::string, std::any> contextParameters;
};

struct ExecutionStats {
    std::chrono::milliseconds executionTime;
    std::size_t memoryUsage;
    int32_t cpuUtilization;
    bool successful;
    std::optional<std::string> errorMessage;
    std::map<std::string, double> customMetrics;
};

struct AlignmentTrends {
    struct TimeSeriesPoint {
        std::chrono::system_clock::time_point timestamp;
        double value;
    };
    std::vector<TimeSeriesPoint> successRate;
    std::vector<TimeSeriesPoint> convergenceTime;
    std::vector<TimeSeriesPoint> resourceUtilization;
    std::map<std::string, std::vector<TimeSeriesPoint>> strategyPerformance;
};

struct StrategyComparison {
    struct StrategyStats {
        double successRate;
        std::chrono::milliseconds averageExecutionTime;
        double resourceEfficiency;
        std::vector<std::string> commonFailurePatterns;
    };
    std::map<std::string, StrategyStats> strategyStats;
    std::vector<std::pair<std::string, std::string>> complementaryPairs;
    std::vector<std::pair<std::string, std::string>> conflictingPairs;
};

struct MetricData {
    std::string metricId;
    std::string category;
    std::chrono::system_clock::time_point timestamp;
    double value;
    std::map<std::string, std::string> labels;
    std::optional<std::string> sessionId;
    // Serialization support can be added here if needed
};

} // namespace metrics
} // namespace common_ground
} // namespace extensions
} // namespace xenocomm

#endif // XENOCOMM_EXTENSIONS_COMMON_GROUND_METRICS_METRIC_TYPES_HPP

#pragma once

#include "xenocomm/utils/result.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xenocomm {

/**
 * @brief Represents a single communication outcome with associated metrics
 */
struct CommunicationOutcome {
    bool success;
    std::chrono::microseconds latency;
    uint32_t bytesTransferred;
    uint32_t retryCount;
    uint32_t errorCount;
    std::string errorType;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @brief Statistical distribution metrics for numeric values
 */
struct DistributionStats {
    double min;
    double max;
    double mean;
    double median;
    double standardDeviation;
    double percentile90;
    double percentile95;
    double percentile99;
};

/**
 * @brief Time series analysis results
 */
struct TimeSeriesAnalysis {
    double trendSlope;              // Rate of change over time
    double seasonalityStrength;     // Measure of periodic patterns (0-1)
    double autocorrelation;         // Correlation with previous values
    bool isStationary;              // Whether the series is stable over time
    std::vector<double> forecast;   // Predicted future values
};

/**
 * @brief Detailed performance metrics for a time window
 */
struct DetailedMetrics {
    // Basic metrics
    MetricsSummary basic;
    
    // Latency distribution
    DistributionStats latencyStats;
    
    // Throughput analysis
    DistributionStats throughputStats;
    double peakThroughput;
    double sustainedThroughput;
    
    // Error analysis
    std::map<std::string, uint32_t> errorTypeFrequency;
    DistributionStats retryStats;
    
    // Time series analysis
    TimeSeriesAnalysis latencyTrend;
    TimeSeriesAnalysis throughputTrend;
    TimeSeriesAnalysis errorRateTrend;
};

/**
 * @brief Aggregated metrics for a specific time window
 */
struct MetricsSummary {
    double successRate;
    double averageLatency;
    double throughputBytesPerSecond;
    double errorRate;
    uint32_t totalTransactions;
    std::chrono::system_clock::time_point windowStart;
    std::chrono::system_clock::time_point windowEnd;
};

/**
 * @brief Configuration options for data persistence
 */
struct PersistenceConfig {
    std::string dataDirectory;           // Directory for storing feedback data
    std::chrono::hours retentionPeriod{720}; // Default 30 days
    uint64_t maxStorageSizeBytes{1073741824}; // Default 1GB
    bool enableCompression{true};        // Whether to compress stored data
    bool enableBackup{true};            // Whether to create backup copies
    uint32_t backupIntervalHours{24};   // How often to create backups
    uint32_t maxBackupCount{7};         // Maximum number of backup files to keep
};

/**
 * @brief Configuration options for the FeedbackLoop
 */
struct FeedbackLoopConfig {
    std::chrono::seconds metricsWindowSize{300}; // Default 5-minute window
    uint32_t maxStoredOutcomes{10000};
    bool enablePersistence{true};
    PersistenceConfig persistence{
        "./feedback_data"  // Default data directory
    };
    bool enableDetailedAnalysis{true};
    uint32_t forecastHorizon{12};   // Number of intervals to forecast
    double outlierThreshold{3.0};   // Standard deviations for outlier detection
};

/**
 * @brief FeedbackLoop class for monitoring and optimizing communication performance
 */
class FeedbackLoop {
public:
    explicit FeedbackLoop(const FeedbackLoopConfig& config = FeedbackLoopConfig{});
    ~FeedbackLoop();

    // Outcome reporting methods
    Result<void> reportOutcome(const CommunicationOutcome& outcome);
    Result<void> recordMetric(const std::string& metricName, double value);
    Result<void> addCommunicationResult(bool success, std::chrono::microseconds latency,
                                      uint32_t bytesTransferred, uint32_t retryCount = 0,
                                      uint32_t errorCount = 0, const std::string& errorType = "");

    // Basic query methods
    Result<MetricsSummary> getCurrentMetrics() const;
    Result<std::vector<CommunicationOutcome>> getRecentOutcomes(uint32_t limit = 100) const;
    Result<double> getMetricValue(const std::string& metricName) const;

    // Advanced statistical analysis
    Result<DetailedMetrics> getDetailedMetrics() const;
    Result<DistributionStats> analyzeLatencyDistribution() const;
    Result<DistributionStats> analyzeThroughputDistribution() const;
    Result<TimeSeriesAnalysis> analyzeLatencyTrend() const;
    Result<std::map<std::string, uint32_t>> getErrorTypeDistribution() const;
    Result<std::vector<CommunicationOutcome>> getOutliers() const;

    // Configuration
    void setConfig(const FeedbackLoopConfig& config);
    const FeedbackLoopConfig& getConfig() const;

    // Persistence methods
    Result<void> saveData() const;
    Result<void> loadData();
    Result<void> createBackup() const;
    Result<void> restoreFromBackup(const std::string& backupFile);
    Result<std::vector<std::string>> listBackups() const;
    Result<void> pruneOldBackups();
    Result<void> compactStorage();
    Result<uint64_t> getStorageSize() const;
    Result<std::chrono::system_clock::time_point> getLastBackupTime() const;
    Result<std::chrono::system_clock::time_point> getOldestDataTime() const;

    // Query methods for historical data
    Result<std::vector<CommunicationOutcome>> getOutcomesByTimeRange(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end) const;
    
    Result<std::vector<std::pair<std::chrono::system_clock::time_point, double>>> 
    getMetricHistory(const std::string& metricName,
                    std::chrono::system_clock::time_point start,
                    std::chrono::system_clock::time_point end) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Prevent copying
    FeedbackLoop(const FeedbackLoop&) = delete;
    FeedbackLoop& operator=(const FeedbackLoop&) = delete;
};

} // namespace xenocomm 
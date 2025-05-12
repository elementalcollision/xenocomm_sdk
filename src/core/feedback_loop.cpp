// #include "xenocomm/utils/logging.h"
#include "xenocomm/core/feedback_loop.h"
#include "feedback_data.pb.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numeric>
#include <zlib.h>

namespace xenocomm {

namespace {
    constexpr uint32_t CURRENT_VERSION = 1;
    constexpr size_t CHUNK_SIZE = 16384;  // 16KB chunks for compression

    // Helper function to compress data using zlib
    std::vector<uint8_t> compressData(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        output.resize(compressBound(input.size()));
        
        uLongf destLen = output.size();
        int result = compress2(output.data(), &destLen,
                             input.data(), input.size(),
                             Z_BEST_COMPRESSION);
        
        if (result != Z_OK) {
            throw std::runtime_error("Data compression failed");
        }
        
        output.resize(destLen);
        return output;
    }

    // Helper function to decompress data using zlib
    std::vector<uint8_t> decompressData(const std::vector<uint8_t>& input, size_t originalSize) {
        std::vector<uint8_t> output;
        output.resize(originalSize);
        
        uLongf destLen = output.size();
        int result = uncompress(output.data(), &destLen,
                              input.data(), input.size());
        
        if (result != Z_OK) {
            throw std::runtime_error("Data decompression failed");
        }
        
        return output;
    }

    // Convert system_clock::time_point to Timestamp proto
    Timestamp timePointToProto(const std::chrono::system_clock::time_point& tp) {
        Timestamp proto;
        auto duration = tp.time_since_epoch();
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - seconds);
        
        proto.set_seconds(seconds.count());
        proto.set_nanos(static_cast<int32_t>(nanos.count()));
        return proto;
    }

    // Convert Timestamp proto to system_clock::time_point
    std::chrono::system_clock::time_point protoToTimePoint(const Timestamp& proto) {
        auto duration = std::chrono::seconds(proto.seconds()) +
                       std::chrono::nanoseconds(proto.nanos());
        return std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(duration));
    }

    // Convert CommunicationOutcome to proto
    CommunicationOutcomeProto outcomeToProto(const CommunicationOutcome& outcome) {
        CommunicationOutcomeProto proto;
        proto.set_success(outcome.success);
        proto.set_latency_micros(outcome.latency.count());
        proto.set_bytes_transferred(outcome.bytesTransferred);
        proto.set_retry_count(outcome.retryCount);
        proto.set_error_count(outcome.errorCount);
        proto.set_error_type(outcome.errorType);
        *proto.mutable_timestamp() = timePointToProto(outcome.timestamp);
        return proto;
    }

    // Convert proto to CommunicationOutcome
    CommunicationOutcome protoToOutcome(const CommunicationOutcomeProto& proto) {
        CommunicationOutcome outcome;
        outcome.success = proto.success();
        outcome.latency = std::chrono::microseconds(proto.latency_micros());
        outcome.bytesTransferred = proto.bytes_transferred();
        outcome.retryCount = proto.retry_count();
        outcome.errorCount = proto.error_count();
        outcome.errorType = proto.error_type();
        outcome.timestamp = protoToTimePoint(proto.timestamp());
        return outcome;
    }
}

struct FeedbackLoop::Impl {
    FeedbackLoopConfig config;
    std::deque<CommunicationOutcome> outcomes;
    std::map<std::string, std::deque<std::pair<std::chrono::system_clock::time_point, double>>> metrics;
    mutable std::mutex mutex;

    // New persistence-related members
    std::chrono::system_clock::time_point lastBackupTime;
    std::string currentDataFile;
    TimeIndex timeIndex;

    void pruneOldData() {
        auto now = std::chrono::system_clock::now();
        auto cutoff = now - config.metricsWindowSize;

        // Prune outcomes
        while (!outcomes.empty() && outcomes.front().timestamp < cutoff) {
            outcomes.pop_front();
        }

        // Prune metrics
        for (auto& [name, values] : metrics) {
            while (!values.empty() && values.front().first < cutoff) {
                values.pop_front();
            }
        }

        // Enforce maximum storage limit
        if (outcomes.size() > config.maxStoredOutcomes) {
            outcomes.erase(outcomes.begin(), 
                         outcomes.begin() + (outcomes.size() - config.maxStoredOutcomes));
        }
    }

    MetricsSummary calculateMetrics(const std::vector<CommunicationOutcome>& windowOutcomes) const {
        MetricsSummary summary{};
        if (windowOutcomes.empty()) {
            return summary;
        }

        summary.windowStart = windowOutcomes.front().timestamp;
        summary.windowEnd = windowOutcomes.back().timestamp;
        summary.totalTransactions = windowOutcomes.size();

        // Calculate metrics
        uint32_t successCount = 0;
        uint32_t errorCount = 0;
        double totalLatency = 0;
        uint64_t totalBytes = 0;

        for (const auto& outcome : windowOutcomes) {
            if (outcome.success) {
                successCount++;
            }
            errorCount += outcome.errorCount;
            totalLatency += outcome.latency.count();
            totalBytes += outcome.bytesTransferred;
        }

        summary.successRate = static_cast<double>(successCount) / windowOutcomes.size();
        summary.averageLatency = totalLatency / windowOutcomes.size();
        
        // Calculate throughput (bytes per second)
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            summary.windowEnd - summary.windowStart).count();
        summary.throughputBytesPerSecond = duration > 0 ? 
            static_cast<double>(totalBytes) / duration : 0;

        summary.errorRate = static_cast<double>(errorCount) / windowOutcomes.size();

        return summary;
    }

    DistributionStats calculateDistributionStats(const std::vector<double>& values) const {
        if (values.empty()) {
            return DistributionStats{};
        }

        std::vector<double> sortedValues = values;
        std::sort(sortedValues.begin(), sortedValues.end());

        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / values.size();

        double sqSum = std::accumulate(values.begin(), values.end(), 0.0,
            [mean](double acc, double val) {
                double diff = val - mean;
                return acc + (diff * diff);
            });

        size_t size = values.size();
        size_t p90Idx = static_cast<size_t>(size * 0.90);
        size_t p95Idx = static_cast<size_t>(size * 0.95);
        size_t p99Idx = static_cast<size_t>(size * 0.99);

        return DistributionStats{
            sortedValues.front(),                    // min
            sortedValues.back(),                     // max
            mean,                                    // mean
            size % 2 == 0 ?                         // median
                (sortedValues[size/2 - 1] + sortedValues[size/2]) / 2 :
                sortedValues[size/2],
            std::sqrt(sqSum / size),                // standardDeviation
            sortedValues[p90Idx],                   // percentile90
            sortedValues[p95Idx],                   // percentile95
            sortedValues[p99Idx]                    // percentile99
        };
    }

    TimeSeriesAnalysis analyzeTimeSeries(const std::vector<std::pair<double, double>>& timeValuePairs) const {
        TimeSeriesAnalysis analysis{};
        if (timeValuePairs.size() < 2) {
            return analysis;
        }

        // Calculate trend (linear regression)
        double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        double n = static_cast<double>(timeValuePairs.size());

        for (const auto& [time, value] : timeValuePairs) {
            sumX += time;
            sumY += value;
            sumXY += time * value;
            sumX2 += time * time;
        }

        analysis.trendSlope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);

        // Calculate autocorrelation (lag-1)
        std::vector<double> values;
        values.reserve(timeValuePairs.size());
        for (const auto& [_, value] : timeValuePairs) {
            values.push_back(value);
        }

        double meanY = sumY / n;
        double numerator = 0, denominator = 0;

        for (size_t i = 1; i < values.size(); ++i) {
            double diff1 = values[i] - meanY;
            double diff2 = values[i-1] - meanY;
            numerator += diff1 * diff2;
            denominator += diff1 * diff1;
        }

        analysis.autocorrelation = denominator != 0 ? numerator / denominator : 0;

        // Determine stationarity (simple test based on trend strength)
        analysis.isStationary = std::abs(analysis.trendSlope) < 0.1;

        // Simple forecast using linear trend
        analysis.forecast.reserve(config.forecastHorizon);
        double lastTime = timeValuePairs.back().first;
        double lastValue = timeValuePairs.back().second;

        for (uint32_t i = 0; i < config.forecastHorizon; ++i) {
            double nextValue = lastValue + analysis.trendSlope;
            analysis.forecast.push_back(nextValue);
            lastValue = nextValue;
        }

        return analysis;
    }

    DetailedMetrics calculateDetailedMetrics(const std::vector<CommunicationOutcome>& windowOutcomes) const {
        DetailedMetrics metrics{};
        if (windowOutcomes.empty()) {
            return metrics;
        }

        // Calculate basic metrics
        metrics.basic = calculateMetrics(windowOutcomes);

        // Extract latency values
        std::vector<double> latencies;
        std::vector<double> throughputs;
        std::vector<double> retries;
        std::map<std::string, uint32_t> errorTypes;
        std::vector<std::pair<double, double>> latencyTimeSeries;
        std::vector<std::pair<double, double>> throughputTimeSeries;

        double baseTime = std::chrono::duration<double>(
            windowOutcomes.front().timestamp.time_since_epoch()).count();

        for (const auto& outcome : windowOutcomes) {
            // Latency analysis
            double latencyMs = outcome.latency.count() / 1000.0;
            latencies.push_back(latencyMs);

            // Throughput analysis
            double timeSinceStart = std::chrono::duration<double>(
                outcome.timestamp.time_since_epoch()).count() - baseTime;
            double throughput = outcome.bytesTransferred / (latencyMs / 1000.0);
            throughputs.push_back(throughput);

            // Error analysis
            if (!outcome.errorType.empty()) {
                errorTypes[outcome.errorType]++;
            }
            retries.push_back(static_cast<double>(outcome.retryCount));

            // Time series data
            latencyTimeSeries.emplace_back(timeSinceStart, latencyMs);
            throughputTimeSeries.emplace_back(timeSinceStart, throughput);
        }

        // Calculate distribution statistics
        metrics.latencyStats = calculateDistributionStats(latencies);
        metrics.throughputStats = calculateDistributionStats(throughputs);
        metrics.retryStats = calculateDistributionStats(retries);
        metrics.errorTypeFrequency = std::move(errorTypes);

        // Calculate peak and sustained throughput
        metrics.peakThroughput = metrics.throughputStats.max;
        metrics.sustainedThroughput = metrics.throughputStats.percentile90;

        // Time series analysis
        metrics.latencyTrend = analyzeTimeSeries(latencyTimeSeries);
        metrics.throughputTrend = analyzeTimeSeries(throughputTimeSeries);

        return metrics;
    }

    std::vector<CommunicationOutcome> findOutliers(
        const std::vector<CommunicationOutcome>& windowOutcomes) const {
        if (windowOutcomes.empty()) {
            return {};
        }

        std::vector<double> latencies;
        latencies.reserve(windowOutcomes.size());
        for (const auto& outcome : windowOutcomes) {
            latencies.push_back(outcome.latency.count());
        }

        // Calculate mean and standard deviation
        double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);
        double mean = sum / latencies.size();
        double sqSum = std::accumulate(latencies.begin(), latencies.end(), 0.0,
            [mean](double acc, double val) {
                double diff = val - mean;
                return acc + (diff * diff);
            });
        double stdDev = std::sqrt(sqSum / latencies.size());

        // Find outliers
        std::vector<CommunicationOutcome> outliers;
        for (size_t i = 0; i < windowOutcomes.size(); ++i) {
            double zScore = std::abs(latencies[i] - mean) / stdDev;
            if (zScore > config.outlierThreshold) {
                outliers.push_back(windowOutcomes[i]);
            }
        }

        return outliers;
    }

    Result<void> saveDataToFile(const std::string& filename) const {
        try {
            FeedbackData data;
            data.set_version(CURRENT_VERSION);
            *data.mutable_last_update() = timePointToProto(std::chrono::system_clock::now());

            // Save outcomes
            for (const auto& outcome : outcomes) {
                *data.add_outcomes() = outcomeToProto(outcome);
            }

            // Save metrics
            for (const auto& [name, values] : metrics) {
                auto* series = data.add_metrics();
                series->set_metric_name(name);
                for (const auto& [time, value] : values) {
                    auto* point = series->add_data_points();
                    *point->mutable_timestamp() = timePointToProto(time);
                    point->set_value(value);
                }
            }

            // Serialize to binary
            std::vector<uint8_t> serialized(data.ByteSizeLong());
            if (!data.SerializeToArray(serialized.data(), serialized.size())) {
                return Result<void>(std::string("Failed to serialize feedback data"));
            }

            // Compress if enabled
            if (config.persistence.enableCompression) {
                serialized = compressData(serialized);
            }

            // Create directory if it doesn't exist
            std::filesystem::create_directories(
                std::filesystem::path(filename).parent_path());

            // Write to file
            std::ofstream file(filename, std::ios::binary);
            if (!file) {
                return Result<void>(std::string("Failed to open file for writing: " + filename));
            }

            file.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());
            
            return Result<void>();
        } catch (const std::exception& e) {
            return Result<void>(std::string("Failed to save data: " + std::string(e.what())));
        }
    }

    Result<void> loadDataFromFile(const std::string& filename) {
        try {
            // Read file
            std::ifstream file(filename, std::ios::binary | std::ios::ate);
            if (!file) {
                return Result<void>(std::string("Failed to open file for reading: " + filename));
            }

            auto size = file.tellg();
            file.seekg(0);
            std::vector<uint8_t> data(size);
            file.read(reinterpret_cast<char*>(data.data()), size);

            // Decompress if needed (check magic bytes for zlib compression)
            if (config.persistence.enableCompression && size >= 2 &&
                (data[0] == 0x78 && (data[1] == 0x01 || data[1] == 0x9C || data[1] == 0xDA))) {
                // Estimate original size (this is a heuristic)
                size_t estimatedSize = size * 4;  // Assume 4:1 compression ratio
                data = decompressData(data, estimatedSize);
            }

            // Parse protobuf
            FeedbackData feedbackData;
            if (!feedbackData.ParseFromArray(data.data(), data.size())) {
                return Result<void>(std::string("Failed to parse feedback data"));
            }

            // Version check
            if (feedbackData.version() > CURRENT_VERSION) {
                return Result<void>(std::string("Unsupported data version"));
            }

            // Clear existing data
            outcomes.clear();
            metrics.clear();

            // Load outcomes
            for (const auto& outcomeProto : feedbackData.outcomes()) {
                outcomes.push_back(protoToOutcome(outcomeProto));
            }

            // Load metrics
            for (const auto& seriesProto : feedbackData.metrics()) {
                auto& series = metrics[seriesProto.metric_name()];
                for (const auto& pointProto : seriesProto.data_points()) {
                    series.emplace_back(
                        protoToTimePoint(pointProto.timestamp()),
                        pointProto.value()
                    );
                }
            }

            return Result<void>();
        } catch (const std::exception& e) {
            return Result<void>(std::string("Failed to load data: " + std::string(e.what())));
        }
    }

    Result<void> updateTimeIndex() {
        try {
            timeIndex.Clear();
            
            // Sort outcomes by timestamp
            std::vector<CommunicationOutcome> sortedOutcomes(outcomes.begin(), outcomes.end());
            std::sort(sortedOutcomes.begin(), sortedOutcomes.end(),
                     [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });

            // Create index entries
            for (size_t i = 0; i < sortedOutcomes.size(); i += CHUNK_SIZE) {
                *timeIndex.add_timestamps() = timePointToProto(sortedOutcomes[i].timestamp);
                timeIndex.add_file_offsets(i);
            }

            return Result<void>();
        } catch (const std::exception& e) {
            return Result<void>(std::string("Failed to update time index: " + std::string(e.what())));
        }
    }

    Result<void> cleanupOldData() {
        try {
            auto now = std::chrono::system_clock::now();
            auto cutoff = now - config.persistence.retentionPeriod;

            // Remove old outcomes
            outcomes.erase(
                std::remove_if(outcomes.begin(), outcomes.end(),
                    [cutoff](const auto& outcome) {
                        return outcome.timestamp < cutoff;
                    }),
                outcomes.end()
            );

            // Remove old metrics
            for (auto& [name, values] : metrics) {
                values.erase(
                    std::remove_if(values.begin(), values.end(),
                        [cutoff](const auto& point) {
                            return point.first < cutoff;
                        }),
                    values.end()
                );
            }

            // Clean up old backup files
            if (config.persistence.enableBackup) {
                auto backupDir = std::filesystem::path(config.persistence.dataDirectory) / "backups";
                if (std::filesystem::exists(backupDir)) {
                    std::vector<std::filesystem::path> backupFiles;
                    for (const auto& entry : std::filesystem::directory_iterator(backupDir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".backup") {
                            backupFiles.push_back(entry.path());
                        }
                    }

                    // Sort by modification time, newest first
                    std::sort(backupFiles.begin(), backupFiles.end(),
                        [](const auto& a, const auto& b) {
                            return std::filesystem::last_write_time(a) >
                                   std::filesystem::last_write_time(b);
                        });

                    // Remove excess backups
                    while (backupFiles.size() > config.persistence.maxBackupCount) {
                        std::filesystem::remove(backupFiles.back());
                        backupFiles.pop_back();
                    }
                }
            }

            return Result<void>();
        } catch (const std::exception& e) {
            return Result<void>(std::string("Failed to clean up old data: " + std::string(e.what())));
        }
    }
};

FeedbackLoop::FeedbackLoop(const FeedbackLoopConfig& config)
    : impl_(std::make_unique<Impl>()) {
    impl_->config = config;

    if (config.enablePersistence) {
        std::filesystem::create_directories(config.persistence.dataDirectory);
    }

    // LOG_INFO("FeedbackLoop initialized with window size: " + 
    //          std::to_string(config.metricsWindowSize.count()) + "s");
}

FeedbackLoop::~FeedbackLoop() = default;

Result<void> FeedbackLoop::reportOutcome(const CommunicationOutcome& outcome) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        impl_->outcomes.push_back(outcome);
        impl_->pruneOldData();
        return Result<void>();
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to report outcome: " + std::string(e.what()));
        return Result<void>(std::string("Failed to report outcome: " + std::string(e.what())));
    }
}

Result<void> FeedbackLoop::recordMetric(const std::string& metricName, double value) {
    if (metricName.empty()) {
        return Result<void>(std::string("Metric name cannot be empty"));
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        impl_->metrics[metricName].push_back(
            {std::chrono::system_clock::now(), value});
        impl_->pruneOldData();
        return Result<void>();
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to record metric: " + std::string(e.what()));
        return Result<void>(std::string("Failed to record metric: " + std::string(e.what())));
    }
}

Result<void> FeedbackLoop::addCommunicationResult(
    bool success, std::chrono::microseconds latency,
    uint32_t bytesTransferred, uint32_t retryCount,
    uint32_t errorCount, const std::string& errorType) {
    
    CommunicationOutcome outcome{
        success,
        latency,
        bytesTransferred,
        retryCount,
        errorCount,
        errorType,
        std::chrono::system_clock::now()
    };

    return reportOutcome(outcome);
}

Result<MetricsSummary> FeedbackLoop::getCurrentMetrics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        std::vector<CommunicationOutcome> windowOutcomes(
            impl_->outcomes.begin(), impl_->outcomes.end());
        return Result<MetricsSummary>(impl_->calculateMetrics(windowOutcomes));
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to get current metrics: " + std::string(e.what()));
        return Result<MetricsSummary>(std::string("Failed to get current metrics: " + std::string(e.what())));
    }
}

Result<std::vector<CommunicationOutcome>> FeedbackLoop::getRecentOutcomes(
    uint32_t limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        std::vector<CommunicationOutcome> recent;
        auto start = impl_->outcomes.size() > limit ?
            impl_->outcomes.end() - limit : impl_->outcomes.begin();
        recent.assign(start, impl_->outcomes.end());
        return Result<std::vector<CommunicationOutcome>>(std::move(recent));
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to get recent outcomes: " + std::string(e.what()));
        return Result<std::vector<CommunicationOutcome>>(std::string("Failed to get recent outcomes: " + std::string(e.what())));
    }
}

Result<double> FeedbackLoop::getMetricValue(const std::string& metricName) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        auto it = impl_->metrics.find(metricName);
        if (it == impl_->metrics.end() || it->second.empty()) {
            return Result<double>(std::string("Metric not found or no values available"));
        }
        return Result<double>(it->second.back().second);
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to get metric value: " + std::string(e.what()));
        return Result<double>(std::string("Failed to get metric value: " + std::string(e.what())));
    }
}

void FeedbackLoop::setConfig(const FeedbackLoopConfig& config) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->config = config;
    impl_->pruneOldData();
}

const FeedbackLoopConfig& FeedbackLoop::getConfig() const {
    return impl_->config;
}

Result<DetailedMetrics> FeedbackLoop::getDetailedMetrics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        if (!impl_->config.enableDetailedAnalysis) {
            return Result<DetailedMetrics>(std::string("Detailed analysis is disabled in configuration"));
        }

        std::vector<CommunicationOutcome> windowOutcomes(
            impl_->outcomes.begin(), impl_->outcomes.end());
        return Result<DetailedMetrics>(impl_->calculateDetailedMetrics(windowOutcomes));
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to calculate detailed metrics: " + std::string(e.what()));
        return Result<DetailedMetrics>(std::string("Failed to calculate detailed metrics: " + std::string(e.what())));
    }
}

Result<DistributionStats> FeedbackLoop::analyzeLatencyDistribution() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        std::vector<double> latencies;
        latencies.reserve(impl_->outcomes.size());
        for (const auto& outcome : impl_->outcomes) {
            latencies.push_back(outcome.latency.count());
        }
        return Result<DistributionStats>(impl_->calculateDistributionStats(latencies));
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to analyze latency distribution: " + std::string(e.what()));
        return Result<DistributionStats>(std::string("Failed to analyze latency distribution: " + std::string(e.what())));
    }
}

Result<DistributionStats> FeedbackLoop::analyzeThroughputDistribution() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        std::vector<double> throughputs;
        throughputs.reserve(impl_->outcomes.size());
        for (const auto& outcome : impl_->outcomes) {
            double seconds = outcome.latency.count() / 1e6;
            if (seconds > 0) {
                throughputs.push_back(outcome.bytesTransferred / seconds);
            }
        }
        return Result<DistributionStats>(impl_->calculateDistributionStats(throughputs));
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to analyze throughput distribution: " + std::string(e.what()));
        return Result<DistributionStats>(std::string("Failed to analyze throughput distribution: " + std::string(e.what())));
    }
}

Result<TimeSeriesAnalysis> FeedbackLoop::analyzeLatencyTrend() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        std::vector<std::pair<double, double>> timeValuePairs;
        if (impl_->outcomes.empty()) {
            return Result<TimeSeriesAnalysis>(TimeSeriesAnalysis{});
        }

        double baseTime = std::chrono::duration<double>(
            impl_->outcomes.front().timestamp.time_since_epoch()).count();

        for (const auto& outcome : impl_->outcomes) {
            double time = std::chrono::duration<double>(
                outcome.timestamp.time_since_epoch()).count() - baseTime;
            timeValuePairs.emplace_back(time, outcome.latency.count());
        }

        return Result<TimeSeriesAnalysis>(impl_->analyzeTimeSeries(timeValuePairs));
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to analyze latency trend: " + std::string(e.what()));
        return Result<TimeSeriesAnalysis>(std::string("Failed to analyze latency trend: " + std::string(e.what())));
    }
}

Result<std::map<std::string, uint32_t>> FeedbackLoop::getErrorTypeDistribution() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        std::map<std::string, uint32_t> distribution;
        for (const auto& outcome : impl_->outcomes) {
            if (!outcome.errorType.empty()) {
                distribution[outcome.errorType]++;
            }
        }
        return Result<std::map<std::string, uint32_t>>(distribution);
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to get error type distribution: " + std::string(e.what()));
        return Result<std::map<std::string, uint32_t>>(std::string("Failed to get error type distribution: " + std::string(e.what())));
    }
}

Result<std::vector<CommunicationOutcome>> FeedbackLoop::getOutliers() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    try {
        std::vector<CommunicationOutcome> windowOutcomes(
            impl_->outcomes.begin(), impl_->outcomes.end());
        return Result<std::vector<CommunicationOutcome>>(
            impl_->findOutliers(windowOutcomes));
    } catch (const std::exception& e) {
        // LOG_ERROR("Failed to find outliers: " + std::string(e.what()));
        return Result<std::vector<CommunicationOutcome>>(std::string("Failed to find outliers: " + std::string(e.what())));
    }
}

Result<void> FeedbackLoop::saveData() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto dataFile = std::filesystem::path(impl_->config.persistence.dataDirectory) / "feedback_data.pb";
    return impl_->saveDataToFile(dataFile.string());
}

Result<void> FeedbackLoop::loadData() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto dataFile = std::filesystem::path(impl_->config.persistence.dataDirectory) / "feedback_data.pb";
    return impl_->loadDataFromFile(dataFile.string());
}

Result<void> FeedbackLoop::createBackup() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        
        auto backupDir = std::filesystem::path(impl_->config.persistence.dataDirectory) / "backups";
        std::filesystem::create_directories(backupDir);
        
        auto backupFile = backupDir / ("feedback_data_" + std::to_string(timestamp) + ".backup");
        auto result = impl_->saveDataToFile(backupFile.string());
        
        if (result.has_value()) {
            impl_->lastBackupTime = now;
        }
        
        return result;
    } catch (const std::exception& e) {
        return Result<void>(std::string("Failed to create backup: " + std::string(e.what())));
    }
}

Result<void> FeedbackLoop::restoreFromBackup(const std::string& backupFile) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->loadDataFromFile(backupFile);
}

Result<std::vector<std::string>> FeedbackLoop::listBackups() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        std::vector<std::string> backups;
        auto backupDir = std::filesystem::path(impl_->config.persistence.dataDirectory) / "backups";
        
        if (std::filesystem::exists(backupDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(backupDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".backup") {
                    backups.push_back(entry.path().string());
                }
            }
        }
        
        return Result<std::vector<std::string>>(std::move(backups));
    } catch (const std::exception& e) {
        return Result<std::vector<std::string>>(std::string("Failed to list backups: " + std::string(e.what())));
    }
}

Result<void> FeedbackLoop::pruneOldBackups() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->cleanupOldData();
}

Result<void> FeedbackLoop::compactStorage() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        // Update time index
        auto result = impl_->updateTimeIndex();
        if (!result.has_value()) {
            return result;
        }

        // Save current data with compression
        bool wasCompressed = impl_->config.persistence.enableCompression;
        impl_->config.persistence.enableCompression = true;
        result = saveData();
        impl_->config.persistence.enableCompression = wasCompressed;

        return result;
    } catch (const std::exception& e) {
        return Result<void>(std::string("Failed to compact storage: " + std::string(e.what())));
    }
}

Result<uint64_t> FeedbackLoop::getStorageSize() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        uint64_t totalSize = 0;
        auto dataDir = std::filesystem::path(impl_->config.persistence.dataDirectory);
        
        if (std::filesystem::exists(dataDir)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dataDir)) {
                if (entry.is_regular_file()) {
                    totalSize += std::filesystem::file_size(entry.path());
                }
            }
        }
        
        return Result<uint64_t>(totalSize);
    } catch (const std::exception& e) {
        return Result<uint64_t>(std::string("Failed to get storage size: " + std::string(e.what())));
    }
}

Result<std::chrono::system_clock::time_point> FeedbackLoop::getLastBackupTime() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return Result<std::chrono::system_clock::time_point>(impl_->lastBackupTime);
}

Result<std::chrono::system_clock::time_point> FeedbackLoop::getOldestDataTime() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        if (impl_->outcomes.empty()) {
            return Result<std::chrono::system_clock::time_point>(std::string("No data available"));
        }
        
        auto oldestTime = impl_->outcomes.front().timestamp;
        for (const auto& [_, values] : impl_->metrics) {
            if (!values.empty() && values.front().first < oldestTime) {
                oldestTime = values.front().first;
            }
        }
        
        return Result<std::chrono::system_clock::time_point>(oldestTime);
    } catch (const std::exception& e) {
        return Result<std::chrono::system_clock::time_point>(std::string("Failed to get oldest data time: " + std::string(e.what())));
    }
}

Result<std::vector<CommunicationOutcome>> FeedbackLoop::getOutcomesByTimeRange(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        std::vector<CommunicationOutcome> results;
        for (const auto& outcome : impl_->outcomes) {
            if (outcome.timestamp >= start && outcome.timestamp <= end) {
                results.push_back(outcome);
            }
        }
        return Result<std::vector<CommunicationOutcome>>(std::move(results));
    } catch (const std::exception& e) {
        return Result<std::vector<CommunicationOutcome>>(std::string("Failed to get outcomes by time range: " + std::string(e.what())));
    }
}

Result<std::vector<std::pair<std::chrono::system_clock::time_point, double>>>
FeedbackLoop::getMetricHistory(
    const std::string& metricName,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        auto it = impl_->metrics.find(metricName);
        if (it == impl_->metrics.end()) {
            return Result<std::vector<std::pair<std::chrono::system_clock::time_point, double>>>(std::string("Metric not found: " + metricName));
        }

        std::vector<std::pair<std::chrono::system_clock::time_point, double>> results;
        for (const auto& [time, value] : it->second) {
            if (time >= start && time <= end) {
                results.emplace_back(time, value);
            }
        }
        return Result<std::vector<std::pair<std::chrono::system_clock::time_point, double>>>(std::move(results));
    } catch (const std::exception& e) {
        return Result<std::vector<std::pair<std::chrono::system_clock::time_point, double>>>(std::string("Failed to get metric history: " + std::string(e.what())));
    }
}
} // namespace xenocomm 
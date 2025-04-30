#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include "xenocomm/core/feedback_loop.h"

namespace py = pybind11;
using namespace xenocomm;

void init_feedback_loop(py::module& m) {
    // Bind CommunicationOutcome struct
    py::class_<CommunicationOutcome>(m, "CommunicationOutcome")
        .def(py::init<>())
        .def_readwrite("success", &CommunicationOutcome::success)
        .def_readwrite("latency", &CommunicationOutcome::latency)
        .def_readwrite("bytes_transferred", &CommunicationOutcome::bytesTransferred)
        .def_readwrite("retry_count", &CommunicationOutcome::retryCount)
        .def_readwrite("error_count", &CommunicationOutcome::errorCount)
        .def_readwrite("error_type", &CommunicationOutcome::errorType)
        .def_readwrite("timestamp", &CommunicationOutcome::timestamp);

    // Bind DistributionStats struct
    py::class_<DistributionStats>(m, "DistributionStats")
        .def(py::init<>())
        .def_readwrite("min", &DistributionStats::min)
        .def_readwrite("max", &DistributionStats::max)
        .def_readwrite("mean", &DistributionStats::mean)
        .def_readwrite("median", &DistributionStats::median)
        .def_readwrite("standard_deviation", &DistributionStats::standardDeviation)
        .def_readwrite("percentile_90", &DistributionStats::percentile90)
        .def_readwrite("percentile_95", &DistributionStats::percentile95)
        .def_readwrite("percentile_99", &DistributionStats::percentile99);

    // Bind TimeSeriesAnalysis struct
    py::class_<TimeSeriesAnalysis>(m, "TimeSeriesAnalysis")
        .def(py::init<>())
        .def_readwrite("trend_slope", &TimeSeriesAnalysis::trendSlope)
        .def_readwrite("seasonality_strength", &TimeSeriesAnalysis::seasonalityStrength)
        .def_readwrite("autocorrelation", &TimeSeriesAnalysis::autocorrelation)
        .def_readwrite("is_stationary", &TimeSeriesAnalysis::isStationary)
        .def_readwrite("forecast", &TimeSeriesAnalysis::forecast);

    // Bind MetricsSummary struct
    py::class_<MetricsSummary>(m, "MetricsSummary")
        .def(py::init<>())
        .def_readwrite("success_rate", &MetricsSummary::successRate)
        .def_readwrite("average_latency", &MetricsSummary::averageLatency)
        .def_readwrite("throughput_bytes_per_second", &MetricsSummary::throughputBytesPerSecond)
        .def_readwrite("error_rate", &MetricsSummary::errorRate)
        .def_readwrite("total_transactions", &MetricsSummary::totalTransactions)
        .def_readwrite("window_start", &MetricsSummary::windowStart)
        .def_readwrite("window_end", &MetricsSummary::windowEnd);

    // Bind DetailedMetrics struct
    py::class_<DetailedMetrics>(m, "DetailedMetrics")
        .def(py::init<>())
        .def_readwrite("basic", &DetailedMetrics::basic)
        .def_readwrite("latency_stats", &DetailedMetrics::latencyStats)
        .def_readwrite("throughput_stats", &DetailedMetrics::throughputStats)
        .def_readwrite("peak_throughput", &DetailedMetrics::peakThroughput)
        .def_readwrite("sustained_throughput", &DetailedMetrics::sustainedThroughput)
        .def_readwrite("error_type_frequency", &DetailedMetrics::errorTypeFrequency)
        .def_readwrite("retry_stats", &DetailedMetrics::retryStats)
        .def_readwrite("latency_trend", &DetailedMetrics::latencyTrend)
        .def_readwrite("throughput_trend", &DetailedMetrics::throughputTrend)
        .def_readwrite("error_rate_trend", &DetailedMetrics::errorRateTrend);

    // Bind PersistenceConfig struct
    py::class_<PersistenceConfig>(m, "PersistenceConfig")
        .def(py::init<>())
        .def_readwrite("data_directory", &PersistenceConfig::dataDirectory)
        .def_readwrite("retention_period", &PersistenceConfig::retentionPeriod)
        .def_readwrite("max_storage_size_bytes", &PersistenceConfig::maxStorageSizeBytes)
        .def_readwrite("enable_compression", &PersistenceConfig::enableCompression)
        .def_readwrite("enable_backup", &PersistenceConfig::enableBackup)
        .def_readwrite("backup_interval_hours", &PersistenceConfig::backupIntervalHours)
        .def_readwrite("max_backup_count", &PersistenceConfig::maxBackupCount);

    // Bind FeedbackLoopConfig struct
    py::class_<FeedbackLoopConfig>(m, "FeedbackLoopConfig")
        .def(py::init<>())
        .def_readwrite("metrics_window_size", &FeedbackLoopConfig::metricsWindowSize)
        .def_readwrite("max_stored_outcomes", &FeedbackLoopConfig::maxStoredOutcomes)
        .def_readwrite("enable_persistence", &FeedbackLoopConfig::enablePersistence)
        .def_readwrite("persistence", &FeedbackLoopConfig::persistence)
        .def_readwrite("enable_detailed_analysis", &FeedbackLoopConfig::enableDetailedAnalysis)
        .def_readwrite("forecast_horizon", &FeedbackLoopConfig::forecastHorizon)
        .def_readwrite("outlier_threshold", &FeedbackLoopConfig::outlierThreshold);

    // Bind FeedbackLoop class
    py::class_<FeedbackLoop>(m, "FeedbackLoop")
        .def(py::init<const FeedbackLoopConfig&>(), py::arg("config") = FeedbackLoopConfig{})
        .def("report_outcome", &FeedbackLoop::reportOutcome)
        .def("record_metric", &FeedbackLoop::recordMetric)
        .def("add_communication_result", &FeedbackLoop::addCommunicationResult,
            py::arg("success"),
            py::arg("latency"),
            py::arg("bytes_transferred"),
            py::arg("retry_count") = 0,
            py::arg("error_count") = 0,
            py::arg("error_type") = "")
        .def("get_current_metrics", &FeedbackLoop::getCurrentMetrics)
        .def("get_recent_outcomes", &FeedbackLoop::getRecentOutcomes,
            py::arg("limit") = 100)
        .def("get_metric_value", &FeedbackLoop::getMetricValue)
        .def("get_detailed_metrics", &FeedbackLoop::getDetailedMetrics)
        .def("analyze_latency_distribution", &FeedbackLoop::analyzeLatencyDistribution)
        .def("analyze_throughput_distribution", &FeedbackLoop::analyzeThroughputDistribution)
        .def("analyze_latency_trend", &FeedbackLoop::analyzeLatencyTrend)
        .def("get_error_type_distribution", &FeedbackLoop::getErrorTypeDistribution)
        .def("get_outliers", &FeedbackLoop::getOutliers)
        .def("set_config", &FeedbackLoop::setConfig)
        .def("get_config", &FeedbackLoop::getConfig)
        .def("save_data", &FeedbackLoop::saveData)
        .def("load_data", &FeedbackLoop::loadData)
        .def("create_backup", &FeedbackLoop::createBackup)
        .def("restore_from_backup", &FeedbackLoop::restoreFromBackup)
        .def("list_backups", &FeedbackLoop::listBackups)
        .def("prune_old_backups", &FeedbackLoop::pruneOldBackups)
        .def("compact_storage", &FeedbackLoop::compactStorage)
        .def("get_storage_size", &FeedbackLoop::getStorageSize)
        .def("get_last_backup_time", &FeedbackLoop::getLastBackupTime)
        .def("get_oldest_data_time", &FeedbackLoop::getOldestDataTime)
        .def("get_outcomes_by_time_range", &FeedbackLoop::getOutcomesByTimeRange)
        .def("get_metric_history", &FeedbackLoop::getMetricHistory);
} 
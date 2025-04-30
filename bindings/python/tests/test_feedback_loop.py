import pytest
import numpy as np
from datetime import datetime, timedelta
from xenocomm import (
    FeedbackLoop, FeedbackLoopConfig, CommunicationOutcome,
    DistributionStats, TimeSeriesAnalysis, MetricsSummary,
    DetailedMetrics, PersistenceConfig
)

def test_feedback_loop_config():
    # Test default configuration
    config = FeedbackLoopConfig()
    assert config.metrics_window_size == timedelta(minutes=5)
    assert config.max_stored_outcomes == 1000
    assert config.enable_persistence is False
    assert config.enable_detailed_analysis is True
    assert config.forecast_horizon == timedelta(hours=1)
    assert config.outlier_threshold == 2.0
    
    # Test persistence config
    persistence = PersistenceConfig()
    assert persistence.data_directory == "feedback_data"
    assert persistence.retention_period == timedelta(days=30)
    assert persistence.max_storage_size_bytes == 1024 * 1024 * 1024  # 1GB
    assert persistence.enable_compression is True
    assert persistence.enable_backup is True
    assert persistence.backup_interval_hours == 24
    assert persistence.max_backup_count == 7

def test_communication_outcome():
    # Test outcome creation and properties
    outcome = CommunicationOutcome()
    outcome.success = True
    outcome.latency = timedelta(milliseconds=100)
    outcome.bytes_transferred = 1024
    outcome.retry_count = 1
    outcome.error_count = 0
    outcome.error_type = ""
    outcome.timestamp = datetime.now()
    
    assert outcome.success is True
    assert outcome.latency.total_seconds() == 0.1
    assert outcome.bytes_transferred == 1024
    assert outcome.retry_count == 1
    assert outcome.error_count == 0
    assert isinstance(outcome.timestamp, datetime)

def test_feedback_loop_basic():
    config = FeedbackLoopConfig()
    loop = FeedbackLoop(config)
    
    # Test reporting outcomes
    outcome = CommunicationOutcome()
    outcome.success = True
    outcome.latency = timedelta(milliseconds=100)
    outcome.bytes_transferred = 1024
    loop.report_outcome(outcome)
    
    # Test getting current metrics
    metrics = loop.get_current_metrics()
    assert isinstance(metrics, MetricsSummary)
    assert metrics.success_rate > 0
    assert metrics.average_latency > 0
    assert metrics.throughput_bytes_per_second > 0
    
    # Test getting recent outcomes
    recent = loop.get_recent_outcomes(limit=10)
    assert len(recent) == 1
    assert recent[0].success is True

def test_detailed_analysis():
    config = FeedbackLoopConfig()
    config.enable_detailed_analysis = True
    loop = FeedbackLoop(config)
    
    # Add some test data
    for i in range(100):
        outcome = CommunicationOutcome()
        outcome.success = True
        outcome.latency = timedelta(milliseconds=100 + i)
        outcome.bytes_transferred = 1024
        outcome.timestamp = datetime.now() - timedelta(minutes=i)
        loop.report_outcome(outcome)
    
    # Test latency distribution analysis
    latency_stats = loop.analyze_latency_distribution()
    assert isinstance(latency_stats, DistributionStats)
    assert latency_stats.min > 0
    assert latency_stats.max > latency_stats.min
    assert latency_stats.mean > 0
    assert latency_stats.median > 0
    assert latency_stats.standard_deviation > 0
    
    # Test throughput distribution analysis
    throughput_stats = loop.analyze_throughput_distribution()
    assert isinstance(throughput_stats, DistributionStats)
    assert throughput_stats.min > 0
    
    # Test trend analysis
    trend = loop.analyze_latency_trend()
    assert isinstance(trend, TimeSeriesAnalysis)
    assert isinstance(trend.trend_slope, float)
    assert isinstance(trend.is_stationary, bool)
    
    # Test error distribution
    error_dist = loop.get_error_type_distribution()
    assert isinstance(error_dist, dict)
    
    # Test outlier detection
    outliers = loop.get_outliers()
    assert isinstance(outliers, list)

def test_persistence():
    config = FeedbackLoopConfig()
    config.enable_persistence = True
    config.persistence.data_directory = "test_feedback_data"
    config.persistence.enable_backup = True
    loop = FeedbackLoop(config)
    
    # Add some test data
    for i in range(10):
        outcome = CommunicationOutcome()
        outcome.success = True
        outcome.latency = timedelta(milliseconds=100)
        outcome.bytes_transferred = 1024
        loop.report_outcome(outcome)
    
    # Test saving and loading
    loop.save_data()
    loop.load_data()
    
    # Test backup functionality
    backup_id = loop.create_backup()
    assert isinstance(backup_id, str)
    
    backups = loop.list_backups()
    assert len(backups) > 0
    assert backup_id in backups
    
    # Test restoration
    loop.restore_from_backup(backup_id)
    
    # Test storage management
    size = loop.get_storage_size()
    assert size > 0
    
    last_backup = loop.get_last_backup_time()
    assert isinstance(last_backup, datetime)
    
    oldest_data = loop.get_oldest_data_time()
    assert isinstance(oldest_data, datetime)
    
    # Test data querying
    start_time = datetime.now() - timedelta(hours=1)
    end_time = datetime.now()
    outcomes = loop.get_outcomes_by_time_range(start_time, end_time)
    assert isinstance(outcomes, list)
    
    metric_history = loop.get_metric_history("latency", start_time, end_time)
    assert isinstance(metric_history, list) 
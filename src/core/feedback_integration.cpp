#include "xenocomm/core/feedback_integration.h"
#include "xenocomm/core/error_correction_mode.h"
#include <thread>
#include <chrono>
#include <stdexcept>
#include <string>
#include <cmath>
#include <iostream>

namespace xenocomm {

// Simple logging implementation
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

namespace {
    double calculate_change(double current, double previous) {
        if (previous == 0) return 0;
        return (current - previous) / previous;
    }
    
    template<typename T>
    T adjust_value(T current, T min, T max, double factor, double sensitivity) {
        T adjusted = static_cast<T>(current * (1 + factor * sensitivity));
        return std::max(min, std::min(max, adjusted));
    }
}

// Constructor is already defined in the header file

Result<void> FeedbackIntegration::start() {
    if (running_) {
        return Result<void>("FeedbackIntegration already running");
    }

    try {
        // Set up retry event callback
        transmission_mgr_.set_retry_callback(
            [this](const core::TransmissionManager::RetryEvent& event) {
                handle_retry_event(event);
            });

        running_ = true;

        // Start update thread if auto-updates are enabled
        if (config_.enable_auto_updates) {
            update_thread_ = std::make_unique<std::thread>([this]() {
                while (running_) {
                    std::this_thread::sleep_for(config_.strategy_update_interval);
                    if (running_) {  // Check again after sleep
                        analyze_and_update_strategy();
                    }
                }
            });
        }

        return Result<void>();
    } catch (const std::exception& e) {
        return Result<void>("Failed to start FeedbackIntegration: " + 
                          std::string(e.what()));
    }
}

void FeedbackIntegration::stop() {
    running_ = false;
    
    if (update_thread_ && update_thread_->joinable()) {
        update_thread_->join();
    }
}

void FeedbackIntegration::set_config(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
}

Result<FeedbackIntegration::StrategyRecommendation> 
FeedbackIntegration::get_latest_recommendation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Result<StrategyRecommendation>(latest_recommendation_);
}

void FeedbackIntegration::set_strategy_callback(
    std::function<void(const StrategyRecommendation&)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    strategy_callback_ = std::move(callback);
}

Result<void> FeedbackIntegration::update_strategy() {
    try {
        analyze_and_update_strategy();
        return Result<void>();
    } catch (const std::exception& e) {
        return Result<void>("Failed to update strategy: " + std::string(e.what()));
    }
}

void FeedbackIntegration::handle_retry_event(
    const core::TransmissionManager::RetryEvent& event) {
    
    // Convert retry event to communication outcome
    CommunicationOutcome outcome;
    outcome.success = (event.type == core::TransmissionManager::RetryEventType::RETRY_SUCCESS);
    outcome.timestamp = std::chrono::system_clock::now();
    outcome.retryCount = event.attempt_number;
    outcome.errorType = event.error_message;
    outcome.errorCount = outcome.success ? 0 : 1;

    // Report to FeedbackLoop
    feedback_loop_.reportOutcome(outcome);
}

void FeedbackIntegration::handle_transmission_stats(
    const core::TransmissionManager::TransmissionStats& stats) {
    
    // Record various metrics
    feedback_loop_.recordMetric("rtt_ms", stats.current_rtt_ms);
    feedback_loop_.recordMetric("window_size", stats.current_window_size);
    feedback_loop_.recordMetric("packet_loss_rate", 
        static_cast<double>(stats.packet_loss_count) / stats.packets_sent);
    feedback_loop_.recordMetric("throughput_bps", 
        static_cast<double>(stats.bytes_sent) / 
        std::chrono::duration<double>(stats.last_update.time_since_epoch()).count());
}

void FeedbackIntegration::analyze_and_update_strategy() {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        // Get detailed metrics from FeedbackLoop
        auto metrics_result = feedback_loop_.getDetailedMetrics();
        if (metrics_result.has_error()) {
            LOG_ERROR("Failed to get detailed metrics: " + metrics_result.error());
            return;
        }

        // Generate new recommendation
        auto new_recommendation = generate_recommendation(metrics_result.value());

        // Check if significant changes are needed
        bool should_update = false;
        const auto& current = latest_recommendation_;
        const auto& proposed = new_recommendation;

        // Compare error correction modes
        if (current.error_mode != proposed.error_mode) {
            should_update = true;
        }

        // Compare fragment configs
        if (current.fragment_config.max_fragment_size != proposed.fragment_config.max_fragment_size ||
            current.fragment_config.reassembly_timeout_ms != proposed.fragment_config.reassembly_timeout_ms) {
            should_update = true;
        }

        // Compare retry configs
        if (current.retry_config.max_retries != proposed.retry_config.max_retries ||
            current.retry_config.retry_timeout_ms != proposed.retry_config.retry_timeout_ms) {
            should_update = true;
        }

        // Compare flow control configs
        if (current.flow_config.initial_window_size != proposed.flow_config.initial_window_size ||
            current.flow_config.congestion_threshold != proposed.flow_config.congestion_threshold) {
            should_update = true;
        }

        if (should_update) {
            latest_recommendation_ = new_recommendation;
            apply_recommendation(new_recommendation);

            // Notify callback if registered
            if (strategy_callback_) {
                strategy_callback_(new_recommendation);
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to analyze and update strategy: " + std::string(e.what()));
    }
}

FeedbackIntegration::StrategyRecommendation 
FeedbackIntegration::generate_recommendation(const DetailedMetrics& metrics) const {
    StrategyRecommendation recommendation = latest_recommendation_;  // Start with current config
    std::string explanation;

    // Analyze error patterns
    double error_rate = 1.0 - metrics.basic.successRate;
    if (error_rate > config_.error_rate_threshold) {
        // Increase error correction if error rate is high
        if (recommendation.error_mode == core::ErrorCorrectionMode::NONE) {
            recommendation.error_mode = core::ErrorCorrectionMode::CHECKSUM_ONLY;
            explanation += "Enabled checksum verification due to high error rate. ";
        } else if (recommendation.error_mode == core::ErrorCorrectionMode::CHECKSUM_ONLY) {
            recommendation.error_mode = core::ErrorCorrectionMode::REED_SOLOMON;
            explanation += "Upgraded to Reed-Solomon error correction due to persistent errors. ";
        }
    }

    // Analyze latency trends
    if (metrics.latencyTrend.trendSlope > 0 && 
        calculate_change(metrics.latencyStats.mean, metrics.basic.averageLatency) > 
        config_.latency_increase_threshold) {
        
        // Adjust fragment size based on latency
        recommendation.fragment_config.max_fragment_size = adjust_value(
            recommendation.fragment_config.max_fragment_size,
            static_cast<uint32_t>(512),  // Minimum fragment size
            static_cast<uint32_t>(16384), // Maximum fragment size
            -0.2,  // Reduce by up to 20%
            config_.latency_sensitivity
        );
        explanation += "Reduced fragment size to improve latency. ";

        // Adjust retry timeouts
        recommendation.retry_config.retry_timeout_ms = adjust_value(
            recommendation.retry_config.retry_timeout_ms,
            static_cast<uint32_t>(100),   // Minimum timeout
            static_cast<uint32_t>(5000),  // Maximum timeout
            0.1,   // Increase by up to 10%
            config_.latency_sensitivity
        );
        explanation += "Adjusted retry timeouts based on latency patterns. ";
    }

    // Analyze throughput
    if (metrics.throughputTrend.trendSlope < 0 && 
        calculate_change(metrics.throughputStats.mean, metrics.basic.throughputBytesPerSecond) < 
        -config_.throughput_decrease_threshold) {
        
        // Adjust window sizes for flow control
        recommendation.flow_config.initial_window_size = adjust_value(
            recommendation.flow_config.initial_window_size,
            static_cast<uint32_t>(1024),     // Minimum window size
            static_cast<uint32_t>(1048576),  // Maximum window size
            -0.15,  // Reduce by up to 15%
            config_.throughput_sensitivity
        );
        explanation += "Adjusted flow control windows for throughput optimization. ";

        // Adjust congestion thresholds
        recommendation.flow_config.congestion_threshold = adjust_value(
            recommendation.flow_config.congestion_threshold,
            static_cast<uint32_t>(50),   // Minimum threshold
            static_cast<uint32_t>(200),  // Maximum threshold
            0.1,    // Increase by up to 10%
            config_.throughput_sensitivity
        );
        explanation += "Updated congestion thresholds based on throughput analysis. ";
    }

    recommendation.explanation = explanation.empty() ? 
        "No significant changes needed" : explanation;

    return recommendation;
}

void FeedbackIntegration::apply_recommendation(const StrategyRecommendation& recommendation) {
    // Update TransmissionManager configuration
    core::TransmissionManager::Config tm_config;
    
    // Copy over configuration values
    tm_config.error_correction_mode = recommendation.error_mode;
    tm_config.fragment_config = recommendation.fragment_config;
    tm_config.retransmission_config = recommendation.retry_config;
    tm_config.flow_control = recommendation.flow_config;
    
    // Apply to TransmissionManager using set_config which exists in the interface
    transmission_mgr_.set_config(tm_config);
    
    // Update last update time
    last_update_ = std::chrono::steady_clock::now();
    
    LOG_INFO("Applied new transmission strategy: " + recommendation.explanation);
}

} // namespace xenocomm 
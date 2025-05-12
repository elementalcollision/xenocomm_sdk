#pragma once

#include "xenocomm/core/feedback_loop.h"
#include "xenocomm/core/transmission_manager.h"
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace xenocomm {

/**
 * @brief Integration layer between FeedbackLoop and TransmissionManager
 * 
 * This class provides the necessary hooks and adapters to connect
 * the FeedbackLoop's monitoring capabilities with TransmissionManager's
 * operations, creating a complete feedback cycle for optimizing
 * communication performance.
 */
class FeedbackIntegration {
public:
    /**
     * @brief Configuration for the feedback integration
     */
    struct Config {
        // How often to update transmission strategies based on feedback
        std::chrono::milliseconds strategy_update_interval{5000};
        
        // Thresholds for strategy adjustments
        double error_rate_threshold{0.1};        // 10% error rate
        double latency_increase_threshold{0.5};  // 50% increase
        double throughput_decrease_threshold{0.3}; // 30% decrease
        
        // Sensitivity for different metrics (0.0 - 1.0)
        double error_sensitivity{0.7};
        double latency_sensitivity{0.8};
        double throughput_sensitivity{0.6};
        
        // Whether to enable automatic strategy updates
        bool enable_auto_updates{true};
    };

    /**
     * @brief Strategy recommendations based on feedback analysis
     */
    struct StrategyRecommendation {
        core::ErrorCorrectionMode error_mode;
        core::TransmissionManager::FragmentConfig fragment_config;
        core::TransmissionManager::RetransmissionConfig retry_config;
        core::TransmissionManager::FlowControlConfig flow_config;
        std::string explanation;  // Reason for the recommendation
    };

    /**
     * @brief Constructs a FeedbackIntegration instance
     * 
     * @param feedback_loop The FeedbackLoop instance to use
     * @param transmission_mgr The TransmissionManager instance to integrate with
     * @param config Configuration options for the integration
     */
    FeedbackIntegration(
        FeedbackLoop& feedback_loop,
        core::TransmissionManager& transmission_mgr,
        const Config& config)
    : feedback_loop_(feedback_loop), transmission_mgr_(transmission_mgr), config_(config) {
        // Constructor body (if any)
    }

    /**
     * @brief Starts the feedback integration
     * 
     * Sets up event listeners and begins collecting feedback.
     * 
     * @return Result<void> Success or error status
     */
    Result<void> start();

    /**
     * @brief Stops the feedback integration
     * 
     * Removes event listeners and stops feedback collection.
     */
    void stop();

    /**
     * @brief Updates the configuration
     * 
     * @param config New configuration to apply
     */
    void set_config(const Config& config);

    /**
     * @brief Gets the current configuration
     * 
     * @return const Config& Current configuration
     */
    const Config& get_config() const { return config_; }

    /**
     * @brief Gets the latest strategy recommendation
     * 
     * @return Result<StrategyRecommendation> Latest recommendation or error
     */
    Result<StrategyRecommendation> get_latest_recommendation() const;

    /**
     * @brief Manually triggers a strategy update
     * 
     * @return Result<void> Success or error status
     */
    Result<void> update_strategy();

    /**
     * @brief Sets a callback for strategy updates
     * 
     * @param callback Function to call when new recommendations are available
     */
    void set_strategy_callback(std::function<void(const StrategyRecommendation&)> callback);

private:
    // Internal methods for feedback processing
    void handle_retry_event(const core::TransmissionManager::RetryEvent& event);
    void handle_transmission_stats(const core::TransmissionManager::TransmissionStats& stats);
    void analyze_and_update_strategy();
    StrategyRecommendation generate_recommendation(const DetailedMetrics& metrics) const;
    void apply_recommendation(const StrategyRecommendation& recommendation);
    
    // Member variables
    FeedbackLoop& feedback_loop_;
    core::TransmissionManager& transmission_mgr_;
    Config config_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> update_thread_;
    mutable std::mutex mutex_;
    StrategyRecommendation latest_recommendation_;
    std::function<void(const StrategyRecommendation&)> strategy_callback_;
    std::chrono::steady_clock::time_point last_update_;
};

} // namespace xenocomm 
#pragma once

#include "xenocomm/core/connection_manager.hpp"
#include "xenocomm/core/security_manager.h"
#include "xenocomm/utils/result.h"
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include <map>
#include <chrono>
#include <deque>
#include <mutex>
#include <functional>

namespace xenocomm {
namespace core {

// Forward declaration
class IErrorCorrection;

/**
 * @brief Configuration for transmission security
 */
struct SecurityConfig {
    bool enable_encryption = false;
    bool require_encryption = false;
    bool verify_hostname = true;
    std::string expected_hostname;
    std::shared_ptr<SecurityManager> security_manager;
};

/**
 * @brief Statistics about the secure connection
 */
struct SecurityStats {
    bool is_encrypted = false;
    std::string cipher_suite;
    std::string protocol_version;
    std::string peer_certificate_info;
};

/**
 * @brief Manages data transmission with configurable error correction and security.
 * 
 * The TransmissionManager class provides a high-level interface for sending and receiving
 * data through a ConnectionManager, with support for various error correction modes,
 * data fragmentation, flow control, and secure communication.
 */
class TransmissionManager {
public:
    /**
     * @brief Error correction modes supported by the TransmissionManager.
     */
    enum class ErrorCorrectionMode {
        NONE,           ///< No error correction, raw data transfer
        CHECKSUM_ONLY,  ///< Basic error detection using CRC32
        REED_SOLOMON    ///< Full error correction using Reed-Solomon codes
    };

    /**
     * @brief Configuration options for the TransmissionManager.
     */
    struct FragmentConfig {
        uint32_t max_fragment_size = 1024;  // Default fragment size in bytes
        uint32_t reassembly_timeout_ms = 5000;  // Default timeout for fragment reassembly
        uint32_t max_fragments = 65535;  // Maximum number of fragments per transmission
        uint32_t fragment_buffer_size = 1024 * 1024;  // Default buffer size for reassembly
    };

    struct RetransmissionConfig {
        uint32_t max_retries = 3;           // Maximum number of retransmission attempts
        uint32_t retry_timeout_ms = 1000;   // Time to wait before retransmission
        uint32_t ack_timeout_ms = 500;      // Time to wait for acknowledgment
    };

    struct FlowControlConfig {
        uint32_t initial_window_size = 65535;    // Initial size of the sliding window in bytes
        uint32_t min_window_size = 1024;         // Minimum window size during congestion
        uint32_t max_window_size = 1048576;      // Maximum window size (1MB)
        uint32_t rtt_smoothing_factor = 8;       // Factor for RTT averaging (1/8)
        uint32_t congestion_threshold = 100;      // RTT increase percentage to trigger congestion
        uint32_t backoff_multiplier = 2;         // Multiplicative decrease factor
        uint32_t recovery_multiplier = 2;        // Multiplicative increase factor
        uint32_t min_rtt_samples = 10;           // Minimum RTT samples before adaptation
    };

    struct TransmissionStats {
        uint64_t bytes_sent = 0;
        uint64_t bytes_received = 0;
        uint64_t packets_sent = 0;
        uint64_t packets_received = 0;
        uint64_t retransmissions = 0;
        double current_rtt_ms = 0;
        double avg_rtt_ms = 0;
        double min_rtt_ms = std::numeric_limits<double>::max();
        double max_rtt_ms = 0;
        uint32_t current_window_size = 0;
        uint32_t packet_loss_count = 0;
        std::chrono::steady_clock::time_point last_update;
        
        // Security-related stats
        bool is_encrypted = false;
        std::string cipher_suite;
        std::string protocol_version;
        std::string peer_certificate_info;
    };

    /**
     * @brief Configuration for transmission management
     */
    struct Config {
        ErrorCorrectionMode error_correction_mode = ErrorCorrectionMode::CHECKSUM_ONLY;
        FragmentConfig fragment_config;
        RetransmissionConfig retransmission_config;
        FlowControlConfig flow_control;
        SecurityConfig security;  // Security configuration
        uint8_t retry_attempts = 3;
        bool enable_logging = true;
    };

    // Fragment header structure
    struct FragmentHeader {
        uint32_t transmission_id;    // Unique ID for this transmission
        uint16_t fragment_index;     // Index of this fragment
        uint16_t total_fragments;    // Total number of fragments in transmission
        uint32_t fragment_size;      // Size of fragment data
        uint32_t original_size;      // Original total data size
        uint32_t error_check;        // Error check value (CRC32 or Reed-Solomon syndrome)
        bool is_encrypted;           // Whether this fragment is encrypted
        uint8_t security_flags;      // Security-related flags
    };

    struct FragmentAck {
        uint32_t transmission_id;
        uint16_t fragment_index;
        bool success;
        uint32_t error_code;
    };

    /**
     * @brief Event types for retry notifications
     */
    enum class RetryEventType {
        RETRY_ATTEMPT,      ///< A retry attempt is being made
        RETRY_SUCCESS,      ///< A retry attempt succeeded
        RETRY_FAILURE,      ///< A retry attempt failed
        MAX_RETRIES_REACHED ///< Maximum number of retries reached
    };

    /**
     * @brief Structure containing retry event information
     */
    struct RetryEvent {
        RetryEventType type;
        uint32_t transmission_id;
        uint16_t fragment_index;
        uint32_t attempt_number;
        std::string error_message;
        std::chrono::steady_clock::time_point timestamp;
    };

    /**
     * @brief Retry statistics for monitoring and analysis
     */
    struct RetryStats {
        uint64_t total_retries = 0;
        uint64_t successful_retries = 0;
        uint64_t failed_retries = 0;
        uint64_t max_retries_reached = 0;
        double avg_retry_latency_ms = 0;
        std::chrono::steady_clock::time_point last_retry;
        std::map<uint32_t, uint32_t> retry_distribution;  // retry_count -> frequency
    };

    // Callback type for retry events
    using RetryCallback = std::function<void(const RetryEvent&)>;

    /**
     * @brief Constructs a TransmissionManager instance.
     * 
     * @param connection_manager The ConnectionManager to use for data transfer
     * @throw std::runtime_error if error correction initialization fails
     */
    explicit TransmissionManager(ConnectionManager& connection_manager);

    /**
     * @brief Sends data through the connection with error correction.
     * 
     * @param data The data to send
     * @return Result<void> Success or error status
     */
    Result<void> send(const std::vector<uint8_t>& data);

    /**
     * @brief Receives data and applies error correction if needed.
     * 
     * @return Result<std::vector<uint8_t>> The received data or error status
     */
    Result<std::vector<uint8_t>> receive(uint32_t timeout_ms = 1000);

    /**
     * @brief Updates the configuration settings.
     * 
     * @param config The new configuration to apply
     */
    void set_config(const Config& config);

    /**
     * @brief Gets the current configuration.
     * 
     * @return const Config& The current configuration
     */
    const Config& get_config() const { return config_; }

    // Flow control methods
    const TransmissionStats& get_stats() const;
    void reset_stats();
    Result<void> wait_for_window_space(size_t data_size, std::chrono::milliseconds timeout);
    void release_window_space(size_t data_size);

    /**
     * @brief Register a callback for retry events
     * 
     * @param callback Function to be called when retry events occur
     */
    void set_retry_callback(RetryCallback callback);

    /**
     * @brief Get current retry statistics
     * 
     * @return const RetryStats& Current retry statistics
     */
    const RetryStats& get_retry_stats() const;

    /**
     * @brief Reset retry statistics
     */
    void reset_retry_stats();

    /**
     * @brief Get the current security status
     * 
     * @return String describing the current security status
     */
    std::string get_security_status() const;

    /**
     * @brief Force renegotiation of secure connection
     * 
     * @return Result<void> Success or error status
     */
    Result<void> renegotiate_security();

    /**
     * @brief Set up a secure channel for encrypted communication
     * @return Result indicating success or failure
     */
    Result<void> setup_secure_channel();

private:
    // Fragmentation methods
    std::vector<std::vector<uint8_t>> fragment_data(const std::vector<uint8_t>& data);
    Result<void> send_fragment(const std::vector<uint8_t>& fragment, const FragmentHeader& header);
    Result<std::vector<uint8_t>> receive_fragment();
    Result<std::vector<uint8_t>> reassemble_fragments(uint32_t transmission_id);

    // Fragment tracking
    struct FragmentInfo {
        std::vector<uint8_t> data;
        bool received = false;
        std::chrono::steady_clock::time_point timestamp;
    };

    struct ReassemblyContext {
        std::map<uint16_t, FragmentInfo> fragments;
        uint16_t total_fragments;
        uint32_t original_size;
        std::chrono::steady_clock::time_point start_time;
    };

    std::map<uint32_t, ReassemblyContext> reassembly_contexts_;
    uint32_t next_transmission_id_ = 0;

    // Fragment error correction methods
    Result<std::vector<uint8_t>> apply_error_correction(const std::vector<uint8_t>& data);
    Result<std::vector<uint8_t>> verify_and_correct(const std::vector<uint8_t>& data);
    uint32_t calculate_error_check(const std::vector<uint8_t>& data);
    
    // Fragment acknowledgment methods
    Result<void> wait_for_ack(uint32_t transmission_id, uint16_t fragment_index);
    Result<void> send_ack(const FragmentAck& ack);
    Result<FragmentAck> receive_ack();
    
    // Retransmission methods
    Result<void> handle_retransmission(uint32_t transmission_id, uint16_t fragment_index);
    Result<void> request_retransmission(uint32_t transmission_id, uint16_t fragment_index);

    // Helper methods
    void cleanup_expired_contexts();
    bool is_reassembly_complete(const ReassemblyContext& context) const;
    std::vector<uint8_t> serialize_header(const FragmentHeader& header);
    FragmentHeader deserialize_header(const std::vector<uint8_t>& data);

    // Class members
    ConnectionManager& connection_manager_;
    Config config_;
    std::unique_ptr<IErrorCorrection> error_correction_;
    std::unique_ptr<Logger> logger_;

    // Additional tracking for retransmission
    struct TransmissionState {
        std::map<uint16_t, uint32_t> retry_counts;  // fragment_index -> retry count
        std::chrono::steady_clock::time_point last_attempt;
        bool complete = false;
    };
    
    std::map<uint32_t, TransmissionState> transmission_states_;

    // Flow control and congestion avoidance
    struct WindowState {
        uint32_t current_size;
        uint32_t available_credits;
        std::chrono::steady_clock::time_point last_adjustment;
        bool in_congestion_avoidance;
        std::deque<std::chrono::steady_clock::time_point> rtt_samples;
        std::mutex mutex;
    };

    WindowState window_state_;
    TransmissionStats stats_;

    void update_rtt(uint32_t transmission_id, const std::chrono::steady_clock::time_point& send_time);
    void adjust_window_size(bool packet_loss);
    bool check_congestion();
    void apply_backoff();
    void update_stats(const std::vector<uint8_t>& data, bool is_receive);

    RetryCallback retry_callback_;
    RetryStats retry_stats_;
    std::mutex retry_stats_mutex_;

    // Enhanced retry handling methods
    void notify_retry_event(RetryEventType type, uint32_t transmission_id, 
                          uint16_t fragment_index, uint32_t attempt, 
                          const std::string& error = "");
    
    uint32_t calculate_retry_delay(uint32_t attempt);
    bool should_retry(uint32_t transmission_id, uint16_t fragment_index);
    void update_retry_stats(const RetryEvent& event);

    // Security-related private methods
    Result<std::vector<uint8_t>> encrypt_data(const std::vector<uint8_t>& data);
    Result<std::vector<uint8_t>> decrypt_data(const std::vector<uint8_t>& data);
    void update_security_stats();
    bool verify_security_requirements();

    // Security-related private members
    std::shared_ptr<SecureContext> secure_context_;
    bool is_secure_channel_established_ = false;
    SecurityStats stats_;
    mutable std::mutex security_mutex_;
};

} // namespace core
} // namespace xenocomm 
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include "xenocomm/core/transmission_manager.h"
#include "xenocomm/core/connection_manager.h"
#include "xenocomm/utils/result.h"
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <queue>

using namespace xenocomm;
using namespace xenocomm::core;

// Mock ConnectionManager for testing
class MockConnectionManager : public ConnectionManager {
public:
    MockConnectionManager() : connected_(true) {}

    void set_connected(bool connected) { connected_ = connected; }
    bool is_connected() const override { return connected_; }

    Result<void> send(const std::vector<uint8_t>& data) override {
        if (!connected_) {
            return Result<void>::error("Not connected");
        }
        sent_data_.push_back(data);
        return Result<void>::ok();
    }

    Result<std::vector<uint8_t>> receive() override {
        if (!connected_) {
            return Result<std::vector<uint8_t>>::error("Not connected");
        }
        if (received_data_.empty()) {
            return Result<std::vector<uint8_t>>::error("No data available");
        }
        auto data = received_data_.front();
        received_data_.pop_front();
        return Result<std::vector<uint8_t>>::ok(data);
    }

    void queue_received_data(const std::vector<uint8_t>& data) {
        received_data_.push_back(data);
    }

    const std::vector<std::vector<uint8_t>>& get_sent_data() const { return sent_data_; }
    const std::deque<std::vector<uint8_t>>& get_received_data() const { return received_data_; }

private:
    bool connected_;
    std::vector<std::vector<uint8_t>> sent_data_;
    std::deque<std::vector<uint8_t>> received_data_;
};

// Helper function to corrupt data
void corrupt_data(std::vector<uint8_t>& data, size_t num_errors) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> pos_dist(0, data.size() - 1);
    std::uniform_int_distribution<> val_dist(0, 255);
    
    for (size_t i = 0; i < num_errors; i++) {
        size_t pos = pos_dist(gen);
        uint8_t new_val;
        do {
            new_val = static_cast<uint8_t>(val_dist(gen));
        } while (new_val == data[pos]);
        data[pos] = new_val;
    }
}

TEST_CASE("TransmissionManager initialization", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("Default configuration") {
        REQUIRE(manager.get_config().error_correction_mode == 
                TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY);
    }
    
    SECTION("Configuration update") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::NONE;
        manager.configure(config);
        
        REQUIRE(manager.get_config().error_correction_mode == 
                TransmissionManager::ErrorCorrectionMode::NONE);
    }
}

TEST_CASE("TransmissionManager connection validation", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("Send with disconnected connection") {
        mock_conn.set_connected(false);
        auto result = manager.send({1, 2, 3, 4});
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error() == "Cannot send: Connection not established");
    }
    
    SECTION("Receive with disconnected connection") {
        mock_conn.set_connected(false);
        auto result = manager.receive();
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error() == "Cannot receive: Connection not established");
    }
}

TEST_CASE("TransmissionManager basic data transfer", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("Send and receive with NONE mode") {
        // Configure for no error correction
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::NONE;
        manager.configure(config);
        
        // Test data
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        
        // Send data
        auto send_result = manager.send(test_data);
        REQUIRE(send_result.is_ok());
        REQUIRE(mock_conn.get_sent_data().back() == test_data);
        
        // Receive data
        auto receive_result = manager.receive();
        REQUIRE(receive_result.is_ok());
        REQUIRE(receive_result.value() == test_data);
    }
    
    SECTION("Send and receive with CHECKSUM_ONLY mode") {
        // Configure for checksum only
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY;
        manager.configure(config);
        
        // Test data
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        
        // Send data (currently same as NONE mode since checksum not implemented)
        auto send_result = manager.send(test_data);
        REQUIRE(send_result.is_ok());
        REQUIRE(mock_conn.get_sent_data().back() == test_data);
        
        // Receive data
        auto receive_result = manager.receive();
        REQUIRE(receive_result.is_ok());
        REQUIRE(receive_result.value() == test_data);
    }
    
    SECTION("Send and receive with REED_SOLOMON mode") {
        // Configure for Reed-Solomon
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::REED_SOLOMON;
        manager.configure(config);
        
        // Test data
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        
        // Send data (currently falls back to CHECKSUM_ONLY)
        auto send_result = manager.send(test_data);
        REQUIRE(send_result.is_ok());
        REQUIRE(mock_conn.get_sent_data().back() == test_data);
        
        // Receive data
        auto receive_result = manager.receive();
        REQUIRE(receive_result.is_ok());
        REQUIRE(receive_result.value() == test_data);
    }
}

TEST_CASE("TransmissionManager fragmentation", "[transmission]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);

    SECTION("Send fragments small payload") {
        std::vector<uint8_t> data(100, 0x42);  // 100 bytes of 0x42
        auto result = manager.send(data);
        REQUIRE(result.is_ok());
        REQUIRE(mock_conn.get_sent_data().size() == 1);  // Should be single fragment
    }

    SECTION("Send fragments large payload") {
        // Create payload larger than default fragment size
        std::vector<uint8_t> data(2000, 0x42);  // 2000 bytes
        auto result = manager.send(data);
        REQUIRE(result.is_ok());
        REQUIRE(mock_conn.get_sent_data().size() == 2);  // Should be two fragments
    }

    SECTION("Send empty payload") {
        auto result = manager.send({});
        REQUIRE(result.is_ok());
        REQUIRE(mock_conn.get_sent_data().empty());
    }

    SECTION("Send with disconnected connection") {
        mock_conn.set_connected(false);
        auto result = manager.send({1, 2, 3});
        REQUIRE(!result.is_ok());
    }
}

TEST_CASE("TransmissionManager reassembly", "[transmission]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);

    SECTION("Receive single fragment") {
        // Create a single fragment with header
        TransmissionManager::FragmentHeader header{
            .transmission_id = 1,
            .fragment_index = 0,
            .total_fragments = 1,
            .fragment_size = 100,
            .original_size = 100
        };

        std::vector<uint8_t> fragment_data(100, 0x42);
        std::vector<uint8_t> complete_fragment;
        complete_fragment.resize(sizeof(header));
        std::memcpy(complete_fragment.data(), &header, sizeof(header));
        complete_fragment.insert(complete_fragment.end(), fragment_data.begin(), fragment_data.end());

        mock_conn.queue_received_data(complete_fragment);
        
        auto result = manager.receive(1000);
        REQUIRE(result.is_ok());
        REQUIRE(result.value().size() == 100);
        REQUIRE(result.value() == fragment_data);
    }

    SECTION("Receive multiple fragments in order") {
        std::vector<uint8_t> fragment1(500, 0x42);
        std::vector<uint8_t> fragment2(500, 0x43);
        
        // Create and queue first fragment
        TransmissionManager::FragmentHeader header1{
            .transmission_id = 1,
            .fragment_index = 0,
            .total_fragments = 2,
            .fragment_size = 500,
            .original_size = 1000
        };
        std::vector<uint8_t> complete_fragment1;
        complete_fragment1.resize(sizeof(header1));
        std::memcpy(complete_fragment1.data(), &header1, sizeof(header1));
        complete_fragment1.insert(complete_fragment1.end(), fragment1.begin(), fragment1.end());
        
        // Create and queue second fragment
        TransmissionManager::FragmentHeader header2{
            .transmission_id = 1,
            .fragment_index = 1,
            .total_fragments = 2,
            .fragment_size = 500,
            .original_size = 1000
        };
        std::vector<uint8_t> complete_fragment2;
        complete_fragment2.resize(sizeof(header2));
        std::memcpy(complete_fragment2.data(), &header2, sizeof(header2));
        complete_fragment2.insert(complete_fragment2.end(), fragment2.begin(), fragment2.end());

        mock_conn.queue_received_data(complete_fragment1);
        mock_conn.queue_received_data(complete_fragment2);
        
        auto result = manager.receive(1000);
        REQUIRE(result.is_ok());
        REQUIRE(result.value().size() == 1000);
        
        // Verify reassembled data
        std::vector<uint8_t> expected;
        expected.insert(expected.end(), fragment1.begin(), fragment1.end());
        expected.insert(expected.end(), fragment2.begin(), fragment2.end());
        REQUIRE(result.value() == expected);
    }

    SECTION("Receive with timeout") {
        auto result = manager.receive(100);  // Short timeout
        REQUIRE(!result.is_ok());
        REQUIRE(result.error() == "Receive timeout");
    }

    SECTION("Receive with disconnected connection") {
        mock_conn.set_connected(false);
        auto result = manager.receive(1000);
        REQUIRE(!result.is_ok());
    }
}

TEST_CASE("TransmissionManager configuration", "[transmission]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);

    SECTION("Configure fragment size") {
        TransmissionManager::Config config;
        config.fragment_config.max_fragment_size = 500;  // Set smaller fragment size
        manager.set_config(config);

        // Send data larger than fragment size
        std::vector<uint8_t> data(1000, 0x42);
        auto result = manager.send(data);
        REQUIRE(result.is_ok());
        REQUIRE(mock_conn.get_sent_data().size() == 3);  // Should be three fragments
    }

    SECTION("Configure reassembly timeout") {
        TransmissionManager::Config config;
        config.fragment_config.reassembly_timeout_ms = 100;  // Set short timeout
        manager.set_config(config);

        // Queue incomplete set of fragments
        TransmissionManager::FragmentHeader header{
            .transmission_id = 1,
            .fragment_index = 0,
            .total_fragments = 2,  // Expect 2 fragments
            .fragment_size = 100,
            .original_size = 200
        };
        std::vector<uint8_t> fragment_data(100, 0x42);
        std::vector<uint8_t> complete_fragment;
        complete_fragment.resize(sizeof(header));
        std::memcpy(complete_fragment.data(), &header, sizeof(header));
        complete_fragment.insert(complete_fragment.end(), fragment_data.begin(), fragment_data.end());

        mock_conn.queue_received_data(complete_fragment);
        
        auto result = manager.receive(200);  // Wait longer than reassembly timeout
        REQUIRE(!result.is_ok());  // Should fail due to timeout
    }
}

TEST_CASE("TransmissionManager error correction", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("NONE mode passes data unchanged") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::NONE;
        manager.set_config(config);
        
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        auto result = manager.send(test_data);
        REQUIRE(result.is_ok());
        
        const auto& sent_data = mock_conn.get_sent_data().back();
        REQUIRE(sent_data.size() > test_data.size()); // Account for header
        
        // Extract payload from sent data
        std::vector<uint8_t> payload(sent_data.begin() + sizeof(TransmissionManager::FragmentHeader),
                                   sent_data.end());
        REQUIRE(payload == test_data);
    }
    
    SECTION("CHECKSUM_ONLY mode detects corruption") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY;
        manager.set_config(config);
        
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        auto send_result = manager.send(test_data);
        REQUIRE(send_result.is_ok());
        
        auto sent_data = mock_conn.get_sent_data().back();
        corrupt_data(sent_data, 1); // Corrupt one byte
        
        mock_conn.queue_received_data(sent_data);
        auto receive_result = manager.receive();
        REQUIRE_FALSE(receive_result.is_ok());
        REQUIRE(receive_result.error() == "Error check failed");
    }
    
    SECTION("REED_SOLOMON mode corrects errors") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::REED_SOLOMON;
        manager.set_config(config);
        
        std::vector<uint8_t> test_data(100, 0x42);
        auto send_result = manager.send(test_data);
        REQUIRE(send_result.is_ok());
        
        auto sent_data = mock_conn.get_sent_data().back();
        corrupt_data(sent_data, 2); // Corrupt two bytes
        
        mock_conn.queue_received_data(sent_data);
        auto receive_result = manager.receive();
        REQUIRE(receive_result.is_ok());
        REQUIRE(receive_result.value() == test_data); // Should be corrected
    }
}

TEST_CASE("TransmissionManager retransmission", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("Successful retransmission after corruption") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY;
        config.retransmission_config.max_retries = 3;
        config.retransmission_config.retry_timeout_ms = 100;
        manager.set_config(config);
        
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        
        // First attempt will be corrupted
        bool first_attempt = true;
        std::thread sender([&]() {
            auto result = manager.send(test_data);
            REQUIRE(result.is_ok());
        });
        
        std::thread receiver([&]() {
            while (true) {
                auto result = manager.receive();
                if (result.is_ok()) {
                    REQUIRE(result.value() == test_data);
                    break;
                }
                if (first_attempt) {
                    first_attempt = false;
                    // Corrupt and request retransmission
                    auto& last_sent = mock_conn.get_sent_data().back();
                    corrupt_data(last_sent, 1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        
        sender.join();
        receiver.join();
    }
    
    SECTION("Maximum retries exceeded") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY;
        config.retransmission_config.max_retries = 2;
        config.retransmission_config.retry_timeout_ms = 100;
        manager.set_config(config);
        
        std::vector<uint8_t> test_data = {1, 2, 3, 4, 5};
        
        // All attempts will be corrupted
        std::thread sender([&]() {
            auto result = manager.send(test_data);
            REQUIRE_FALSE(result.is_ok());
            REQUIRE(result.error() == "Failed to send fragment after all retries");
        });
        
        std::thread receiver([&]() {
            int attempts = 0;
            while (attempts < 3) {
                auto result = manager.receive();
                if (!result.is_ok()) {
                    attempts++;
                    // Corrupt and request retransmission
                    auto& last_sent = mock_conn.get_sent_data().back();
                    corrupt_data(last_sent, 1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        
        sender.join();
        receiver.join();
    }
}

TEST_CASE("TransmissionManager fragmentation with error correction", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("Large payload with error correction") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::REED_SOLOMON;
        config.fragment_config.max_fragment_size = 512;
        manager.set_config(config);
        
        // Create large test data
        std::vector<uint8_t> test_data(2000);
        std::iota(test_data.begin(), test_data.end(), 0);
        
        auto send_result = manager.send(test_data);
        REQUIRE(send_result.is_ok());
        
        // Verify multiple fragments were sent
        REQUIRE(mock_conn.get_sent_data().size() > 1);
        
        // Corrupt some fragments
        auto sent_fragments = mock_conn.get_sent_data();
        for (auto& fragment : sent_fragments) {
            if (rand() % 2 == 0) { // 50% chance to corrupt
                corrupt_data(fragment, 1);
            }
            mock_conn.queue_received_data(fragment);
        }
        
        // Should still receive correct data after reassembly
        auto receive_result = manager.receive();
        REQUIRE(receive_result.is_ok());
        REQUIRE(receive_result.value() == test_data);
    }
    
    SECTION("Fragment acknowledgment") {
        TransmissionManager::Config config;
        config.error_correction_mode = TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY;
        config.fragment_config.max_fragment_size = 512;
        manager.set_config(config);
        
        std::vector<uint8_t> test_data(1000);
        std::iota(test_data.begin(), test_data.end(), 0);
        
        bool received_ack = false;
        std::thread sender([&]() {
            auto result = manager.send(test_data);
            REQUIRE(result.is_ok());
        });
        
        std::thread receiver([&]() {
            while (!received_ack) {
                auto result = manager.receive();
                if (result.is_ok()) {
                    received_ack = true;
                    REQUIRE(result.value() == test_data);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        
        sender.join();
        receiver.join();
        REQUIRE(received_ack);
    }
}

TEST_CASE("TransmissionManager flow control", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("Initial window size") {
        TransmissionManager::Config config;
        config.flow_control.initial_window_size = 1024;
        manager.set_config(config);
        
        // Reset stats to apply new config
        manager.reset_stats();
        
        const auto& stats = manager.get_stats();
        REQUIRE(stats.current_window_size == 1024);
    }
    
    SECTION("Window size adaptation") {
        TransmissionManager::Config config;
        config.flow_control.initial_window_size = 1024;
        config.flow_control.min_window_size = 256;
        config.flow_control.max_window_size = 4096;
        config.flow_control.recovery_multiplier = 2;
        manager.set_config(config);
        manager.reset_stats();
        
        // Send data to trigger window size adjustments
        std::vector<uint8_t> data(512, 0x42);
        auto result = manager.send(data);
        REQUIRE(result.is_ok());
        
        const auto& stats = manager.get_stats();
        REQUIRE(stats.current_window_size >= config.flow_control.min_window_size);
        REQUIRE(stats.current_window_size <= config.flow_control.max_window_size);
    }
    
    SECTION("Congestion avoidance") {
        TransmissionManager::Config config;
        config.flow_control.initial_window_size = 2048;
        config.flow_control.min_window_size = 256;
        config.flow_control.backoff_multiplier = 2;
        config.flow_control.congestion_threshold = 50;  // 50% RTT increase triggers congestion
        manager.set_config(config);
        manager.reset_stats();
        
        // Simulate increasing RTTs to trigger congestion
        std::vector<uint8_t> data(1024, 0x42);
        
        // First send with normal RTT
        auto start = std::chrono::steady_clock::now();
        auto result = manager.send(data);
        REQUIRE(result.is_ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Second send with higher RTT
        result = manager.send(data);
        REQUIRE(result.is_ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        const auto& stats = manager.get_stats();
        REQUIRE(stats.current_window_size < config.flow_control.initial_window_size);
    }
    
    SECTION("Window space waiting") {
        TransmissionManager::Config config;
        config.flow_control.initial_window_size = 1024;
        manager.set_config(config);
        manager.reset_stats();
        
        // Try to send data larger than window size
        std::vector<uint8_t> large_data(2048, 0x42);
        auto result = manager.send(large_data);
        REQUIRE(!result.is_ok());
        REQUIRE(result.error() == "Window space wait timeout");
    }
    
    SECTION("Statistics tracking") {
        manager.reset_stats();
        
        std::vector<uint8_t> data(100, 0x42);
        auto result = manager.send(data);
        REQUIRE(result.is_ok());
        
        const auto& stats = manager.get_stats();
        REQUIRE(stats.bytes_sent == 100);
        REQUIRE(stats.packets_sent == 1);
        REQUIRE(stats.current_rtt_ms > 0);
    }
}

TEST_CASE("TransmissionManager adaptive behavior", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    SECTION("Window size increases under good conditions") {
        TransmissionManager::Config config;
        config.flow_control.initial_window_size = 1024;
        config.flow_control.max_window_size = 4096;
        config.flow_control.recovery_multiplier = 2;
        manager.set_config(config);
        manager.reset_stats();
        
        // Send multiple packets with stable RTT
        std::vector<uint8_t> data(256, 0x42);
        for (int i = 0; i < 5; i++) {
            auto result = manager.send(data);
            REQUIRE(result.is_ok());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        const auto& stats = manager.get_stats();
        REQUIRE(stats.current_window_size > config.flow_control.initial_window_size);
    }
    
    SECTION("Window size decreases under poor conditions") {
        TransmissionManager::Config config;
        config.flow_control.initial_window_size = 4096;
        config.flow_control.min_window_size = 256;
        config.flow_control.backoff_multiplier = 2;
        manager.set_config(config);
        manager.reset_stats();
        
        // Simulate deteriorating network conditions
        std::vector<uint8_t> data(1024, 0x42);
        for (int i = 0; i < 5; i++) {
            auto result = manager.send(data);
            REQUIRE(result.is_ok());
            std::this_thread::sleep_for(std::chrono::milliseconds(10 * (i + 1)));
        }
        
        const auto& stats = manager.get_stats();
        REQUIRE(stats.current_window_size < config.flow_control.initial_window_size);
    }
    
    SECTION("RTT statistics") {
        manager.reset_stats();
        
        std::vector<uint8_t> data(100, 0x42);
        for (int i = 0; i < 5; i++) {
            auto result = manager.send(data);
            REQUIRE(result.is_ok());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        const auto& stats = manager.get_stats();
        REQUIRE(stats.min_rtt_ms > 0);
        REQUIRE(stats.max_rtt_ms >= stats.min_rtt_ms);
        REQUIRE(stats.avg_rtt_ms >= stats.min_rtt_ms);
        REQUIRE(stats.avg_rtt_ms <= stats.max_rtt_ms);
    }
}

TEST_CASE("TransmissionManager retry mechanisms", "[transmission_manager]") {
    MockConnectionManager mock_conn;
    TransmissionManager manager(mock_conn);
    
    std::vector<TransmissionManager::RetryEvent> captured_events;
    manager.set_retry_callback([&captured_events](const TransmissionManager::RetryEvent& event) {
        captured_events.push_back(event);
    });

    SECTION("Basic retry behavior") {
        TransmissionManager::Config config;
        config.retransmission_config.max_retries = 3;
        config.retransmission_config.retry_timeout_ms = 100;
        manager.set_config(config);

        // Prepare test data
        std::vector<uint8_t> test_data(1024, 0x42);
        mock_conn.set_failure_mode(true, 2); // Fail first 2 attempts

        // Send data
        auto result = manager.send(test_data);
        REQUIRE(result.is_ok());

        // Verify retry events
        REQUIRE(captured_events.size() >= 2);
        REQUIRE(captured_events[0].type == TransmissionManager::RetryEventType::RETRY_FAILURE);
        REQUIRE(captured_events[1].type == TransmissionManager::RetryEventType::RETRY_FAILURE);
        
        // Check retry stats
        const auto& stats = manager.get_retry_stats();
        REQUIRE(stats.total_retries > 0);
        REQUIRE(stats.successful_retries > 0);
        REQUIRE(stats.failed_retries == 2);
    }

    SECTION("Max retries exceeded") {
        TransmissionManager::Config config;
        config.retransmission_config.max_retries = 2;
        config.retransmission_config.retry_timeout_ms = 50;
        manager.set_config(config);

        // Always fail
        mock_conn.set_failure_mode(true);

        std::vector<uint8_t> test_data(512, 0x42);
        auto result = manager.send(test_data);
        REQUIRE_FALSE(result.is_ok());
        REQUIRE(result.error() == "Failed to send fragment after all retries");

        // Verify max retries event was generated
        bool found_max_retries = false;
        for (const auto& event : captured_events) {
            if (event.type == TransmissionManager::RetryEventType::MAX_RETRIES_REACHED) {
                found_max_retries = true;
                break;
            }
        }
        REQUIRE(found_max_retries);

        const auto& stats = manager.get_retry_stats();
        REQUIRE(stats.max_retries_reached > 0);
    }

    SECTION("Exponential backoff") {
        TransmissionManager::Config config;
        config.retransmission_config.max_retries = 3;
        config.retransmission_config.retry_timeout_ms = 100;
        manager.set_config(config);

        std::vector<std::chrono::milliseconds> retry_intervals;
        auto last_time = std::chrono::steady_clock::now();

        manager.set_retry_callback([&](const TransmissionManager::RetryEvent& event) {
            auto now = std::chrono::steady_clock::now();
            if (event.type == TransmissionManager::RetryEventType::RETRY_ATTEMPT) {
                retry_intervals.push_back(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time));
                last_time = now;
            }
        });

        mock_conn.set_failure_mode(true, 3);
        std::vector<uint8_t> test_data(256, 0x42);
        auto result = manager.send(test_data);
        REQUIRE(result.is_ok());

        // Verify exponential backoff pattern
        REQUIRE(retry_intervals.size() >= 2);
        for (size_t i = 1; i < retry_intervals.size(); i++) {
            // Each interval should be roughly double the previous (accounting for jitter)
            auto ratio = retry_intervals[i].count() / static_cast<double>(retry_intervals[i-1].count());
            REQUIRE(ratio > 1.5); // Allow for jitter
            REQUIRE(ratio < 2.5); // Allow for jitter
        }
    }

    SECTION("Retry statistics") {
        manager.reset_retry_stats();
        TransmissionManager::Config config;
        config.retransmission_config.max_retries = 5;
        config.retransmission_config.retry_timeout_ms = 50;
        manager.set_config(config);

        // Set up varying failure patterns
        mock_conn.set_failure_mode(true, 2); // Fail first 2 attempts
        std::vector<uint8_t> test_data(128, 0x42);
        
        auto result = manager.send(test_data);
        REQUIRE(result.is_ok());

        const auto& stats = manager.get_retry_stats();
        REQUIRE(stats.total_retries > 0);
        REQUIRE(stats.successful_retries > 0);
        REQUIRE(stats.failed_retries == 2);
        REQUIRE(stats.avg_retry_latency_ms > 0);
        
        // Verify retry distribution
        bool has_distribution = false;
        for (const auto& [attempt, count] : stats.retry_distribution) {
            if (count > 0) {
                has_distribution = true;
                break;
            }
        }
        REQUIRE(has_distribution);
    }
}

// Helper class for testing retry behavior
class RetryTestConnectionManager : public MockConnectionManager {
public:
    void set_retry_pattern(std::vector<bool> pattern) {
        retry_pattern_ = std::move(pattern);
        current_index_ = 0;
    }

    Result<void> send(const std::vector<uint8_t>& data) override {
        if (current_index_ >= retry_pattern_.size()) {
            return Result<void>::ok();
        }

        if (retry_pattern_[current_index_++]) {
            return Result<void>::error("Simulated failure");
        }

        return Result<void>::ok();
    }

private:
    std::vector<bool> retry_pattern_;
    size_t current_index_ = 0;
};

TEST_CASE("TransmissionManager complex retry scenarios", "[transmission_manager]") {
    RetryTestConnectionManager test_conn;
    TransmissionManager manager(test_conn);
    
    SECTION("Variable retry patterns") {
        TransmissionManager::Config config;
        config.retransmission_config.max_retries = 4;
        config.retransmission_config.retry_timeout_ms = 50;
        manager.set_config(config);

        std::vector<std::tuple<std::vector<bool>, bool>> test_cases = {
            {{true, false}, true},                    // Fail once, then succeed
            {{true, true, false}, true},              // Fail twice, then succeed
            {{true, true, true, true, true}, false},  // Always fail
            {{false}, true},                          // Immediate success
            {{true, true, true, false}, true}         // Fail thrice, then succeed
        };

        for (const auto& [pattern, should_succeed] : test_cases) {
            test_conn.set_retry_pattern(pattern);
            manager.reset_retry_stats();

            std::vector<uint8_t> test_data(64, 0x42);
            auto result = manager.send(test_data);

            if (should_succeed) {
                REQUIRE(result.is_ok());
            } else {
                REQUIRE_FALSE(result.is_ok());
            }

            const auto& stats = manager.get_retry_stats();
            REQUIRE(stats.total_retries == pattern.size() - 1);
        }
    }
} 
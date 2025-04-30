#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <pybind11/functional.h>
#include "xenocomm/core/transmission_manager.h"

namespace py = pybind11;
using namespace xenocomm::core;

void init_transmission_manager(py::module& m) {
    // Bind ErrorCorrectionMode enum
    py::enum_<TransmissionManager::ErrorCorrectionMode>(m, "ErrorCorrectionMode")
        .value("NONE", TransmissionManager::ErrorCorrectionMode::NONE)
        .value("CHECKSUM_ONLY", TransmissionManager::ErrorCorrectionMode::CHECKSUM_ONLY)
        .value("REED_SOLOMON", TransmissionManager::ErrorCorrectionMode::REED_SOLOMON)
        .export_values();

    // Bind RetryEventType enum
    py::enum_<TransmissionManager::RetryEventType>(m, "RetryEventType")
        .value("RETRY_ATTEMPT", TransmissionManager::RetryEventType::RETRY_ATTEMPT)
        .value("RETRY_SUCCESS", TransmissionManager::RetryEventType::RETRY_SUCCESS)
        .value("RETRY_FAILURE", TransmissionManager::RetryEventType::RETRY_FAILURE)
        .value("MAX_RETRIES_REACHED", TransmissionManager::RetryEventType::MAX_RETRIES_REACHED)
        .export_values();

    // Bind FragmentConfig struct
    py::class_<TransmissionManager::FragmentConfig>(m, "FragmentConfig")
        .def(py::init<>())
        .def_readwrite("max_fragment_size", &TransmissionManager::FragmentConfig::max_fragment_size)
        .def_readwrite("reassembly_timeout_ms", &TransmissionManager::FragmentConfig::reassembly_timeout_ms)
        .def_readwrite("max_fragments", &TransmissionManager::FragmentConfig::max_fragments)
        .def_readwrite("fragment_buffer_size", &TransmissionManager::FragmentConfig::fragment_buffer_size);

    // Bind RetransmissionConfig struct
    py::class_<TransmissionManager::RetransmissionConfig>(m, "RetransmissionConfig")
        .def(py::init<>())
        .def_readwrite("max_retries", &TransmissionManager::RetransmissionConfig::max_retries)
        .def_readwrite("retry_timeout_ms", &TransmissionManager::RetransmissionConfig::retry_timeout_ms)
        .def_readwrite("ack_timeout_ms", &TransmissionManager::RetransmissionConfig::ack_timeout_ms);

    // Bind FlowControlConfig struct
    py::class_<TransmissionManager::FlowControlConfig>(m, "FlowControlConfig")
        .def(py::init<>())
        .def_readwrite("initial_window_size", &TransmissionManager::FlowControlConfig::initial_window_size)
        .def_readwrite("min_window_size", &TransmissionManager::FlowControlConfig::min_window_size)
        .def_readwrite("max_window_size", &TransmissionManager::FlowControlConfig::max_window_size)
        .def_readwrite("rtt_smoothing_factor", &TransmissionManager::FlowControlConfig::rtt_smoothing_factor)
        .def_readwrite("congestion_threshold", &TransmissionManager::FlowControlConfig::congestion_threshold)
        .def_readwrite("backoff_multiplier", &TransmissionManager::FlowControlConfig::backoff_multiplier)
        .def_readwrite("recovery_multiplier", &TransmissionManager::FlowControlConfig::recovery_multiplier)
        .def_readwrite("min_rtt_samples", &TransmissionManager::FlowControlConfig::min_rtt_samples);

    // Bind SecurityConfig struct
    py::class_<SecurityConfig>(m, "SecurityConfig")
        .def(py::init<>())
        .def_readwrite("enable_encryption", &SecurityConfig::enable_encryption)
        .def_readwrite("require_encryption", &SecurityConfig::require_encryption)
        .def_readwrite("verify_hostname", &SecurityConfig::verify_hostname)
        .def_readwrite("expected_hostname", &SecurityConfig::expected_hostname)
        .def_readwrite("security_manager", &SecurityConfig::security_manager);

    // Bind TransmissionStats struct
    py::class_<TransmissionManager::TransmissionStats>(m, "TransmissionStats")
        .def(py::init<>())
        .def_readwrite("bytes_sent", &TransmissionManager::TransmissionStats::bytes_sent)
        .def_readwrite("bytes_received", &TransmissionManager::TransmissionStats::bytes_received)
        .def_readwrite("packets_sent", &TransmissionManager::TransmissionStats::packets_sent)
        .def_readwrite("packets_received", &TransmissionManager::TransmissionStats::packets_received)
        .def_readwrite("retransmissions", &TransmissionManager::TransmissionStats::retransmissions)
        .def_readwrite("current_rtt_ms", &TransmissionManager::TransmissionStats::current_rtt_ms)
        .def_readwrite("avg_rtt_ms", &TransmissionManager::TransmissionStats::avg_rtt_ms)
        .def_readwrite("min_rtt_ms", &TransmissionManager::TransmissionStats::min_rtt_ms)
        .def_readwrite("max_rtt_ms", &TransmissionManager::TransmissionStats::max_rtt_ms)
        .def_readwrite("current_window_size", &TransmissionManager::TransmissionStats::current_window_size)
        .def_readwrite("packet_loss_count", &TransmissionManager::TransmissionStats::packet_loss_count)
        .def_readwrite("is_encrypted", &TransmissionManager::TransmissionStats::is_encrypted)
        .def_readwrite("cipher_suite", &TransmissionManager::TransmissionStats::cipher_suite)
        .def_readwrite("protocol_version", &TransmissionManager::TransmissionStats::protocol_version)
        .def_readwrite("peer_certificate_info", &TransmissionManager::TransmissionStats::peer_certificate_info);

    // Bind Config struct
    py::class_<TransmissionManager::Config>(m, "TransmissionConfig")
        .def(py::init<>())
        .def_readwrite("error_correction_mode", &TransmissionManager::Config::error_correction_mode)
        .def_readwrite("fragment_config", &TransmissionManager::Config::fragment_config)
        .def_readwrite("retransmission_config", &TransmissionManager::Config::retransmission_config)
        .def_readwrite("flow_control", &TransmissionManager::Config::flow_control)
        .def_readwrite("security", &TransmissionManager::Config::security)
        .def_readwrite("retry_attempts", &TransmissionManager::Config::retry_attempts)
        .def_readwrite("enable_logging", &TransmissionManager::Config::enable_logging);

    // Bind RetryEvent struct
    py::class_<TransmissionManager::RetryEvent>(m, "RetryEvent")
        .def(py::init<>())
        .def_readwrite("type", &TransmissionManager::RetryEvent::type)
        .def_readwrite("transmission_id", &TransmissionManager::RetryEvent::transmission_id)
        .def_readwrite("fragment_index", &TransmissionManager::RetryEvent::fragment_index)
        .def_readwrite("attempt_number", &TransmissionManager::RetryEvent::attempt_number)
        .def_readwrite("error_message", &TransmissionManager::RetryEvent::error_message)
        .def_readwrite("timestamp", &TransmissionManager::RetryEvent::timestamp);

    // Bind TransmissionManager class
    py::class_<TransmissionManager>(m, "TransmissionManager")
        .def(py::init<ConnectionManager&>())
        .def("send", &TransmissionManager::send)
        .def("receive", &TransmissionManager::receive,
            py::arg("timeout_ms") = 1000)
        .def("set_config", &TransmissionManager::set_config)
        .def("get_config", &TransmissionManager::get_config)
        .def("get_stats", &TransmissionManager::get_stats)
        .def("reset_stats", &TransmissionManager::reset_stats)
        .def("wait_for_window_space", &TransmissionManager::wait_for_window_space)
        .def("release_window_space", &TransmissionManager::release_window_space)
        .def("set_retry_callback", &TransmissionManager::set_retry_callback)
        .def("reset_retry_stats", &TransmissionManager::reset_retry_stats)
        .def("get_security_status", &TransmissionManager::get_security_status)
        .def("renegotiate_security", &TransmissionManager::renegotiate_security)
        .def("setup_secure_channel", &TransmissionManager::setup_secure_channel);
} 
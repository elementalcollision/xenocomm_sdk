import pytest
import numpy as np
from xenocomm import (
    TransmissionManager, ConnectionManager, ErrorCorrectionMode,
    RetryEventType, FragmentConfig, RetransmissionConfig,
    FlowControlConfig, SecurityConfig, TransmissionStats,
    TransmissionConfig, RetryEvent
)

def test_transmission_config():
    # Test default configuration
    config = TransmissionConfig()
    assert config.error_correction_mode == ErrorCorrectionMode.CHECKSUM_ONLY
    assert config.retry_attempts == 3
    assert config.enable_logging is True
    
    # Test fragment config
    fragment_config = FragmentConfig()
    assert fragment_config.max_fragment_size == 1024
    assert fragment_config.reassembly_timeout_ms == 5000
    assert fragment_config.max_fragments == 65535
    assert fragment_config.fragment_buffer_size == 1024 * 1024
    
    # Test retransmission config
    retrans_config = RetransmissionConfig()
    assert retrans_config.max_retries == 3
    assert retrans_config.retry_timeout_ms == 1000
    assert retrans_config.ack_timeout_ms == 500
    
    # Test flow control config
    flow_config = FlowControlConfig()
    assert flow_config.initial_window_size == 65535
    assert flow_config.min_window_size == 1024
    assert flow_config.max_window_size == 1048576

def test_transmission_manager_basic():
    conn_manager = ConnectionManager()
    manager = TransmissionManager(conn_manager)
    
    # Test configuration
    config = TransmissionConfig()
    config.error_correction_mode = ErrorCorrectionMode.REED_SOLOMON
    manager.set_config(config)
    
    current_config = manager.get_config()
    assert current_config.error_correction_mode == ErrorCorrectionMode.REED_SOLOMON
    
    # Test stats
    stats = manager.get_stats()
    assert isinstance(stats, TransmissionStats)
    assert stats.bytes_sent == 0
    assert stats.bytes_received == 0
    
    # Test reset stats
    manager.reset_stats()
    stats = manager.get_stats()
    assert stats.bytes_sent == 0

def test_transmission_retry_callback():
    conn_manager = ConnectionManager()
    manager = TransmissionManager(conn_manager)
    
    retry_events = []
    def retry_callback(event: RetryEvent):
        retry_events.append(event)
    
    manager.set_retry_callback(retry_callback)
    
    # Test sending data that triggers retries
    data = bytes([1, 2, 3, 4])
    result = manager.send(data)
    
    # Verify retry events were captured
    assert len(retry_events) > 0
    event = retry_events[0]
    assert isinstance(event.type, RetryEventType)
    assert event.transmission_id > 0
    assert event.attempt_number > 0

def test_transmission_security():
    conn_manager = ConnectionManager()
    manager = TransmissionManager(conn_manager)
    
    # Configure security
    config = manager.get_config()
    config.security.enable_encryption = True
    config.security.require_encryption = True
    manager.set_config(config)
    
    # Test secure channel setup
    result = manager.setup_secure_channel()
    assert result.is_ok()
    
    # Test security status
    status = manager.get_security_status()
    assert isinstance(status, str)
    assert len(status) > 0
    
    # Test secure data transmission
    data = bytes([1, 2, 3, 4])
    result = manager.send(data)
    assert result.is_ok()
    
    received = manager.receive(timeout_ms=1000)
    assert received.is_ok()
    if received.is_ok():
        assert received.value == data

def test_flow_control():
    conn_manager = ConnectionManager()
    manager = TransmissionManager(conn_manager)
    
    # Test window space management
    large_data_size = 1024 * 1024  # 1MB
    result = manager.wait_for_window_space(large_data_size, timeout_ms=1000)
    assert result.is_ok()
    
    # Release window space
    manager.release_window_space(large_data_size)
    
    # Verify stats
    stats = manager.get_stats()
    assert isinstance(stats.current_window_size, int) 
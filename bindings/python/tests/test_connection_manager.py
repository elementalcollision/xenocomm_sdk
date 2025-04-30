import pytest
from xenocomm import ConnectionManager, ConnectionConfig, ConnectionStatus

def test_connection_config():
    # Test default configuration
    config = ConnectionConfig()
    assert config.timeout.total_seconds() == 5.0  # Default 5 seconds
    assert config.auto_reconnect is True
    assert config.max_retries == 3
    assert config.retry_delay.total_seconds() == 1.0  # Default 1 second

    # Test string representation
    assert str(config).startswith("ConnectionConfig(")

def test_connection_manager_basic():
    manager = ConnectionManager()
    
    # Test context manager
    with manager as mgr:
        assert isinstance(mgr, ConnectionManager)
        
        # Test establishing a connection
        conn = mgr.establish("test_conn")
        assert conn is not None
        
        # Test getting connection status
        status = mgr.check_status("test_conn")
        assert status in [ConnectionStatus.CONNECTED, ConnectionStatus.CONNECTING]
        
        # Test getting connection by ID
        retrieved_conn = mgr.get_connection("test_conn")
        assert retrieved_conn.get_id() == "test_conn"
        
        # Test getting active connections
        active_conns = mgr.get_active_connections()
        assert len(active_conns) == 1
        assert active_conns[0].get_id() == "test_conn"
        
        # Test closing a connection
        assert mgr.close("test_conn") is True
        
        # Test string representation
        assert str(mgr).startswith("ConnectionManager(")

def test_connection_manager_errors():
    manager = ConnectionManager()
    
    # Test getting non-existent connection
    with pytest.raises(Exception):  # Specific exception type from C++
        manager.get_connection("nonexistent")
    
    # Test checking status of non-existent connection
    with pytest.raises(Exception):  # Specific exception type from C++
        manager.check_status("nonexistent")
    
    # Test closing non-existent connection
    assert manager.close("nonexistent") is False

def test_connection_manager_multiple():
    manager = ConnectionManager()
    
    # Create multiple connections
    conn1 = manager.establish("conn1")
    conn2 = manager.establish("conn2", ConnectionConfig())
    
    # Verify both connections are active
    active_conns = manager.get_active_connections()
    assert len(active_conns) == 2
    conn_ids = {conn.get_id() for conn in active_conns}
    assert conn_ids == {"conn1", "conn2"}
    
    # Close one connection
    assert manager.close("conn1") is True
    
    # Verify only one connection remains
    active_conns = manager.get_active_connections()
    assert len(active_conns) == 1
    assert active_conns[0].get_id() == "conn2"

def test_connection_context_cleanup():
    manager = ConnectionManager()
    
    # Create connections inside context
    with manager:
        conn1 = manager.establish("conn1")
        conn2 = manager.establish("conn2")
        assert len(manager.get_active_connections()) == 2
    
    # Verify all connections are closed after context exit
    with pytest.raises(Exception):  # Specific exception type from C++
        manager.check_status("conn1")
    with pytest.raises(Exception):  # Specific exception type from C++
        manager.check_status("conn2") 
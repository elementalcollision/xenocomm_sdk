import pytest
from xenocomm import CapabilitySignaler, Capability, CapabilityVersion, Version

def test_capability_version():
    # Test initialization
    ver = CapabilityVersion(1, 2, 3)
    assert ver.major == 1
    assert ver.minor == 2
    assert ver.patch == 3
    
    # Test comparison operators
    ver1 = CapabilityVersion(1, 0, 0)
    ver2 = CapabilityVersion(2, 0, 0)
    assert ver1 < ver2
    assert ver1 == CapabilityVersion(1, 0, 0)
    
    # Test string representation
    assert str(ver).startswith("CapabilityVersion(")

def test_capability():
    # Test basic initialization
    cap = Capability("test_cap", Version(1, 0, 0))
    assert cap.name == "test_cap"
    assert cap.version.major == 1
    assert not cap.is_deprecated
    
    # Test with parameters
    params = {"param1": "value1", "param2": "value2"}
    cap = Capability("test_cap", Version(1, 0, 0), params)
    assert cap.parameters == params
    
    # Test deprecation
    cap.deprecate(Version(2, 0, 0), Version(3, 0, 0), "new_cap")
    assert cap.is_deprecated
    assert cap.deprecated_since == Version(2, 0, 0)
    assert cap.removal_version == Version(3, 0, 0)
    assert cap.replacement_capability == "new_cap"
    
    # Test matching
    cap1 = Capability("test_cap", Version(2, 0, 0), {"required": "yes"})
    cap2 = Capability("test_cap", Version(2, 1, 0), {"required": "yes", "optional": "no"})
    
    # Exact matching
    assert cap2.matches(cap1, allow_partial=False)
    
    # Partial matching
    cap3 = Capability("test_cap", Version(1, 0, 0), {"required": "yes"})
    assert not cap2.matches(cap3, allow_partial=False)
    assert cap2.matches(cap3, allow_partial=True)
    
    # Test string representation
    assert str(cap).startswith("Capability(")

def test_capability_signaler():
    signaler = CapabilitySignaler()
    
    # Create test capabilities
    cap1 = Capability("cap1", Version(1, 0, 0), {"type": "sensor"})
    cap2 = Capability("cap2", Version(2, 0, 0), {"type": "actuator"})
    
    # Test registration
    assert signaler.register_capability("agent1", cap1)
    assert signaler.register_capability("agent1", cap2)
    assert signaler.register_capability("agent2", cap1)
    
    # Test discovery with exact matching
    agents = signaler.discover_agents([cap1])
    assert "agent1" in agents
    assert "agent2" in agents
    
    agents = signaler.discover_agents([cap1, cap2])
    assert "agent1" in agents
    assert "agent2" not in agents
    
    # Test discovery with partial matching
    cap1_v2 = Capability("cap1", Version(2, 0, 0), {"type": "sensor"})
    agents = signaler.discover_agents([cap1_v2], partial_match=True)
    assert len(agents) > 0
    
    # Test getting agent capabilities
    caps = signaler.get_agent_capabilities("agent1")
    assert len(caps) == 2
    assert any(c.name == "cap1" for c in caps)
    assert any(c.name == "cap2" for c in caps)
    
    # Test unregistration
    assert signaler.unregister_capability("agent1", cap1)
    caps = signaler.get_agent_capabilities("agent1")
    assert len(caps) == 1
    assert caps[0].name == "cap2"
    
    # Test binary registration and retrieval
    binary_data = bytes([1, 2, 3, 4])  # Example binary capability data
    assert signaler.register_capability_binary("agent3", binary_data)
    retrieved_data = signaler.get_agent_capabilities_binary("agent3")
    assert retrieved_data == binary_data

def test_capability_signaler_errors():
    signaler = CapabilitySignaler()
    cap = Capability("test_cap", Version(1, 0, 0))
    
    # Test unregistering non-existent capability
    assert not signaler.unregister_capability("nonexistent", cap)
    
    # Test getting capabilities for non-existent agent
    assert len(signaler.get_agent_capabilities("nonexistent")) == 0
    
    # Test getting binary capabilities for non-existent agent
    assert len(signaler.get_agent_capabilities_binary("nonexistent")) == 0 
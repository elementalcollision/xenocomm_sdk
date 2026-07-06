import logging
import pytest
from xenocomm import BoundaryGateway, BoundaryGatewayError

class DummyGateway(BoundaryGateway):
    def send_request(self, *args, **kwargs):
        return "sent"
    def receive_response(self, *args, **kwargs):
        return "received"

def test_instantiation_and_config():
    gw = DummyGateway(config={"foo": "bar"})
    assert gw.config["foo"] == "bar"
    gw.load_config({"baz": 42})
    assert gw.config["baz"] == 42

def test_logging_and_lifecycle(caplog):
    gw = DummyGateway()
    with caplog.at_level(logging.INFO):
        gw.log("test log message")
        assert any("test log message" in r.message for r in caplog.records)
    gw.initialize()
    assert gw.initialized
    gw.shutdown()
    assert not gw.initialized

def test_adapter_registry():
    gw = DummyGateway()
    gw.register_adapter("foo", object())
    assert "foo" in gw.adapters
    adapter = gw.get_adapter("foo")
    assert adapter is not None
    with pytest.raises(BoundaryGatewayError):
        gw.get_adapter("bar")

def test_context_manager():
    gw = DummyGateway()
    with gw as g2:
        assert g2.initialized
    assert not gw.initialized

def test_abstract_methods_enforced():
    class IncompleteGateway(BoundaryGateway):
        pass
    with pytest.raises(TypeError):
        IncompleteGateway() 
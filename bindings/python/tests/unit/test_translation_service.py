import pytest
from xenocomm.translation_service import TranslationService, TranslationError

class Dummy:
    def __init__(self, id, name):
        self.id = id
        self.name = name

def test_json_serialization():
    svc = TranslationService()
    data = {"foo": 1, "bar": [2, 3]}
    json_str = svc.to_json(data)
    assert isinstance(json_str, str)
    assert svc.from_json(json_str) == data

def test_xml_serialization():
    svc = TranslationService()
    data = {"root": {"foo": 1, "bar": [2, 3]}}
    try:
        xml_str = svc.to_xml(data)
        assert isinstance(xml_str, str)
        parsed = svc.from_xml(xml_str)
        assert "root" in parsed
    except ImportError:
        pytest.skip("xmltodict not installed")

@pytest.mark.parametrize("valid,expected", [
    (True, None),
    (False, "Validation error at 'foo'")
])
def test_json_schema_validation(valid, expected):
    svc = TranslationService()
    try:
        schema = {"type": "object", "properties": {"foo": {"type": "number"}}, "required": ["foo"]}
        data = {"foo": 1} if valid else {"foo": "bad"}
        ok, err = svc.validate_json(data, schema)
        assert ok is valid
        if not valid:
            assert expected in err
    except ImportError:
        pytest.skip("jsonschema not installed")

def test_template_formatting():
    svc = TranslationService()
    data = {"name": "Alice", "value": 42}
    template = "Hello {{ name }}, value={{ value }}"
    try:
        result = svc.format_template(data, template)
        assert "Alice" in result and "42" in result
    except ImportError:
        pytest.skip("jinja2 not installed")

def test_type_mapping_and_hooks():
    svc = TranslationService()
    # Register mapping for Dummy <-> dict
    svc.register_type_mapping(
        "Dummy", "dict",
        lambda d: {"id": d.id, "name": d.name},
        lambda d: Dummy(d["id"], d["name"])
    )
    # Add pre and post hooks
    svc.add_custom_hook("Dummy", "outbound", "pre", lambda d: Dummy(d.id + 1, d.name.upper()))
    svc.add_custom_hook("dict", "outbound", "post", lambda d: {**d, "extra": True})
    dummy = Dummy(1, "alice")
    result = svc.translate(dummy, "Dummy", "dict", direction="outbound")
    assert result["id"] == 2 and result["name"] == "ALICE" and result["extra"] is True
    # Inbound
    inbound = svc.translate({"id": 5, "name": "BOB"}, "Dummy", "dict", direction="inbound")
    assert isinstance(inbound, Dummy) and inbound.id == 5 and inbound.name == "BOB"

def test_missing_mapping_raises():
    svc = TranslationService()
    with pytest.raises(TranslationError):
        svc.translate({"foo": 1}, "A", "B", direction="outbound") 
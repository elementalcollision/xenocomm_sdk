"""
TranslationService: Data translation, validation, and formatting utilities for BoundaryGateway.

Features:
- JSON and XML serialization/deserialization (requires xmltodict for XML)
- JSON schema validation (requires jsonschema)
- Template-based formatting (requires jinja2)
- Type mapping registry for bidirectional translation between internal and external types
- Custom pre/post hooks for translation pipelines
- Comprehensive error handling with context
"""
from typing import Any, Dict, Callable, Optional
import json
import xml.etree.ElementTree as ET

class TranslationError(Exception):
    """Custom exception for translation errors with context."""
    def __init__(self, message: str, source_type: str = None, target_type: str = None, data_sample: Any = None):
        self.source_type = source_type
        self.target_type = target_type
        self.data_sample = data_sample
        super().__init__(f"Translation error ({source_type} → {target_type}): {message}")

class TranslationService:
    """
    Service for translating between internal and external data formats (JSON, XML, etc.),
    performing schema validation, template formatting, and managing type mappings and hooks.

    Methods:
        - to_json, from_json: JSON serialization/deserialization
        - to_xml, from_xml: XML serialization/deserialization (requires xmltodict)
        - validate_json: JSON schema validation (requires jsonschema)
        - format_template: Render data with Jinja2 templates (requires jinja2)
        - register_type_mapping: Register bidirectional type mappings
        - add_custom_hook: Register pre/post hooks for translation
        - translate: Perform translation using mappings and hooks
    """
    def __init__(self):
        self.type_registry: Dict[str, dict] = {}  # type_name -> schema
        self.custom_hooks: Dict[tuple, list] = {}  # (type_name, direction, hook_type) -> list of functions
        self.type_mappings: Dict[tuple, dict] = {}  # (internal_type, external_type) -> {'outbound': func, 'inbound': func}

    def to_json(self, data: Any) -> str:
        """Serialize a Python object to a JSON string."""
        try:
            return json.dumps(data)
        except Exception as e:
            raise TranslationError(f"Failed to serialize to JSON: {e}", source_type=type(data).__name__, target_type="json", data_sample=data)

    def from_json(self, json_str: str) -> Any:
        """Deserialize a JSON string to a Python object."""
        try:
            return json.loads(json_str)
        except Exception as e:
            raise TranslationError(f"Failed to deserialize from JSON: {e}", source_type="json", data_sample=json_str)

    def to_xml(self, data: Any) -> str:
        """Serialize a Python dict to an XML string using xmltodict (requires xmltodict)."""
        try:
            import xmltodict
        except ImportError:
            raise ImportError("xmltodict package is required for XML serialization.")
        try:
            return xmltodict.unparse(data, pretty=True)
        except Exception as e:
            raise TranslationError(f"Failed to serialize to XML: {e}", source_type=type(data).__name__, target_type="xml", data_sample=data)

    def from_xml(self, xml_str: str) -> Any:
        """Deserialize an XML string to a Python dict using xmltodict (requires xmltodict)."""
        try:
            import xmltodict
        except ImportError:
            raise ImportError("xmltodict package is required for XML deserialization.")
        try:
            return xmltodict.parse(xml_str)
        except Exception as e:
            raise TranslationError(f"Failed to deserialize from XML: {e}", source_type="xml", data_sample=xml_str)

    def register_type(self, type_name: str, schema: dict):
        """Register a schema for a type name."""
        self.type_registry[type_name] = schema

    def get_schema(self, type_name: str) -> Optional[dict]:
        """Get the registered schema for a type name, or None if not found."""
        return self.type_registry.get(type_name)

    def validate_json(self, data: Any, schema: dict) -> tuple[bool, Optional[str]]:
        """
        Validate data against a JSON schema (requires jsonschema).
        Returns (True, None) if valid, (False, error_message) if invalid.
        """
        try:
            import jsonschema
        except ImportError:
            raise ImportError("jsonschema package is required for JSON schema validation.")
        try:
            jsonschema.validate(instance=data, schema=schema)
            return True, None
        except jsonschema.exceptions.ValidationError as e:
            path = " → ".join(str(p) for p in e.path)
            return False, f"Validation error at '{path}': {e.message}"

    def format_template(self, data: Any, template_str: str) -> str:
        """
        Render a human-readable output using a Jinja2 template string and the provided data (requires jinja2).
        """
        try:
            import jinja2
        except ImportError:
            raise ImportError("jinja2 package is required for template-based formatting.")
        try:
            template = jinja2.Template(template_str)
            return template.render(**data)
        except Exception as e:
            raise TranslationError(f"Failed to render template: {e}", source_type=type(data).__name__, target_type="template", data_sample=data)

    def register_type_mapping(self, internal_type: str, external_type: str, internal_to_external: Callable, external_to_internal: Callable) -> None:
        """
        Register bidirectional mapping between internal and external types.
        internal_to_external: function to convert internal to external
        external_to_internal: function to convert external to internal
        """
        key = (internal_type, external_type)
        self.type_mappings[key] = {
            'outbound': internal_to_external,
            'inbound': external_to_internal
        }

    def add_custom_hook(self, type_name: str, direction: str, hook_type: str, func: Callable) -> None:
        """
        Add a custom hook for data transformation.
        hook_type: 'pre' or 'post' to specify when the hook should run.
        direction: 'outbound' or 'inbound'.
        func: the hook function to apply
        """
        key = (type_name, direction, hook_type)
        if key not in self.custom_hooks:
            self.custom_hooks[key] = []
        self.custom_hooks[key].append(func)

    def _run_hooks(self, type_name: str, direction: str, hook_type: str, data: Any) -> Any:
        """Run all hooks for a given type, direction, and hook_type ('pre' or 'post')."""
        key = (type_name, direction, hook_type)
        hooks = self.custom_hooks.get(key, [])
        for hook in hooks:
            data = hook(data)
        return data

    def translate(self, data: Any, from_type: str, to_type: str, direction: str = 'outbound') -> Any:
        """
        Translate data between internal and external types using registered mappings and hooks.
        direction: 'outbound' (internal->external) or 'inbound' (external->internal)
        Runs pre and post hooks if registered.
        Raises TranslationError if no mapping is found.
        """
        # Run pre-translation hooks
        data = self._run_hooks(from_type, direction, 'pre', data)
        key = (from_type, to_type)
        if key not in self.type_mappings or direction not in self.type_mappings[key]:
            raise TranslationError(f"No mapping registered for {from_type} → {to_type} ({direction})", from_type, to_type, data)
        func = self.type_mappings[key][direction]
        result = func(data)
        # Run post-translation hooks
        result = self._run_hooks(to_type, direction, 'post', result)
        return result 
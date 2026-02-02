# XenoComm SDK

A high-performance communication framework enabling AI agents to communicate over IP networks with maximum computational efficiency.

**Now with MCP Protocol Bridge** — Bring XenoComm's coordination capabilities to Claude Code, Moltbot/OpenClaw, Cursor, and any MCP-compatible client.

## Overview

XenoComm SDK is designed to provide a low-latency, resource-efficient communication layer for AI agents running in cloud environments. The framework prioritizes machine-optimized interaction over human readability, enabling the potential emergence of adaptive, machine-optimized communication protocols.

## 🚀 Quick Start: MCP Server

The fastest way to use XenoComm is via the MCP (Model Context Protocol) server:

```bash
# Clone and install
git clone https://github.com/elementalcollision/xenocomm_sdk.git
cd xenocomm_sdk/mcp_server
pip install -r requirements.txt

# Run (stdio for Claude Code, etc.)
python -m xenocomm_mcp

# Or HTTP transport
python -m xenocomm_mcp --transport http --port 8000
```

### Integrate with Claude

**Option 1: Project-level configuration**

The repository includes a `.mcp.json` file. When you open the project in Claude Code, it will automatically detect and offer to enable the XenoComm MCP server.

**Option 2: Global Claude configuration**

Add to your `~/.claude.json` or Claude Desktop config:

```json
{
  "mcpServers": {
    "xenocomm": {
      "command": "python",
      "args": ["-m", "xenocomm_mcp"],
      "cwd": "/path/to/xenocomm_sdk/mcp_server",
      "env": {
        "PYTHONPATH": "/path/to/xenocomm_sdk/mcp_server"
      }
    }
  }
}
```

**Option 3: Using uvx (recommended for installed packages)**

```json
{
  "mcpServers": {
    "xenocomm": {
      "command": "uvx",
      "args": ["xenocomm-mcp"]
    }
  }
}
```

Once integrated, Claude will have access to 40+ XenoComm tools for agent coordination, including alignment verification, protocol negotiation, A/B testing, and workflow orchestration.

See the [MCP Server documentation](mcp_server/README.md) for full details.

## Key Features

- **🔌 MCP Protocol Bridge**: Expose coordination capabilities to the agentic AI ecosystem (Claude Code, Moltbot, Cursor)
- **🤝 Agent Alignment Verification**: 5 formal strategies (knowledge, goals, terminology, assumptions, context) for establishing mutual understanding
- **📋 Dynamic Protocol Negotiation**: State machine for agents to agree on communication parameters
- **🧬 Protocol Evolution**: EmergenceManager with canary deployments, circuit breakers, and safe rollback
- **Optimized Network Connections**: Direct TCP/UDP streams with connection pooling and async I/O support
- **Efficient Capability Discovery**: Binary-optimized capability representation with fast indexing and caching
- **Direct Data Representation**: Multiple optimized encoding formats including VECTOR_FLOAT32, VECTOR_INT8, and GGWAVE_FSK
- **Robust Error Handling**: Configurable error detection, recovery strategies, and retry logic
- **Feedback-Driven Optimization**: Built-in performance monitoring and protocol adaptation
- **Security First**: Integrated encryption and authentication with minimal overhead
- **Language Support**: Core C++ implementation with Python bindings and MCP server
- **Data Translation & Validation**: Built-in TranslationService for JSON/XML serialization, schema validation, template formatting, and type mapping (Python)

## Getting Started

### Prerequisites

- C++17 compatible compiler
- CMake 3.15 or newer
- Python 3.8+ (for Python bindings)
- OpenSSL development libraries
- zlib development libraries

### Building from Source

```bash
# Clone the repository
git clone https://github.com/elementalcollision/xenocomm_sdk.git
cd xenocomm_sdk

# Configure with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cd build

# Build
cmake --build .

# Run tests
ctest --output-on-failure
```

### Basic Usage

```cpp
#include <xenocomm/connection_manager.hpp>
#include <xenocomm/capability_signaler.hpp>
#include <xenocomm/negotiation_protocol.hpp>
#include <xenocomm/data_transcoder.hpp>

// Initialize connection manager with async support
auto conn_mgr = xenocomm::ConnectionManager(xenocomm::AsyncConfig{
    .pool_size = 10,
    .event_loop_threads = 2
});

// Register agent capabilities with efficient binary encoding
auto signaler = xenocomm::CapabilitySignaler();
signaler.register_capabilities(agent_id, capabilities);

// Establish connection with automatic protocol negotiation
auto negotiator = xenocomm::NegotiationProtocol();
auto conn = conn_mgr.establish_connection(target_agent_id, negotiator);

// Use optimized data encoding
auto transcoder = xenocomm::DataTranscoder();
auto encoded_data = transcoder.encode(data, xenocomm::Format::VECTOR_FLOAT32);
conn.send(encoded_data);
```

### Advanced Features

#### Protocol Evolution

```cpp
#include <xenocomm/extensions/emergence_manager.hpp>

// Initialize EmergenceManager with safeguards
auto emergence_mgr = xenocomm::extensions::EmergenceManager({
    .rollback_points = 5,
    .state_chunk_size = 50 * 1024 * 1024,  // 50MB chunks for large states
    .btree_order = 128                      // Optimized for fast lookups
});

// Propose and track protocol variants
auto variant = emergence_mgr.proposeVariant(new_protocol_config);
emergence_mgr.trackPerformance(variant.id, metrics);

// Automatic rollback on issues
if (emergence_mgr.shouldRollback(variant.id)) {
    emergence_mgr.rollbackToLastStable();
}
```

## Extensibility: Custom Strategies & Plugins

The XenoComm SDK provides a powerful extensibility subsystem for building, composing, loading, and testing custom alignment strategies. This enables users to:
- Define new strategies using a fluent builder API
- Compose strategies with sequence, parallel, fallback, and conditional logic
- Dynamically load external strategy plugins at runtime
- Implement configurable strategies using templates
- Test and validate strategies with a dedicated harness

**Main Components:**
- `StrategyBuilder`: Fluent API for building custom strategies
- `StrategyComposer`: Static methods for composing strategies
- `PluginLoader`: Dynamic loading and management of external strategy plugins
- `StrategyTemplate`: Base template for implementing configurable strategies
- `StrategyTestHarness`: Tools for testing and validating strategies

**Example Usage:**

```cpp
#include <xenocomm/extensions/common_ground/extensibility/strategy_builder.hpp>
#include <xenocomm/extensions/common_ground/extensibility/strategy_composer.hpp>
#include <xenocomm/extensions/common_ground/extensibility/plugin_loader.hpp>
#include <xenocomm/extensions/common_ground/extensibility/testing_harness.hpp>

using namespace xenocomm::common_ground;

// Build a custom strategy
auto myStrategy = StrategyBuilder()
    .withId("custom1")
    .withName("Custom Strategy")
    .withDescription("Checks custom alignment condition.")
    .withVerificationLogic([](const AlignmentContext& ctx) {
        // Custom logic here
        return AlignmentResult(true, {}, 1.0);
    })
    .build();

// Register with the framework
framework.registerStrategy(myStrategy);

// Compose strategies
auto composed = StrategyComposer::sequence({myStrategy, otherStrategy});

// Load plugins
auto loader = PluginLoader("./plugins");
loader.loadPlugin("my_plugin.so");

// Test a strategy
StrategyTestHarness harness(myStrategy);
harness.withContext(ctx);
auto result = harness.runTest();
```

For more details, see the [Doxygen API documentation](docs/api/README.md#extensibility-subsystem).

## MCP Protocol Bridge

The XenoComm MCP Server exposes 22 tools for agent-to-agent coordination, compatible with any MCP client.

### Alignment Tools

Verify that two agents can effectively collaborate:

```python
# Register agents with their context
register_agent(
    agent_id="researcher",
    knowledge_domains=["python", "machine_learning"],
    goals=[{"type": "analysis", "description": "Analyze data"}],
    terminology={"model": "trained neural network"},
    assumptions=["GPU available"],
    context_params={"environment": "research"}
)

# Run comprehensive alignment check
result = full_alignment_check("researcher", "writer")
# Returns: overall_status, confidence, and recommendations
```

**Available alignment tools:**
- `verify_knowledge_alignment` - Check shared knowledge domains
- `verify_goal_alignment` - Check goal compatibility
- `align_terminology` - Ensure consistent term definitions
- `verify_assumptions` - Surface conflicting assumptions
- `sync_context` - Align environmental parameters

### Negotiation Tools

Dynamic protocol negotiation between agents:

```python
# Agent A initiates
session = initiate_negotiation("agent-a", "agent-b", {
    "data_format": "json",
    "compression": "gzip"
})

# Agent B counters
respond_to_negotiation(session_id, "agent-b", "counter", {
    "data_format": "msgpack"
})

# Agent A accepts and finalizes
accept_counter_proposal(session_id, "agent-a")
finalize_negotiation(session_id, "agent-a")
```

### Emergence Tools

Safe protocol evolution with canary deployments:

```python
# Propose a variant
variant = propose_protocol_variant("Add streaming", {"streaming": True})

# Progress through pipeline
start_variant_testing(variant_id)
start_canary_deployment(variant_id, initial_percentage=0.1)

# Track metrics and ramp up or rollback
track_variant_performance(variant_id, success_rate=0.99, latency_ms=15)
ramp_canary(variant_id)  # Increases traffic percentage
```

See [mcp_server/README.md](mcp_server/README.md) for complete documentation.

## Documentation

- **[MCP Server Guide](mcp_server/README.md)** - Get started with the MCP Protocol Bridge
- [API Reference](docs/api/README.md)
  - [EmergenceManager API](docs/api/emergence_manager.md)
- [Integration Guide](docs/guides/integration.md)
  - [Protocol Evolution Guide](docs/guides/emergence_manager_guide.md)
- [Examples](examples/README.md)
- [Performance Optimization](docs/guides/performance.md)
- [Protocol Specification](docs/specs/protocol.md)
  - [EmergenceManager Specification](docs/specs/emergence_manager_spec.md)

## Contributing

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details on how to get started.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Project Status

XenoComm SDK is currently in active development.

**v2.0** introduces the MCP Protocol Bridge, bringing XenoComm's coordination capabilities to the agentic AI ecosystem. The MCP server exposes alignment verification, protocol negotiation, and emergence tools compatible with Claude Code, Moltbot/OpenClaw, Cursor, and other MCP clients.

Core features including ConnectionManager, CapabilitySignaler, NegotiationProtocol, DataTranscoder, EmergenceManager, and the new MCP server are implemented and tested.

## Contact

For questions and support:
- [GitHub Issues](https://github.com/elementalcollision/xenocomm_sdk/issues)
- [Discussions](https://github.com/elementalcollision/xenocomm_sdk/discussions)

## Data Translation & Validation (Python)

The Python bindings include a powerful `TranslationService` for converting between internal and external data formats, validating schemas, and generating human-readable outputs.

**Features:**
- JSON and XML serialization/deserialization (requires `xmltodict` for XML)
- JSON schema validation (requires `jsonschema`)
- Template-based formatting (requires `jinja2`)
- Type mapping registry for bidirectional translation between internal and external types
- Custom pre/post hooks for translation pipelines
- Comprehensive error handling with context

**Example Usage:**

```python
from xenocomm.translation_service import TranslationService

svc = TranslationService()

# JSON serialization
obj = {"foo": 1, "bar": [2, 3]}
json_str = svc.to_json(obj)
assert svc.from_json(json_str) == obj

# XML serialization (requires xmltodict)
try:
    xml_str = svc.to_xml({"root": obj})
    parsed = svc.from_xml(xml_str)
except ImportError:
    pass

# JSON schema validation (requires jsonschema)
schema = {"type": "object", "properties": {"foo": {"type": "number"}}, "required": ["foo"]}
ok, err = svc.validate_json(obj, schema)

# Template formatting (requires jinja2)
template = "Hello {{ foo }}, bar={{ bar|join(',') }}"
try:
    result = svc.format_template(obj, template)
except ImportError:
    pass

# Type mapping and translation
class Dummy:
    def __init__(self, id, name):
        self.id = id
        self.name = name
svc.register_type_mapping(
    "Dummy", "dict",
    lambda d: {"id": d.id, "name": d.name},
    lambda d: Dummy(d["id"], d["name"])
)
dummy = Dummy(1, "alice")
dict_result = svc.translate(dummy, "Dummy", "dict", direction="outbound")

# Custom hooks
svc.add_custom_hook("Dummy", "outbound", "pre", lambda d: Dummy(d.id + 1, d.name.upper()))
svc.add_custom_hook("dict", "outbound", "post", lambda d: {**d, "extra": True})
```

## Rate Limiting & Caching (Python)

The Python bindings provide built-in support for rate limiting and response caching via the `RateLimiter` and `CacheManager` classes. These can be used with the REST and GraphQL adapters to control request rates and improve performance.

**Features:**
- Thread-safe token bucket rate limiting (per-endpoint or shared)
- TTL-based response caching with manual/conditional invalidation
- Metrics for monitoring rate limit and cache usage

**Example Usage:**

```python
from xenocomm.rest_adapter import RestApiAdapter
from xenocomm.graphql_adapter import GraphQLAdapter
from xenocomm.rate_limiter import RateLimiter
from xenocomm.cache_manager import CacheManager

# Create shared rate limiter and cache manager
rate_limiter = RateLimiter(capacity=10, refill_rate=2)  # 10 requests max, 2/sec
cache_manager = CacheManager()

# REST adapter with rate limiting and caching
rest = RestApiAdapter(rate_limiter=rate_limiter, cache_manager=cache_manager)
rest.configure("https://api.example.com")
users = rest.execute("users", "GET")  # May be cached and rate-limited

# GraphQL adapter with rate limiting and caching
graphql = GraphQLAdapter(rate_limiter=rate_limiter, cache_manager=cache_manager)
graphql.configure("https://api.example.com/graphql")
query = """
query GetUsers { users { id name } }
"""
data = graphql.query(query)

# Inspect metrics
print("Rate limiter:", rate_limiter.get_metrics())
print("Cache:", cache_manager.get_metrics())
```

## Authentication (Python)

The Python bindings include a flexible `AuthenticationManager` for handling authentication with external services. This manager supports multiple authentication methods (API key, OAuth2, JWT, request signing, and custom providers) and integrates seamlessly with the REST and GraphQL adapters.

**Features:**
- Pluggable provider pattern for different authentication schemes
- Thread-safe and supports per-service configuration
- Secure credential storage with AES-256 encryption (via `EncryptedCredentialStore`)
- Request signing with HMAC-SHA256 or Ed25519 (public-key) signatures
- Easily integrates with `RestApiAdapter` and `GraphQLAdapter`

**Example Usage (Config-based):**

```python
from xenocomm import RestApiAdapter, GraphQLAdapter, AuthenticationManager

# Define authentication config for your services
auth_config = {
    "my_rest_service": {
        "auth_type": "api_key",
        "credentials": {"api_key": "secret123"},
        "api_key_header": "X-API-Key"
    },
    "my_graphql_service": {
        "auth_type": "oauth2",
        "credentials": {"access_token": "token123"}
    }
}

auth_mgr = AuthenticationManager(auth_config)

# REST adapter with authentication
rest = RestApiAdapter(auth_manager=auth_mgr, auth_service="my_rest_service")
rest.configure("https://api.example.com")
users = rest.execute("users", "GET")  # Auth header is applied automatically

# GraphQL adapter with authentication
graphql = GraphQLAdapter(auth_manager=auth_mgr, auth_service="my_graphql_service")
graphql.configure("https://api.example.com/graphql")
query = '''
query GetUsers { users { id name } }
'''
data = graphql.query(query)  # Bearer token is applied automatically
```

**Secure Credential Storage (EncryptedCredentialStore):**

You can store credentials securely, encrypted at rest, using the `EncryptedCredentialStore` class. This uses AES-256 encryption and a passphrase-derived key.

```python
from xenocomm.authentication_manager import AuthenticationManager
from xenocomm.credential_store import EncryptedCredentialStore

# Create and save encrypted credentials
store = EncryptedCredentialStore("creds.enc.json", passphrase="mysecret")
store.set_credentials("my_service", {"api_key": "secret123"})
store.save()

# Later, load credentials and use with AuthenticationManager
store2 = EncryptedCredentialStore("creds.enc.json", passphrase="mysecret")
auth_mgr = AuthenticationManager({
    "my_service": {"auth_type": "api_key", "api_key_header": "X-API-Key"}
}, credential_store=store2)

request = SomeRequestObject()
authed_request = auth_mgr.authenticate("my_service", request)
```

**Request Signing (Advanced):**

You can use request signing for services that require HMAC-SHA256 or Ed25519 signatures. The signature can be added to a header or query parameter, and the payload to sign can be the request body, headers, or a custom value.

*HMAC-SHA256 Example:*
```python
import base64
from xenocomm.authentication_manager import AuthenticationManager

# Use a base64-encoded key (for example, b'secretkeybase64')
signing_key_b64 = base64.b64encode(b'secretkeybase64').decode()

config = {
    "my_signed_service": {
        "auth_type": "request_signing",
        "signing_key": signing_key_b64,
        "signing_algorithm": "hmac-sha256",
        "signature_header": "X-Signature",
        "signing_payload": "body"
    }
}
auth_mgr = AuthenticationManager(config)
request = SomeRequestObject()
request.body = b"important data"
authed_request = auth_mgr.authenticate("my_signed_service", request)
print(authed_request.headers["X-Signature"])
```

*Ed25519 Example:*
```python
import base64
from xenocomm.authentication_manager import AuthenticationManager
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization

# Generate Ed25519 private key (for demonstration)
private_key = Ed25519PrivateKey.generate()
private_bytes = private_key.private_bytes(
    encoding=serialization.Encoding.Raw,
    format=serialization.PrivateFormat.Raw,
    encryption_algorithm=serialization.NoEncryption()
)
signing_key_b64 = base64.b64encode(private_bytes).decode()

config = {
    "my_signed_service": {
        "auth_type": "request_signing",
        "signing_key": signing_key_b64,
        "signing_algorithm": "ed25519",
        "signature_header": "X-Signature",
        "signing_payload": "body"
    }
}
auth_mgr = AuthenticationManager(config)
request = SomeRequestObject()
request.body = b"important data"
authed_request = auth_mgr.authenticate("my_signed_service", request)
print(authed_request.headers["X-Signature"])
```

**Supported Authentication Types:**
- API Key (customizable header)
- OAuth2 Bearer Token (with automatic refresh)
- JWT
- Request Signing (HMAC-SHA256, Ed25519)
- Easily extensible with custom providers

See the Python API docs for more details on advanced authentication, request signing, custom providers, and secure credential storage.

## See the Python API docs for more details on advanced configuration and integration. 
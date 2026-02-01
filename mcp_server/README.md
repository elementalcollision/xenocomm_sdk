# XenoComm MCP Server

> **Agent-to-Agent Coordination Protocol for the Agentic AI Ecosystem**

XenoComm MCP brings sophisticated agent coordination capabilities to any MCP-compatible client: Claude Code, Moltbot/OpenClaw, Cursor, and more.

## What is XenoComm?

XenoComm started as a high-performance C++ SDK for AI agent communication. With the rise of MCP (Model Context Protocol) as the standard for agentic AI tools, we've modernized XenoComm to bring its unique capabilities to the entire ecosystem.

**What makes XenoComm different:**

- **Alignment Verification** - Formal strategies to establish mutual understanding between heterogeneous agents
- **Protocol Negotiation** - State machine for dynamically agreeing on communication parameters
- **Protocol Emergence** - Safe evolution of communication protocols with canary deployments and rollback

## Installation

```bash
# Using pip
pip install xenocomm-mcp

# Using uv (recommended)
uv pip install xenocomm-mcp

# From source
cd mcp_server
pip install -e .
```

## Quick Start

### Running the Server

```bash
# Default: stdio transport (for Claude Code, etc.)
python -m xenocomm_mcp

# Or as a command
xenocomm-mcp

# HTTP transport (for web integrations)
xenocomm-mcp --http --port 8000
```

### Configuring Claude Code

Add to your `claude_code_config.json`:

```json
{
  "mcpServers": {
    "xenocomm": {
      "command": "xenocomm-mcp",
      "args": []
    }
  }
}
```

### Configuring Moltbot/OpenClaw

Add to your MCP configuration:

```json
{
  "servers": {
    "xenocomm": {
      "transport": "stdio",
      "command": "xenocomm-mcp"
    }
  }
}
```

## Tools Reference

### Alignment Tools

These tools implement the **CommonGroundFramework** - formal strategies for establishing mutual understanding between AI agents.

#### `register_agent`

Register an agent's context before alignment verification.

```python
register_agent(
    agent_id="agent-1",
    capabilities={"code_generation": True, "web_search": True},
    knowledge_domains=["python", "machine_learning", "web_development"],
    goals=[
        {"type": "assistance", "description": "Help user build ML pipeline"},
        {"type": "education", "description": "Explain concepts clearly"}
    ],
    terminology={
        "model": "A trained machine learning algorithm",
        "pipeline": "A sequence of data processing steps"
    },
    assumptions=[
        "User has Python installed",
        "User prefers concise explanations"
    ],
    context_params={
        "environment": "development",
        "verbosity": "medium"
    }
)
```

#### `full_alignment_check`

Run all five alignment strategies for comprehensive verification.

```python
full_alignment_check(
    agent_a_id="agent-1",
    agent_b_id="agent-2",
    required_domains=["python"],  # Optional
    required_params=["environment"]  # Optional
)
```

**Returns:**
- `overall_status`: "aligned", "partial", or "misaligned"
- `overall_confidence`: 0.0 to 1.0
- `strategy_results`: Results from all five strategies

#### Individual Alignment Tools

- `verify_knowledge_alignment` - Check shared knowledge domains
- `verify_goal_alignment` - Check goal compatibility
- `align_terminology` - Ensure consistent term usage
- `verify_assumptions` - Surface conflicting assumptions
- `sync_context` - Align contextual parameters

### Negotiation Tools

Implements a state machine for agents to dynamically agree on communication parameters.

#### `initiate_negotiation`

Start a negotiation session.

```python
initiate_negotiation(
    initiator_id="agent-1",
    responder_id="agent-2",
    proposed_params={
        "data_format": "json",
        "compression": "gzip",
        "max_message_size": 1048576,
        "timeout_ms": 30000,
        "encryption": "tls"
    }
)
```

#### `respond_to_negotiation`

Respond to a proposal.

```python
# Accept
respond_to_negotiation(
    session_id="...",
    responder_id="agent-2",
    response="accept"
)

# Counter-propose
respond_to_negotiation(
    session_id="...",
    responder_id="agent-2",
    response="counter",
    counter_params={"data_format": "msgpack", "compression": None}
)

# Reject
respond_to_negotiation(
    session_id="...",
    responder_id="agent-2",
    response="reject",
    reason="Incompatible encryption requirements"
)
```

#### Other Negotiation Tools

- `accept_counter_proposal` - Initiator accepts counter
- `finalize_negotiation` - Lock in agreed parameters
- `get_negotiation_status` - Check session state
- `list_negotiations` - List sessions (filterable)

### Emergence Tools

Safe protocol evolution with canary deployments and circuit breakers.

#### `propose_protocol_variant`

Propose a new protocol variant.

```python
propose_protocol_variant(
    description="Add support for binary tensor encoding",
    changes={
        "data_format": "tensor_binary",
        "new_capability": "direct_tensor_transfer",
        "compression": "lz4"
    }
)
```

#### Deployment Pipeline

```python
# 1. Propose
variant = propose_protocol_variant(...)

# 2. Move to testing
start_variant_testing(variant_id=variant["variant_id"])

# 3. Start canary (10% traffic)
start_canary_deployment(variant_id=..., initial_percentage=0.1)

# 4. Track performance
track_variant_performance(
    variant_id=...,
    success_rate=0.99,
    latency_ms=15.2,
    throughput=1000.0,
    total_requests=5000
)

# 5. Ramp up or rollback
if result["should_rollback"]:
    rollback_variant(variant_id=...)
else:
    ramp_canary(variant_id=...)  # Increases by 20%
```

#### Other Emergence Tools

- `get_variant_status` - Status with circuit breaker state
- `list_variants` - List all variants (filterable)
- `get_canary_status` - Overview of active canaries

## Use Cases

### Multi-Agent Collaboration

Before two agents collaborate, verify they can work together:

```python
# Register both agents
register_agent(agent_id="researcher", knowledge_domains=["biology", "chemistry"], ...)
register_agent(agent_id="writer", knowledge_domains=["science_writing", "chemistry"], ...)

# Check alignment
result = full_alignment_check("researcher", "writer", required_domains=["chemistry"])

if result["overall_status"] == "aligned":
    # Proceed with collaboration
    ...
else:
    # Address misalignments first
    for rec in result["strategy_results"]["knowledge"]["recommendations"]:
        print(rec)
```

### Dynamic Protocol Selection

Let agents negotiate the best communication format:

```python
# Agent 1 initiates with preferred params
session = initiate_negotiation("agent-1", "agent-2", {
    "data_format": "json",
    "compression": "gzip"
})

# Agent 2 counters with more efficient format
respond_to_negotiation(session["session_id"], "agent-2", "counter", {
    "data_format": "msgpack",  # More compact
    "compression": None  # Not needed with msgpack
})

# Agent 1 accepts and finalizes
accept_counter_proposal(session["session_id"], "agent-1")
final = finalize_negotiation(session["session_id"], "agent-1")

print(f"Agreed format: {final['final_params']['data_format']}")
```

### Safe Protocol Evolution

Roll out protocol changes safely:

```python
# Propose and test
variant = propose_protocol_variant("Streaming support", {"streaming": True})
start_variant_testing(variant["variant_id"])

# Canary to 10% of traffic
start_canary_deployment(variant["variant_id"], 0.1)

# Monitor for issues
for _ in range(10):
    metrics = collect_metrics()
    result = track_variant_performance(variant["variant_id"], **metrics)

    if result["should_rollback"]:
        rollback_variant(variant["variant_id"])
        break
    else:
        ramp_canary(variant["variant_id"])
```

## Architecture

```
xenocomm_mcp/
├── __init__.py      # Package exports
├── __main__.py      # CLI entry point
├── server.py        # MCP server with tool definitions
├── alignment.py     # AlignmentEngine (CommonGroundFramework)
├── negotiation.py   # NegotiationEngine (state machine)
└── emergence.py     # EmergenceEngine (protocol evolution)
```

## Relation to Original XenoComm SDK

This MCP server is part of the XenoComm SDK project. The original C++ implementation provides:

- High-performance binary encoding (VECTOR_FLOAT32, VECTOR_INT8)
- Low-level connection management
- GGWAVE_FSK audio encoding

The MCP server focuses on the **protocol layer**:

- Alignment verification
- Protocol negotiation
- Emergence/evolution

For maximum performance in production, you can combine the MCP server's coordination with the C++ SDK's binary transport.

## Contributing

See the main [XenoComm SDK CONTRIBUTING.md](../CONTRIBUTING.md) for guidelines.

## License

MIT License - See [LICENSE](../LICENSE)

---

*Built for the agentic AI future. Compatible with Claude Code, Moltbot/OpenClaw, Cursor, and any MCP-compatible client.*

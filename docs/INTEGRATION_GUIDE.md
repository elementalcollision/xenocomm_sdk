# XenoComm Integration Guide

This guide explains how to integrate XenoComm with external multi-agent systems, including OpenClaw and Claude-Flow.

---

## Overview

XenoComm provides integration bridges that enable:

1. **Monitoring** - Observe agent activity from external systems in Flow Observatory
2. **Lifecycle Management** - Apply KFM evolutionary selection to external agents
3. **Cross-System Coordination** - Route events between different agent platforms
4. **Unified Analytics** - Aggregate metrics across all connected systems

---

## 1. OpenClaw Integration

### What is OpenClaw?

[OpenClaw](https://github.com/openclaw/openclaw) is a WebSocket-based agent control plane that manages AI agent sessions across multiple messaging channels (Discord, Slack, Telegram, WhatsApp, etc.).

### Integration Architecture

```
┌──────────────────────────────────────────────────────────┐
│                       OpenClaw                           │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐    │
│  │ Discord │  │  Slack  │  │Telegram │  │ Signal  │    │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘    │
│       └────────────┼────────────┼────────────┘          │
│                    ▼                                     │
│              ┌──────────┐                               │
│              │ Gateway  │ ◄─── WebSocket API            │
│              └────┬─────┘                               │
└───────────────────┼──────────────────────────────────────┘
                    │
                    ▼
┌───────────────────────────────────────────────────────────┐
│                  XenoComm Bridge                          │
│  ┌──────────────────┐    ┌──────────────────┐           │
│  │ OpenClawBridge   │───▶│ Flow Observatory  │           │
│  └──────────────────┘    └──────────────────┘           │
│           │                                              │
│           ▼                                              │
│  ┌──────────────────┐                                   │
│  │ KFM Lifecycle    │                                   │
│  │ Engine           │                                   │
│  └──────────────────┘                                   │
└───────────────────────────────────────────────────────────┘
```

### Setup

```python
from xenocomm_mcp import (
    setup_openclaw_integration,
    OpenClawConfig,
)

# Configure the integration
config = OpenClawConfig(
    gateway_url="ws://127.0.0.1:18789",
    api_token="your-api-token",  # Optional
    reconnect_interval=5.0,
    channels=["discord", "slack"],
)

# Initialize the bridge
bridge = setup_openclaw_integration(config)

# Connect (async)
import asyncio
asyncio.run(bridge.connect())
```

### Event Mapping

| OpenClaw Event | XenoComm Flow Type | Description |
|----------------|-------------------|-------------|
| `session_start` | `AGENT_LIFECYCLE` | New agent session created |
| `session_end` | `AGENT_LIFECYCLE` | Session terminated |
| `sessions_send` | `COLLABORATION` | Inter-agent message |
| `tool_invoke` | `WORKFLOW` | Tool execution |
| `webhook` | `SYSTEM` | External webhook received |
| `cron_trigger` | `SYSTEM` | Scheduled job triggered |

### Custom Event Handlers

```python
from xenocomm_mcp.integrations import IntegrationEvent

def my_custom_handler(event: IntegrationEvent):
    """Handle custom OpenClaw events."""
    print(f"Received: {event.event_type}")
    print(f"Payload: {event.payload}")

# Register handler
bridge.register_handler("custom_event_type", my_custom_handler)

# Wildcard handler (receives all events)
bridge.register_handler("*", my_custom_handler)
```

---

## 2. Claude-Flow Integration

### What is Claude-Flow?

[Claude-Flow](https://github.com/ruvnet/claude-flow) is an enterprise AI orchestration platform featuring:

- Hierarchical mesh topology with queen-coordinator agents
- Multiple consensus algorithms (Raft, Byzantine, Gossip)
- RuVector intelligence layer with HNSW vector search
- 60+ specialized agents across worker categories

### Integration Architecture

```
┌──────────────────────────────────────────────────────────┐
│                      Claude-Flow                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │                   EventBus                       │    │
│  └─────────────────────┬───────────────────────────┘    │
│           ┌────────────┼────────────┐                   │
│           ▼            ▼            ▼                   │
│      ┌────────┐   ┌────────┐   ┌────────┐              │
│      │ Queen  │   │Workers │   │ Memory │              │
│      └────────┘   └────────┘   └────────┘              │
└───────────────────────┬──────────────────────────────────┘
                        │
                        ▼
┌───────────────────────────────────────────────────────────┐
│                  XenoComm Bridge                          │
│  ┌──────────────────┐    ┌──────────────────┐           │
│  │ClaudeFlowBridge  │───▶│ Flow Observatory  │           │
│  └──────────────────┘    └──────────────────┘           │
│           │                                              │
│           ▼                                              │
│  ┌──────────────────┐    ┌──────────────────┐           │
│  │ KFM Lifecycle    │    │ Consensus        │           │
│  │ Engine           │    │ Analytics        │           │
│  └──────────────────┘    └──────────────────┘           │
└───────────────────────────────────────────────────────────┘
```

### Setup

```python
from xenocomm_mcp import (
    setup_claude_flow_integration,
    ClaudeFlowConfig,
)

# Configure the integration
config = ClaudeFlowConfig(
    memory_namespace="xenocomm",
    swarm_topology="hierarchical-mesh",
    max_agents=8,
    enable_consensus_tracking=True,
)

# Initialize the bridge
bridge = setup_claude_flow_integration(config)

# Connect (async)
import asyncio
asyncio.run(bridge.connect())
```

### Event Mapping

| Claude-Flow Event | XenoComm Flow Type | Description |
|-------------------|-------------------|-------------|
| `swarm_init` | `SYSTEM` | Swarm initialized |
| `swarm_terminate` | `SYSTEM` | Swarm terminated |
| `agent_spawn` | `AGENT_LIFECYCLE` | Agent created |
| `agent_complete` | `AGENT_LIFECYCLE` | Agent task finished |
| `consensus_round` | `NEGOTIATION` | Consensus decision |
| `memory_operation` | `WORKFLOW` | Memory read/write |
| `security_event` | `SYSTEM` | Security alert |

### Consensus Analytics

```python
# Get consensus analytics
analytics = bridge.get_consensus_analytics()

print(f"Total consensus rounds: {analytics['total']}")
print(f"Success rate: {analytics['success_rate']:.1%}")

for algo, stats in analytics['by_algorithm'].items():
    print(f"  {algo}: {stats['agreed']}/{stats['total']} agreed, "
          f"avg {stats['avg_rounds']:.1f} rounds")
```

---

## 3. KFM Lifecycle Management

The KFM (Kill/Fuck/Marry) lifecycle engine from [Agentic Evolution](https://github.com/elementalcollision/Agentic_Evolution) provides evolutionary selection for agent management.

### Selection Operators

| Operator | Phase | Action | When to Apply |
|----------|-------|--------|---------------|
| **KILL** | Deprecated/Eliminated | Remove from system | Performance degraded, misaligned, excessive conflicts |
| **FUCK** | Experimental | Test and adapt | New agent, environment change, potential improvement |
| **MARRY** | Stable | Commit resources | Proven reliable, well-aligned, consistent |

### Lifecycle Flow

```
PROPOSED → EXPERIMENTAL → STABLE
              ↓              ↓
           (adapt)        (degrade)
              ↓              ↓
          DEPRECATED ← ─ ─ ─ ┘
              ↓
          ELIMINATED
```

### Using KFM with External Agents

```python
from xenocomm_mcp import (
    get_kfm_engine,
    LifecyclePhase,
    PerformanceMetrics,
    AlignmentMetrics,
    EvolutionPotential,
)

kfm = get_kfm_engine()

# Register an external agent
state = kfm.register_entity(
    entity_id="openclaw:agent_123",
    entity_type="agent",
    initial_phase=LifecyclePhase.EXPERIMENTAL,
    metadata={
        "source": "openclaw",
        "channel": "discord",
    }
)

# Update metrics based on observed behavior
kfm.update_metrics(
    "openclaw:agent_123",
    performance=PerformanceMetrics(
        success_rate=0.85,
        error_rate=0.05,
        latency_ms=150,
        throughput=100,
    ),
    alignment=AlignmentMetrics(
        goal_alignment=0.9,
        collaboration_success=0.8,
        conflict_frequency=0.1,
    ),
    evolution=EvolutionPotential(
        innovation_score=0.6,
        learning_rate=0.7,
        adaptability=0.8,
    ),
)

# Evaluate for phase transition
new_phase, reason = kfm.evaluate_transition("openclaw:agent_123")
if new_phase:
    print(f"Recommend transition to {new_phase.value}: {reason}")

    # Execute transition
    kfm.transition_phase("openclaw:agent_123", new_phase, reason)
```

### Quick KFM Scoring

```python
from xenocomm_mcp import compute_agent_kfm_score

scores = compute_agent_kfm_score(
    success_rate=0.85,
    error_rate=0.05,
    alignment_score=0.8,
    collaboration_success=0.75,
    innovation_potential=0.6,
    time_active_hours=24,
)

print(f"Kill score:  {scores.kill_score:.3f}")
print(f"Fuck score:  {scores.fuck_score:.3f}")
print(f"Marry score: {scores.marry_score:.3f}")
print(f"Recommendation: {scores.recommendation.value}")
print(f"Reasoning: {scores.reasoning}")
```

### KFM Dashboard

```python
# Get lifecycle dashboard for all entities
dashboard = kfm.get_lifecycle_dashboard()

print(f"Total entities: {dashboard['total_entities']}")
print(f"By phase: {dashboard['by_phase']}")
print(f"By recommendation: {dashboard['by_recommendation']}")
```

---

## 4. Cross-System Event Routing

### Integration Manager

```python
from xenocomm_mcp import get_integration_manager

manager = get_integration_manager()

# Check status of all integrations
status = manager.get_status()
for name, state in status.items():
    print(f"{name}: {state}")

# Get all external agents
agents = manager.get_all_external_agents()
for agent in agents:
    print(f"{agent.system}:{agent.agent_id} - Last seen: {agent.last_seen}")
```

### Event Forwarding

Forward events between systems:

```python
async def forward_to_claude_flow(event: IntegrationEvent):
    """Forward OpenClaw events to Claude-Flow."""
    claude_flow = manager.get_bridge("claude_flow")

    if event.event_type == "sessions_send":
        # Transform and forward
        cf_event = IntegrationEvent(
            event_id=f"forwarded-{event.event_id}",
            source_system="openclaw",
            event_type="external_message",
            timestamp=event.timestamp,
            payload={
                "original_system": "openclaw",
                **event.payload,
            },
        )
        await claude_flow.send_event(cf_event)

# Register forwarder
openclaw = manager.get_bridge("openclaw")
openclaw.register_handler("sessions_send", forward_to_claude_flow)
```

---

## 5. Flow Observatory Visualization

### Viewing External Agent Activity

All integrated agent activity appears in the Flow Observatory:

```bash
# Run the dashboard
python -m xenocomm_mcp dashboard

# Or run the live demo with integrations
python -m xenocomm_mcp research --mode text
```

### Event Tags

External events are tagged for easy filtering:

- `openclaw` - Events from OpenClaw integration
- `claude_flow` - Events from Claude-Flow integration
- `external` - All external system events
- `security` - Security-related events
- `consensus` - Consensus algorithm events

---

## 6. Best Practices

### Connection Management

```python
import asyncio
import signal

async def main():
    manager = get_integration_manager()

    # Setup integrations
    setup_openclaw_integration()
    setup_claude_flow_integration()

    # Connect all
    results = await manager.connect_all()
    print(f"Connection results: {results}")

    # Handle shutdown gracefully
    loop = asyncio.get_event_loop()

    def shutdown():
        asyncio.create_task(manager.disconnect_all())

    signal.signal(signal.SIGINT, lambda *_: shutdown())
    signal.signal(signal.SIGTERM, lambda *_: shutdown())

    # Keep running
    while True:
        await asyncio.sleep(1)

asyncio.run(main())
```

### Error Handling

```python
def safe_handler(event: IntegrationEvent):
    """Handler with error protection."""
    try:
        # Process event
        process_event(event)
    except Exception as e:
        # Log error but don't crash
        logger.error(f"Handler error: {e}")

        # Emit error event for visibility
        obs = get_observation_manager()
        obs.event_bus.publish(FlowEvent(
            event_id=f"handler-error-{event.event_id}",
            flow_type=FlowType.SYSTEM,
            event_name="handler_error",
            timestamp=datetime.utcnow(),
            summary=f"Error processing {event.event_type}: {str(e)}",
            severity=EventSeverity.ERROR,
            tags=["error", "handler"],
        ))
```

### Performance Considerations

1. **Batching** - Buffer events and emit in batches for high-throughput systems
2. **Filtering** - Register specific handlers rather than wildcards when possible
3. **Async** - Use async handlers for I/O-bound operations
4. **Memory** - Set appropriate history limits in analytics

---

## 7. Troubleshooting

### Connection Issues

```python
# Check integration status
manager = get_integration_manager()
for name, bridge in manager.bridges.items():
    print(f"{name}: {bridge.status.value}")
    if bridge.status == IntegrationStatus.ERROR:
        # Attempt reconnect
        asyncio.run(bridge.connect())
```

### Missing Events

1. Verify handlers are registered before connecting
2. Check event type strings match exactly
3. Look for errors in system events
4. Verify WebSocket/EventBus connectivity

### KFM Scores Not Updating

1. Ensure metrics are being updated regularly
2. Check entity exists in KFM engine
3. Verify metric values are within valid ranges (0-1 for rates)

---

## Next Steps

- See `PROTOCOL_DEEP_DIVE.md` for detailed protocol explanations
- See `KFM_WEIGHTING.md` for evolutionary lifecycle details
- See `API_REFERENCE.md` for complete MCP tool documentation

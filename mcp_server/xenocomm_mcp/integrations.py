"""
XenoComm Integration Bridges

This module provides integration adapters for external multi-agent systems:
- OpenClaw: WebSocket-based agent control plane
- Claude-Flow: Enterprise multi-agent orchestration

These bridges enable XenoComm's Flow Observatory to monitor and interact
with agents from these external systems.
"""

import asyncio
import json
import hashlib
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Any, Callable, Optional
import threading

from .observation import (
    ObservationManager,
    FlowType,
    EventSeverity,
    FlowEvent,
    get_observation_manager,
)
from .kfm_lifecycle import (
    KFMLifecycleEngine,
    PerformanceMetrics,
    AlignmentMetrics,
    EvolutionPotential,
    LifecyclePhase,
    get_kfm_engine,
)


# ============================================================================
# Base Integration Framework
# ============================================================================

class IntegrationStatus(Enum):
    """Status of an integration connection."""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    ERROR = "error"
    RECONNECTING = "reconnecting"


@dataclass
class ExternalAgent:
    """Represents an agent from an external system."""
    agent_id: str
    system: str  # "openclaw", "claude_flow", etc.
    external_id: str  # ID in the external system
    capabilities: dict[str, Any] = field(default_factory=dict)
    metadata: dict[str, Any] = field(default_factory=dict)
    registered_at: datetime = field(default_factory=datetime.utcnow)
    last_seen: datetime = field(default_factory=datetime.utcnow)


@dataclass
class IntegrationEvent:
    """Event from an external system."""
    event_id: str
    source_system: str
    event_type: str
    timestamp: datetime
    payload: dict[str, Any]
    raw_data: Any = None


class IntegrationBridge(ABC):
    """Base class for integration bridges."""

    def __init__(
        self,
        name: str,
        obs: ObservationManager | None = None,
        kfm: KFMLifecycleEngine | None = None,
    ):
        self.name = name
        self.obs = obs or get_observation_manager()
        self.kfm = kfm or get_kfm_engine()
        self.status = IntegrationStatus.DISCONNECTED
        self.agents: dict[str, ExternalAgent] = {}
        self._event_handlers: dict[str, list[Callable]] = {}

    @abstractmethod
    async def connect(self, **config) -> bool:
        """Establish connection to external system."""
        pass

    @abstractmethod
    async def disconnect(self) -> bool:
        """Close connection to external system."""
        pass

    @abstractmethod
    async def send_event(self, event: IntegrationEvent) -> bool:
        """Send an event to the external system."""
        pass

    def register_handler(self, event_type: str, handler: Callable):
        """Register a handler for a specific event type."""
        if event_type not in self._event_handlers:
            self._event_handlers[event_type] = []
        self._event_handlers[event_type].append(handler)

    def _emit_to_observatory(
        self,
        event: IntegrationEvent,
        flow_type: FlowType,
        summary: str,
    ):
        """Emit an external event to the Flow Observatory."""
        self.obs.event_bus.publish(FlowEvent(
            event_id=event.event_id,
            flow_type=flow_type,
            event_name=f"{self.name}:{event.event_type}",
            timestamp=event.timestamp,
            summary=summary,
            tags=[self.name, event.event_type, "external"],
            metrics=event.payload.get("metrics", {}),
        ))

    def _process_event(self, event: IntegrationEvent):
        """Process an incoming event from the external system."""
        handlers = self._event_handlers.get(event.event_type, [])
        handlers.extend(self._event_handlers.get("*", []))  # Wildcard handlers

        for handler in handlers:
            try:
                handler(event)
            except Exception as e:
                self.obs.event_bus.publish(FlowEvent(
                    event_id=f"error-{event.event_id}",
                    flow_type=FlowType.SYSTEM,
                    event_name="integration_handler_error",
                    timestamp=datetime.utcnow(),
                    summary=f"Error in {self.name} handler: {str(e)}",
                    severity=EventSeverity.ERROR,
                    tags=[self.name, "error"],
                ))


# ============================================================================
# OpenClaw Integration
# ============================================================================

class OpenClawEventType(Enum):
    """Event types from OpenClaw."""
    SESSION_START = "session_start"
    SESSION_END = "session_end"
    MESSAGE_SENT = "sessions_send"
    TOOL_INVOKED = "tool_invoke"
    AGENT_RESPONSE = "agent_response"
    CHANNEL_DELIVERY = "channel_delivery"
    WEBHOOK_RECEIVED = "webhook"
    CRON_TRIGGERED = "cron_trigger"


@dataclass
class OpenClawConfig:
    """Configuration for OpenClaw integration."""
    gateway_url: str = "ws://127.0.0.1:18789"
    api_token: str | None = None
    reconnect_interval: float = 5.0
    max_reconnect_attempts: int = 10
    channels: list[str] = field(default_factory=list)


class OpenClawBridge(IntegrationBridge):
    """
    Integration bridge for OpenClaw agent control plane.

    OpenClaw uses a WebSocket-based control plane for managing AI agent
    sessions across multiple messaging channels (Discord, Slack, Telegram, etc.).

    This bridge:
    - Monitors agent session lifecycle
    - Tracks inter-agent messaging via sessions_send
    - Observes tool invocations and responses
    - Feeds events to XenoComm's Flow Observatory
    - Applies KFM lifecycle management to OpenClaw agents

    Reference: https://github.com/openclaw/openclaw
    """

    def __init__(
        self,
        config: OpenClawConfig | None = None,
        obs: ObservationManager | None = None,
    ):
        super().__init__("openclaw", obs)
        self.config = config or OpenClawConfig()
        self._ws = None
        self._listener_task = None
        self._sessions: dict[str, dict] = {}

    async def connect(self, **kwargs) -> bool:
        """Connect to OpenClaw gateway via WebSocket."""
        try:
            # Note: Actual WebSocket connection would use websockets library
            # This is a structural implementation showing the integration pattern
            self.status = IntegrationStatus.CONNECTING

            self.obs.event_bus.publish(FlowEvent(
                event_id=f"openclaw-connect-{datetime.utcnow().timestamp()}",
                flow_type=FlowType.SYSTEM,
                event_name="integration_connecting",
                timestamp=datetime.utcnow(),
                summary=f"Connecting to OpenClaw gateway at {self.config.gateway_url}",
                tags=["openclaw", "connection"],
            ))

            # In production, this would establish actual WebSocket connection:
            # self._ws = await websockets.connect(self.config.gateway_url)
            # self._listener_task = asyncio.create_task(self._listen())

            self.status = IntegrationStatus.CONNECTED
            return True

        except Exception as e:
            self.status = IntegrationStatus.ERROR
            self.obs.event_bus.publish(FlowEvent(
                event_id=f"openclaw-error-{datetime.utcnow().timestamp()}",
                flow_type=FlowType.SYSTEM,
                event_name="integration_error",
                timestamp=datetime.utcnow(),
                summary=f"OpenClaw connection failed: {str(e)}",
                severity=EventSeverity.ERROR,
                tags=["openclaw", "error"],
            ))
            return False

    async def disconnect(self) -> bool:
        """Disconnect from OpenClaw gateway."""
        if self._listener_task:
            self._listener_task.cancel()
        if self._ws:
            await self._ws.close()

        self.status = IntegrationStatus.DISCONNECTED
        return True

    async def send_event(self, event: IntegrationEvent) -> bool:
        """Send an event to OpenClaw (e.g., trigger a session command)."""
        if self.status != IntegrationStatus.CONNECTED:
            return False

        # In production, send via WebSocket:
        # await self._ws.send(json.dumps(event.payload))
        return True

    async def _listen(self):
        """Listen for events from OpenClaw gateway."""
        while self.status == IntegrationStatus.CONNECTED:
            try:
                message = await self._ws.recv()
                data = json.loads(message)
                event = self._parse_openclaw_event(data)
                self._process_event(event)
            except asyncio.CancelledError:
                break
            except Exception as e:
                self.obs.event_bus.publish(FlowEvent(
                    event_id=f"openclaw-listen-error",
                    flow_type=FlowType.SYSTEM,
                    event_name="listener_error",
                    timestamp=datetime.utcnow(),
                    summary=f"OpenClaw listener error: {str(e)}",
                    severity=EventSeverity.WARNING,
                    tags=["openclaw", "listener"],
                ))

    def _parse_openclaw_event(self, data: dict) -> IntegrationEvent:
        """Parse a raw OpenClaw message into an IntegrationEvent."""
        event_type = data.get("type", "unknown")

        return IntegrationEvent(
            event_id=data.get("id", hashlib.md5(json.dumps(data).encode()).hexdigest()[:16]),
            source_system="openclaw",
            event_type=event_type,
            timestamp=datetime.utcnow(),
            payload=data,
            raw_data=data,
        )

    def handle_session_event(self, event: IntegrationEvent):
        """Handle OpenClaw session lifecycle events."""
        payload = event.payload
        session_id = payload.get("session_id")

        if event.event_type == OpenClawEventType.SESSION_START.value:
            # Track new session
            self._sessions[session_id] = {
                "started_at": event.timestamp,
                "agent_id": payload.get("agent_id"),
                "channel": payload.get("channel"),
            }

            # Register agent in KFM lifecycle
            agent_id = f"openclaw:{payload.get('agent_id')}"
            self.kfm.register_entity(
                entity_id=agent_id,
                entity_type="agent",
                initial_phase=LifecyclePhase.EXPERIMENTAL,
                metadata={"session_id": session_id, "channel": payload.get("channel")},
            )

            # Emit to observatory
            self._emit_to_observatory(
                event,
                FlowType.AGENT_LIFECYCLE,
                f"OpenClaw session started: {session_id[:8]}",
            )

        elif event.event_type == OpenClawEventType.SESSION_END.value:
            session = self._sessions.pop(session_id, {})

            self._emit_to_observatory(
                event,
                FlowType.AGENT_LIFECYCLE,
                f"OpenClaw session ended: {session_id[:8]}",
            )

    def handle_message_event(self, event: IntegrationEvent):
        """Handle inter-agent messaging events."""
        payload = event.payload

        self._emit_to_observatory(
            event,
            FlowType.COLLABORATION,
            f"OpenClaw message: {payload.get('from', '?')} -> {payload.get('to', '?')}",
        )

        # Track for pattern detection
        agent_id = f"openclaw:{payload.get('from')}"
        if agent_id in self.kfm.entities:
            # Update evolution metrics based on communication patterns
            state = self.kfm.entities[agent_id]
            current = state.evolution
            self.kfm.update_metrics(
                agent_id,
                evolution=EvolutionPotential(
                    innovation_score=current.innovation_score,
                    learning_rate=current.learning_rate,
                    adaptability=current.adaptability,
                    pattern_emergence=min(1.0, current.pattern_emergence + 0.01),
                ),
            )

    def handle_tool_event(self, event: IntegrationEvent):
        """Handle tool invocation events."""
        payload = event.payload
        tool_name = payload.get("tool", "unknown")
        success = payload.get("success", True)

        self._emit_to_observatory(
            event,
            FlowType.WORKFLOW,
            f"OpenClaw tool: {tool_name} ({'✓' if success else '✗'})",
        )

        # Update performance metrics
        agent_id = f"openclaw:{payload.get('agent_id')}"
        if agent_id in self.kfm.entities:
            state = self.kfm.entities[agent_id]
            current = state.performance

            # Update success/error rates
            total_ops = current.throughput + 1
            new_success_rate = (
                (current.success_rate * (total_ops - 1) + (1 if success else 0)) / total_ops
            )
            new_error_rate = (
                (current.error_rate * (total_ops - 1) + (0 if success else 1)) / total_ops
            )

            self.kfm.update_metrics(
                agent_id,
                performance=PerformanceMetrics(
                    success_rate=new_success_rate,
                    error_rate=new_error_rate,
                    throughput=total_ops,
                    latency_ms=payload.get("latency_ms", current.latency_ms),
                ),
            )

    def setup_default_handlers(self):
        """Set up default event handlers for OpenClaw integration."""
        self.register_handler(OpenClawEventType.SESSION_START.value, self.handle_session_event)
        self.register_handler(OpenClawEventType.SESSION_END.value, self.handle_session_event)
        self.register_handler(OpenClawEventType.MESSAGE_SENT.value, self.handle_message_event)
        self.register_handler(OpenClawEventType.TOOL_INVOKED.value, self.handle_tool_event)


# ============================================================================
# Claude-Flow Integration
# ============================================================================

class ClaudeFlowEventType(Enum):
    """Event types from Claude-Flow."""
    SWARM_INIT = "swarm_init"
    SWARM_TERMINATE = "swarm_terminate"
    AGENT_SPAWN = "agent_spawn"
    AGENT_COMPLETE = "agent_complete"
    TASK_ASSIGNED = "task_assigned"
    TASK_COMPLETED = "task_completed"
    CONSENSUS_ROUND = "consensus_round"
    MEMORY_OPERATION = "memory_operation"
    REASONING_CYCLE = "reasoning_cycle"
    SECURITY_EVENT = "security_event"


@dataclass
class ClaudeFlowConfig:
    """Configuration for Claude-Flow integration."""
    event_bus_url: str | None = None  # If using network EventBus
    memory_namespace: str = "xenocomm"
    swarm_topology: str = "hierarchical-mesh"
    max_agents: int = 8
    enable_consensus_tracking: bool = True


class ClaudeFlowBridge(IntegrationBridge):
    """
    Integration bridge for Claude-Flow multi-agent orchestration.

    Claude-Flow is an enterprise AI orchestration platform with:
    - Hierarchical mesh topology with queen-coordinator agents
    - Multiple consensus algorithms (Raft, Byzantine, Gossip)
    - RuVector intelligence layer with HNSW vector search
    - 60+ specialized agents across worker categories

    This bridge:
    - Monitors swarm lifecycle and agent spawning
    - Tracks consensus rounds and coordination decisions
    - Observes memory operations and reasoning cycles
    - Feeds events to XenoComm's Flow Observatory
    - Applies KFM lifecycle management to Claude-Flow agents

    Reference: https://github.com/ruvnet/claude-flow
    """

    def __init__(
        self,
        config: ClaudeFlowConfig | None = None,
        obs: ObservationManager | None = None,
    ):
        super().__init__("claude_flow", obs)
        self.config = config or ClaudeFlowConfig()
        self._swarms: dict[str, dict] = {}
        self._consensus_history: list[dict] = []

    async def connect(self, **kwargs) -> bool:
        """Connect to Claude-Flow EventBus."""
        try:
            self.status = IntegrationStatus.CONNECTING

            self.obs.event_bus.publish(FlowEvent(
                event_id=f"claude-flow-connect-{datetime.utcnow().timestamp()}",
                flow_type=FlowType.SYSTEM,
                event_name="integration_connecting",
                timestamp=datetime.utcnow(),
                summary="Connecting to Claude-Flow EventBus",
                tags=["claude_flow", "connection"],
            ))

            # In production, this would connect to Claude-Flow's EventBus
            # or set up MCP tool integration

            self.status = IntegrationStatus.CONNECTED
            return True

        except Exception as e:
            self.status = IntegrationStatus.ERROR
            return False

    async def disconnect(self) -> bool:
        """Disconnect from Claude-Flow."""
        self.status = IntegrationStatus.DISCONNECTED
        return True

    async def send_event(self, event: IntegrationEvent) -> bool:
        """Send an event to Claude-Flow."""
        if self.status != IntegrationStatus.CONNECTED:
            return False
        # In production, emit via Claude-Flow's EventBus
        return True

    def handle_swarm_event(self, event: IntegrationEvent):
        """Handle swarm lifecycle events."""
        payload = event.payload
        swarm_id = payload.get("swarm_id")

        if event.event_type == ClaudeFlowEventType.SWARM_INIT.value:
            self._swarms[swarm_id] = {
                "started_at": event.timestamp,
                "topology": payload.get("topology", self.config.swarm_topology),
                "max_agents": payload.get("max_agents", self.config.max_agents),
                "agents": [],
            }

            self._emit_to_observatory(
                event,
                FlowType.SYSTEM,
                f"Claude-Flow swarm initialized: {payload.get('topology')} with {payload.get('max_agents')} agents",
            )

        elif event.event_type == ClaudeFlowEventType.SWARM_TERMINATE.value:
            swarm = self._swarms.pop(swarm_id, {})

            self._emit_to_observatory(
                event,
                FlowType.SYSTEM,
                f"Claude-Flow swarm terminated: {swarm_id[:8]}",
            )

    def handle_agent_event(self, event: IntegrationEvent):
        """Handle agent spawn/complete events."""
        payload = event.payload
        agent_id = f"claude_flow:{payload.get('agent_id')}"
        agent_type = payload.get("agent_type", "worker")  # queen, worker, specialist

        if event.event_type == ClaudeFlowEventType.AGENT_SPAWN.value:
            # Register in KFM lifecycle
            self.kfm.register_entity(
                entity_id=agent_id,
                entity_type="agent",
                initial_phase=LifecyclePhase.EXPERIMENTAL,
                metadata={
                    "swarm_id": payload.get("swarm_id"),
                    "agent_type": agent_type,
                    "worker_category": payload.get("category"),  # Researcher, Coder, etc.
                },
            )

            self._emit_to_observatory(
                event,
                FlowType.AGENT_LIFECYCLE,
                f"Claude-Flow {agent_type} spawned: {payload.get('agent_id')}",
            )

        elif event.event_type == ClaudeFlowEventType.AGENT_COMPLETE.value:
            success = payload.get("success", True)

            # Update performance metrics
            if agent_id in self.kfm.entities:
                state = self.kfm.entities[agent_id]
                current = state.performance

                self.kfm.update_metrics(
                    agent_id,
                    performance=PerformanceMetrics(
                        success_rate=current.success_rate * 0.9 + (0.1 if success else 0),
                        error_rate=current.error_rate * 0.9 + (0 if success else 0.1),
                        throughput=current.throughput + 1,
                    ),
                )

            self._emit_to_observatory(
                event,
                FlowType.AGENT_LIFECYCLE,
                f"Claude-Flow agent completed: {payload.get('agent_id')} ({'✓' if success else '✗'})",
            )

    def handle_consensus_event(self, event: IntegrationEvent):
        """Handle consensus round events (Raft, Byzantine, etc.)."""
        if not self.config.enable_consensus_tracking:
            return

        payload = event.payload
        algorithm = payload.get("algorithm", "unknown")  # raft, byzantine, gossip
        outcome = payload.get("outcome")  # agreed, split, timeout
        participants = payload.get("participants", [])
        rounds = payload.get("rounds", 1)

        self._consensus_history.append({
            "timestamp": event.timestamp.isoformat(),
            "algorithm": algorithm,
            "outcome": outcome,
            "participants": len(participants),
            "rounds": rounds,
        })

        # Keep last 100 consensus events
        self._consensus_history = self._consensus_history[-100:]

        self._emit_to_observatory(
            event,
            FlowType.NEGOTIATION,
            f"Claude-Flow consensus ({algorithm}): {outcome} in {rounds} rounds",
        )

        # Update alignment metrics for participants
        for participant_id in participants:
            agent_id = f"claude_flow:{participant_id}"
            if agent_id in self.kfm.entities:
                state = self.kfm.entities[agent_id]
                current = state.alignment

                # Successful consensus improves collaboration score
                collaboration_delta = 0.05 if outcome == "agreed" else -0.02

                self.kfm.update_metrics(
                    agent_id,
                    alignment=AlignmentMetrics(
                        goal_alignment=current.goal_alignment,
                        knowledge_coverage=current.knowledge_coverage,
                        conflict_frequency=current.conflict_frequency,
                        collaboration_success=min(1.0, max(0.0,
                            current.collaboration_success + collaboration_delta
                        )),
                        protocol_compliance=current.protocol_compliance,
                    ),
                )

    def handle_memory_event(self, event: IntegrationEvent):
        """Handle memory/reasoning events."""
        payload = event.payload
        operation = payload.get("operation")  # retrieve, store, consolidate
        namespace = payload.get("namespace")

        self._emit_to_observatory(
            event,
            FlowType.WORKFLOW,
            f"Claude-Flow memory {operation}: {namespace}",
        )

    def handle_security_event(self, event: IntegrationEvent):
        """Handle security-related events."""
        payload = event.payload
        threat_type = payload.get("threat_type")
        severity = payload.get("severity", "low")

        severity_map = {
            "low": EventSeverity.INFO,
            "medium": EventSeverity.WARNING,
            "high": EventSeverity.ERROR,
            "critical": EventSeverity.CRITICAL,
        }

        self.obs.event_bus.publish(FlowEvent(
            event_id=event.event_id,
            flow_type=FlowType.SYSTEM,
            event_name=f"claude_flow:security:{threat_type}",
            timestamp=event.timestamp,
            summary=f"Claude-Flow security: {threat_type} ({severity})",
            severity=severity_map.get(severity, EventSeverity.WARNING),
            tags=["claude_flow", "security", threat_type],
        ))

    def setup_default_handlers(self):
        """Set up default event handlers for Claude-Flow integration."""
        self.register_handler(ClaudeFlowEventType.SWARM_INIT.value, self.handle_swarm_event)
        self.register_handler(ClaudeFlowEventType.SWARM_TERMINATE.value, self.handle_swarm_event)
        self.register_handler(ClaudeFlowEventType.AGENT_SPAWN.value, self.handle_agent_event)
        self.register_handler(ClaudeFlowEventType.AGENT_COMPLETE.value, self.handle_agent_event)
        self.register_handler(ClaudeFlowEventType.CONSENSUS_ROUND.value, self.handle_consensus_event)
        self.register_handler(ClaudeFlowEventType.MEMORY_OPERATION.value, self.handle_memory_event)
        self.register_handler(ClaudeFlowEventType.SECURITY_EVENT.value, self.handle_security_event)

    def get_consensus_analytics(self) -> dict[str, Any]:
        """Get analytics on consensus history."""
        if not self._consensus_history:
            return {"total": 0, "by_algorithm": {}, "success_rate": 0}

        by_algorithm = {}
        successes = 0

        for event in self._consensus_history:
            algo = event["algorithm"]
            if algo not in by_algorithm:
                by_algorithm[algo] = {"total": 0, "agreed": 0, "avg_rounds": 0}

            by_algorithm[algo]["total"] += 1
            if event["outcome"] == "agreed":
                by_algorithm[algo]["agreed"] += 1
                successes += 1

            # Update rolling average for rounds
            n = by_algorithm[algo]["total"]
            by_algorithm[algo]["avg_rounds"] = (
                by_algorithm[algo]["avg_rounds"] * (n - 1) + event["rounds"]
            ) / n

        return {
            "total": len(self._consensus_history),
            "success_rate": successes / len(self._consensus_history),
            "by_algorithm": by_algorithm,
        }


# ============================================================================
# Integration Manager
# ============================================================================

class IntegrationManager:
    """
    Manages all external system integrations.

    Provides a unified interface for:
    - Connecting/disconnecting integrations
    - Routing events between systems
    - Aggregating cross-system analytics
    """

    def __init__(self, obs: ObservationManager | None = None):
        self.obs = obs or get_observation_manager()
        self.bridges: dict[str, IntegrationBridge] = {}

    def register_bridge(self, bridge: IntegrationBridge):
        """Register an integration bridge."""
        self.bridges[bridge.name] = bridge

    def get_bridge(self, name: str) -> IntegrationBridge | None:
        """Get an integration bridge by name."""
        return self.bridges.get(name)

    async def connect_all(self) -> dict[str, bool]:
        """Connect all registered bridges."""
        results = {}
        for name, bridge in self.bridges.items():
            results[name] = await bridge.connect()
        return results

    async def disconnect_all(self) -> dict[str, bool]:
        """Disconnect all bridges."""
        results = {}
        for name, bridge in self.bridges.items():
            results[name] = await bridge.disconnect()
        return results

    def get_status(self) -> dict[str, str]:
        """Get status of all integrations."""
        return {
            name: bridge.status.value
            for name, bridge in self.bridges.items()
        }

    def get_all_external_agents(self) -> list[ExternalAgent]:
        """Get all agents from all connected systems."""
        agents = []
        for bridge in self.bridges.values():
            agents.extend(bridge.agents.values())
        return agents


# ============================================================================
# Convenience Functions
# ============================================================================

_integration_manager: IntegrationManager | None = None


def get_integration_manager() -> IntegrationManager:
    """Get the singleton integration manager."""
    global _integration_manager
    if _integration_manager is None:
        _integration_manager = IntegrationManager()
    return _integration_manager


def setup_openclaw_integration(config: OpenClawConfig | None = None) -> OpenClawBridge:
    """Set up OpenClaw integration with default handlers."""
    manager = get_integration_manager()
    bridge = OpenClawBridge(config)
    bridge.setup_default_handlers()
    manager.register_bridge(bridge)
    return bridge


def setup_claude_flow_integration(config: ClaudeFlowConfig | None = None) -> ClaudeFlowBridge:
    """Set up Claude-Flow integration with default handlers."""
    manager = get_integration_manager()
    bridge = ClaudeFlowBridge(config)
    bridge.setup_default_handlers()
    manager.register_bridge(bridge)
    return bridge

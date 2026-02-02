"""
XenoComm Observation System

Provides sensors and event capture for monitoring agent communication flows.
Enables real-time observation of negotiations, orchestrations, and emergence
without exposing raw protocol or language exchange details.
"""

from __future__ import annotations

import uuid
import time
import threading
from datetime import datetime
from dataclasses import dataclass, field
from typing import Any, Callable
from enum import Enum
from collections import deque
from abc import ABC, abstractmethod


# ==================== Event Types ====================

class FlowType(Enum):
    """Categories of communication flows."""
    AGENT_LIFECYCLE = "agent_lifecycle"
    ALIGNMENT = "alignment"
    NEGOTIATION = "negotiation"
    EMERGENCE = "emergence"
    WORKFLOW = "workflow"
    COLLABORATION = "collaboration"
    SYSTEM = "system"


class EventSeverity(Enum):
    """Event severity levels."""
    DEBUG = "debug"
    INFO = "info"
    WARNING = "warning"
    ERROR = "error"
    CRITICAL = "critical"


@dataclass
class FlowEvent:
    """Represents a captured communication flow event."""
    event_id: str
    flow_type: FlowType
    event_name: str
    timestamp: datetime
    severity: EventSeverity = EventSeverity.INFO
    source_agent: str | None = None
    target_agent: str | None = None
    session_id: str | None = None
    metrics: dict[str, Any] = field(default_factory=dict)
    summary: str = ""
    tags: list[str] = field(default_factory=list)
    parent_event_id: str | None = None
    duration_ms: float | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "event_id": self.event_id,
            "flow_type": self.flow_type.value,
            "event_name": self.event_name,
            "timestamp": self.timestamp.isoformat(),
            "severity": self.severity.value,
            "source_agent": self.source_agent,
            "target_agent": self.target_agent,
            "session_id": self.session_id,
            "metrics": self.metrics,
            "summary": self.summary,
            "tags": self.tags,
            "parent_event_id": self.parent_event_id,
            "duration_ms": self.duration_ms,
        }


@dataclass
class FlowSnapshot:
    """Point-in-time snapshot of system state."""
    snapshot_id: str
    timestamp: datetime
    active_agents: list[str]
    active_negotiations: int
    active_workflows: int
    protocol_variants: int
    alignment_scores: dict[str, float]
    throughput_events_per_sec: float
    error_rate: float

    def to_dict(self) -> dict[str, Any]:
        return {
            "snapshot_id": self.snapshot_id,
            "timestamp": self.timestamp.isoformat(),
            "active_agents": self.active_agents,
            "active_negotiations": self.active_negotiations,
            "active_workflows": self.active_workflows,
            "protocol_variants": self.protocol_variants,
            "alignment_scores": self.alignment_scores,
            "throughput_events_per_sec": self.throughput_events_per_sec,
            "error_rate": self.error_rate,
        }


# ==================== Event Bus ====================

class EventBus:
    """
    Central event bus for collecting and distributing flow events.

    Thread-safe event collection with subscriber notification.
    """

    def __init__(self, max_history: int = 10000):
        self.max_history = max_history
        self._events: deque[FlowEvent] = deque(maxlen=max_history)
        self._subscribers: dict[str, Callable[[FlowEvent], None]] = {}
        self._type_subscribers: dict[FlowType, list[Callable[[FlowEvent], None]]] = {}
        self._lock = threading.RLock()
        self._event_counts: dict[str, int] = {}
        self._start_time = datetime.utcnow()

    def publish(self, event: FlowEvent) -> None:
        """Publish an event to the bus."""
        with self._lock:
            self._events.append(event)

            # Update counts
            key = f"{event.flow_type.value}:{event.event_name}"
            self._event_counts[key] = self._event_counts.get(key, 0) + 1

        # Notify subscribers (outside lock)
        self._notify_subscribers(event)

    def subscribe(self, subscriber_id: str, callback: Callable[[FlowEvent], None],
                  flow_types: list[FlowType] | None = None) -> None:
        """Subscribe to events."""
        with self._lock:
            self._subscribers[subscriber_id] = callback

            if flow_types:
                for ft in flow_types:
                    if ft not in self._type_subscribers:
                        self._type_subscribers[ft] = []
                    self._type_subscribers[ft].append(callback)

    def unsubscribe(self, subscriber_id: str) -> None:
        """Unsubscribe from events."""
        with self._lock:
            if subscriber_id in self._subscribers:
                callback = self._subscribers.pop(subscriber_id)
                # Remove from type subscribers
                for callbacks in self._type_subscribers.values():
                    if callback in callbacks:
                        callbacks.remove(callback)

    def _notify_subscribers(self, event: FlowEvent) -> None:
        """Notify relevant subscribers of an event."""
        callbacks_to_notify = []

        with self._lock:
            # General subscribers
            callbacks_to_notify.extend(self._subscribers.values())

            # Type-specific subscribers
            if event.flow_type in self._type_subscribers:
                callbacks_to_notify.extend(self._type_subscribers[event.flow_type])

        # Deduplicate and notify
        notified = set()
        for callback in callbacks_to_notify:
            if id(callback) not in notified:
                notified.add(id(callback))
                try:
                    callback(event)
                except Exception:
                    pass  # Don't let subscriber errors crash the bus

    def get_recent_events(self, count: int = 100,
                          flow_type: FlowType | None = None) -> list[FlowEvent]:
        """Get recent events, optionally filtered by type."""
        with self._lock:
            if flow_type:
                events = [e for e in self._events if e.flow_type == flow_type]
            else:
                events = list(self._events)
            return events[-count:]

    def get_events_since(self, since: datetime,
                         flow_type: FlowType | None = None) -> list[FlowEvent]:
        """Get events since a timestamp."""
        with self._lock:
            events = [e for e in self._events if e.timestamp >= since]
            if flow_type:
                events = [e for e in events if e.flow_type == flow_type]
            return events

    def get_stats(self) -> dict[str, Any]:
        """Get event bus statistics."""
        with self._lock:
            total_events = len(self._events)
            uptime = (datetime.utcnow() - self._start_time).total_seconds()

            type_counts = {}
            for event in self._events:
                ft = event.flow_type.value
                type_counts[ft] = type_counts.get(ft, 0) + 1

            error_count = sum(1 for e in self._events
                            if e.severity in (EventSeverity.ERROR, EventSeverity.CRITICAL))

            return {
                "total_events": total_events,
                "uptime_seconds": uptime,
                "events_per_second": total_events / max(uptime, 1),
                "type_counts": type_counts,
                "error_count": error_count,
                "error_rate": error_count / max(total_events, 1),
                "subscriber_count": len(self._subscribers),
            }

    def clear(self) -> None:
        """Clear all events."""
        with self._lock:
            self._events.clear()
            self._event_counts.clear()


# ==================== Flow Sensors ====================

class FlowSensor(ABC):
    """Base class for flow sensors."""

    def __init__(self, event_bus: EventBus, sensor_id: str | None = None):
        self.event_bus = event_bus
        self.sensor_id = sensor_id or str(uuid.uuid4())[:8]
        self.enabled = True
        self._active_spans: dict[str, tuple[datetime, dict]] = {}

    @abstractmethod
    def get_flow_type(self) -> FlowType:
        """Return the flow type this sensor monitors."""
        pass

    def emit(self, event_name: str, summary: str = "",
             source_agent: str | None = None,
             target_agent: str | None = None,
             session_id: str | None = None,
             metrics: dict[str, Any] | None = None,
             severity: EventSeverity = EventSeverity.INFO,
             tags: list[str] | None = None,
             parent_event_id: str | None = None) -> FlowEvent:
        """Emit a flow event."""
        if not self.enabled:
            return None

        event = FlowEvent(
            event_id=str(uuid.uuid4()),
            flow_type=self.get_flow_type(),
            event_name=event_name,
            timestamp=datetime.utcnow(),
            severity=severity,
            source_agent=source_agent,
            target_agent=target_agent,
            session_id=session_id,
            metrics=metrics or {},
            summary=summary,
            tags=tags or [],
            parent_event_id=parent_event_id,
        )

        self.event_bus.publish(event)
        return event

    def start_span(self, span_name: str, context: dict[str, Any] | None = None) -> str:
        """Start a timed span for measuring durations."""
        span_id = str(uuid.uuid4())
        self._active_spans[span_id] = (datetime.utcnow(), context or {})
        return span_id

    def end_span(self, span_id: str, event_name: str, summary: str = "",
                 **kwargs) -> FlowEvent | None:
        """End a span and emit an event with duration."""
        if span_id not in self._active_spans:
            return None

        start_time, context = self._active_spans.pop(span_id)
        duration_ms = (datetime.utcnow() - start_time).total_seconds() * 1000

        # Merge context into kwargs
        merged_kwargs = {**context, **kwargs}
        metrics = merged_kwargs.pop("metrics", {})
        metrics["duration_ms"] = duration_ms

        event = self.emit(event_name, summary, metrics=metrics, **merged_kwargs)
        if event:
            event.duration_ms = duration_ms
        return event


class AgentSensor(FlowSensor):
    """Sensor for agent lifecycle events."""

    def get_flow_type(self) -> FlowType:
        return FlowType.AGENT_LIFECYCLE

    def agent_registered(self, agent_id: str, capabilities: list[str],
                        domains: list[str]) -> FlowEvent:
        return self.emit(
            "agent_registered",
            f"Agent '{agent_id}' joined the network",
            source_agent=agent_id,
            metrics={
                "capability_count": len(capabilities),
                "domain_count": len(domains),
            },
            tags=["lifecycle", "registration"]
        )

    def agent_deregistered(self, agent_id: str, reason: str = "") -> FlowEvent:
        return self.emit(
            "agent_deregistered",
            f"Agent '{agent_id}' left the network: {reason}",
            source_agent=agent_id,
            tags=["lifecycle", "deregistration"]
        )


class AlignmentSensor(FlowSensor):
    """Sensor for alignment check events."""

    def get_flow_type(self) -> FlowType:
        return FlowType.ALIGNMENT

    def alignment_started(self, agent_a: str, agent_b: str,
                         session_id: str) -> str:
        self.emit(
            "alignment_started",
            f"Checking alignment: {agent_a} <-> {agent_b}",
            source_agent=agent_a,
            target_agent=agent_b,
            session_id=session_id,
            tags=["alignment", "start"]
        )
        return self.start_span("alignment", {
            "source_agent": agent_a,
            "target_agent": agent_b,
            "session_id": session_id
        })

    def alignment_completed(self, span_id: str, score: float,
                           dimensions_checked: int,
                           aligned_count: int) -> FlowEvent:
        return self.end_span(
            span_id,
            "alignment_completed",
            f"Alignment score: {score:.1%} ({aligned_count}/{dimensions_checked} aligned)",
            metrics={
                "score": score,
                "dimensions_checked": dimensions_checked,
                "aligned_count": aligned_count,
            },
            tags=["alignment", "complete"]
        )


class NegotiationSensor(FlowSensor):
    """Sensor for negotiation events."""

    def get_flow_type(self) -> FlowType:
        return FlowType.NEGOTIATION

    def negotiation_initiated(self, initiator: str, responder: str,
                             session_id: str) -> str:
        self.emit(
            "negotiation_initiated",
            f"Negotiation started: {initiator} -> {responder}",
            source_agent=initiator,
            target_agent=responder,
            session_id=session_id,
            tags=["negotiation", "start"]
        )
        return self.start_span("negotiation", {
            "source_agent": initiator,
            "target_agent": responder,
            "session_id": session_id
        })

    def proposal_made(self, session_id: str, proposer: str,
                     param_count: int) -> FlowEvent:
        return self.emit(
            "proposal_made",
            f"Proposal from {proposer}: {param_count} parameters",
            source_agent=proposer,
            session_id=session_id,
            metrics={"param_count": param_count},
            tags=["negotiation", "proposal"]
        )

    def counter_proposal(self, session_id: str, responder: str,
                        changes: int) -> FlowEvent:
        return self.emit(
            "counter_proposal",
            f"Counter from {responder}: {changes} changes",
            source_agent=responder,
            session_id=session_id,
            metrics={"changes": changes},
            tags=["negotiation", "counter"]
        )

    def negotiation_completed(self, span_id: str, outcome: str,
                             rounds: int) -> FlowEvent:
        return self.end_span(
            span_id,
            "negotiation_completed",
            f"Negotiation {outcome} after {rounds} rounds",
            metrics={"rounds": rounds, "outcome": outcome},
            tags=["negotiation", "complete"]
        )


class EmergenceSensor(FlowSensor):
    """Sensor for protocol emergence events."""

    def get_flow_type(self) -> FlowType:
        return FlowType.EMERGENCE

    def variant_proposed(self, variant_id: str, description: str,
                        change_count: int) -> FlowEvent:
        return self.emit(
            "variant_proposed",
            f"New variant '{variant_id}': {description}",
            metrics={"change_count": change_count},
            tags=["emergence", "proposal"]
        )

    def canary_started(self, variant_id: str, percentage: float) -> FlowEvent:
        return self.emit(
            "canary_started",
            f"Canary deployment: {variant_id} at {percentage:.0%}",
            metrics={"percentage": percentage},
            tags=["emergence", "canary"]
        )

    def canary_ramped(self, variant_id: str, old_pct: float,
                     new_pct: float) -> FlowEvent:
        return self.emit(
            "canary_ramped",
            f"Canary ramp: {variant_id} {old_pct:.0%} -> {new_pct:.0%}",
            metrics={"old_percentage": old_pct, "new_percentage": new_pct},
            tags=["emergence", "canary", "ramp"]
        )

    def variant_activated(self, variant_id: str) -> FlowEvent:
        return self.emit(
            "variant_activated",
            f"Variant '{variant_id}' is now active",
            severity=EventSeverity.INFO,
            tags=["emergence", "activation"]
        )

    def variant_rolled_back(self, variant_id: str, reason: str) -> FlowEvent:
        return self.emit(
            "variant_rolled_back",
            f"Rollback: {variant_id} - {reason}",
            severity=EventSeverity.WARNING,
            tags=["emergence", "rollback"]
        )

    def experiment_started(self, experiment_id: str, control: str,
                          treatment: str) -> FlowEvent:
        return self.emit(
            "experiment_started",
            f"A/B test: {control} vs {treatment}",
            metrics={"control": control, "treatment": treatment},
            tags=["emergence", "experiment"]
        )

    def experiment_concluded(self, experiment_id: str, winner: str | None,
                            confidence: float) -> FlowEvent:
        winner_str = winner or "no clear winner"
        return self.emit(
            "experiment_concluded",
            f"A/B result: {winner_str} (confidence: {confidence:.1%})",
            metrics={"winner": winner, "confidence": confidence},
            tags=["emergence", "experiment", "conclusion"]
        )


class WorkflowSensor(FlowSensor):
    """Sensor for workflow execution events."""

    def get_flow_type(self) -> FlowType:
        return FlowType.WORKFLOW

    def workflow_started(self, workflow_type: str, execution_id: str,
                        step_count: int) -> str:
        self.emit(
            "workflow_started",
            f"Workflow '{workflow_type}' started ({step_count} steps)",
            session_id=execution_id,
            metrics={"step_count": step_count},
            tags=["workflow", "start", workflow_type]
        )
        return self.start_span("workflow", {"session_id": execution_id})

    def step_started(self, execution_id: str, step_name: str,
                    step_index: int) -> FlowEvent:
        return self.emit(
            "step_started",
            f"Step {step_index + 1}: {step_name}",
            session_id=execution_id,
            metrics={"step_index": step_index, "step_name": step_name},
            tags=["workflow", "step"]
        )

    def step_completed(self, execution_id: str, step_name: str,
                      success: bool) -> FlowEvent:
        status = "completed" if success else "failed"
        return self.emit(
            "step_completed",
            f"Step '{step_name}' {status}",
            session_id=execution_id,
            severity=EventSeverity.INFO if success else EventSeverity.ERROR,
            metrics={"success": success},
            tags=["workflow", "step", status]
        )

    def workflow_completed(self, span_id: str, outcome: str) -> FlowEvent:
        return self.end_span(
            span_id,
            "workflow_completed",
            f"Workflow {outcome}",
            tags=["workflow", "complete", outcome]
        )


class CollaborationSensor(FlowSensor):
    """Sensor for collaboration session events."""

    def get_flow_type(self) -> FlowType:
        return FlowType.COLLABORATION

    def session_created(self, session_id: str, agent_a: str,
                       agent_b: str) -> FlowEvent:
        return self.emit(
            "session_created",
            f"Collaboration: {agent_a} <-> {agent_b}",
            source_agent=agent_a,
            target_agent=agent_b,
            session_id=session_id,
            tags=["collaboration", "start"]
        )

    def session_state_changed(self, session_id: str, old_state: str,
                             new_state: str) -> FlowEvent:
        return self.emit(
            "session_state_changed",
            f"Session state: {old_state} -> {new_state}",
            session_id=session_id,
            metrics={"old_state": old_state, "new_state": new_state},
            tags=["collaboration", "state"]
        )

    def session_ended(self, session_id: str, reason: str) -> FlowEvent:
        return self.emit(
            "session_ended",
            f"Session ended: {reason}",
            session_id=session_id,
            tags=["collaboration", "end"]
        )


# ==================== Observation Manager ====================

class ObservationManager:
    """
    Central manager for all observation sensors and dashboard data.

    Provides a unified interface for instrumenting the XenoComm system
    and collecting flow data for visualization.
    """

    def __init__(self, max_history: int = 10000):
        self.event_bus = EventBus(max_history=max_history)

        # Initialize sensors
        self.agent_sensor = AgentSensor(self.event_bus)
        self.alignment_sensor = AlignmentSensor(self.event_bus)
        self.negotiation_sensor = NegotiationSensor(self.event_bus)
        self.emergence_sensor = EmergenceSensor(self.event_bus)
        self.workflow_sensor = WorkflowSensor(self.event_bus)
        self.collaboration_sensor = CollaborationSensor(self.event_bus)

        # Snapshot history
        self._snapshots: deque[FlowSnapshot] = deque(maxlen=1000)
        self._snapshot_interval = 5.0  # seconds
        self._snapshot_thread: threading.Thread | None = None
        self._running = False

    def start(self) -> None:
        """Start the observation manager."""
        self._running = True
        self._snapshot_thread = threading.Thread(
            target=self._snapshot_loop,
            daemon=True
        )
        self._snapshot_thread.start()

        self.event_bus.publish(FlowEvent(
            event_id=str(uuid.uuid4()),
            flow_type=FlowType.SYSTEM,
            event_name="observation_started",
            timestamp=datetime.utcnow(),
            summary="Observation system initialized",
            tags=["system", "startup"]
        ))

    def stop(self) -> None:
        """Stop the observation manager."""
        self._running = False
        if self._snapshot_thread:
            self._snapshot_thread.join(timeout=2.0)

        self.event_bus.publish(FlowEvent(
            event_id=str(uuid.uuid4()),
            flow_type=FlowType.SYSTEM,
            event_name="observation_stopped",
            timestamp=datetime.utcnow(),
            summary="Observation system stopped",
            tags=["system", "shutdown"]
        ))

    def _snapshot_loop(self) -> None:
        """Background thread for taking periodic snapshots."""
        while self._running:
            time.sleep(self._snapshot_interval)
            if self._running:
                self._take_snapshot()

    def _take_snapshot(self) -> FlowSnapshot:
        """Take a snapshot of current system state."""
        stats = self.event_bus.get_stats()

        snapshot = FlowSnapshot(
            snapshot_id=str(uuid.uuid4()),
            timestamp=datetime.utcnow(),
            active_agents=[],  # Will be populated by orchestrator
            active_negotiations=0,
            active_workflows=0,
            protocol_variants=0,
            alignment_scores={},
            throughput_events_per_sec=stats["events_per_second"],
            error_rate=stats["error_rate"],
        )

        self._snapshots.append(snapshot)
        return snapshot

    def get_dashboard_data(self) -> dict[str, Any]:
        """Get data for dashboard rendering."""
        stats = self.event_bus.get_stats()
        recent_events = self.event_bus.get_recent_events(50)

        # Group events by type
        events_by_type = {}
        for event in recent_events:
            ft = event.flow_type.value
            if ft not in events_by_type:
                events_by_type[ft] = []
            events_by_type[ft].append(event.to_dict())

        # Recent activity timeline
        timeline = [
            {
                "time": e.timestamp.strftime("%H:%M:%S"),
                "type": e.flow_type.value,
                "event": e.event_name,
                "summary": e.summary,
                "severity": e.severity.value,
            }
            for e in recent_events[-20:]
        ]

        return {
            "stats": stats,
            "events_by_type": events_by_type,
            "timeline": list(reversed(timeline)),
            "snapshots": [s.to_dict() for s in list(self._snapshots)[-10:]],
        }

    def get_flow_summary(self) -> dict[str, Any]:
        """Get a high-level summary of flow activity."""
        stats = self.event_bus.get_stats()

        # Calculate rates by type
        type_rates = {}
        uptime = stats["uptime_seconds"]
        for flow_type, count in stats["type_counts"].items():
            type_rates[flow_type] = count / max(uptime, 1)

        return {
            "total_events": stats["total_events"],
            "uptime_seconds": uptime,
            "overall_rate": stats["events_per_second"],
            "type_rates": type_rates,
            "error_rate": stats["error_rate"],
            "active_subscribers": stats["subscriber_count"],
        }


# ==================== Global Instance ====================

_observation_manager: ObservationManager | None = None


def get_observation_manager() -> ObservationManager:
    """Get or create the global observation manager."""
    global _observation_manager
    if _observation_manager is None:
        _observation_manager = ObservationManager()
    return _observation_manager


def reset_observation_manager() -> ObservationManager:
    """Reset and return a new observation manager."""
    global _observation_manager
    if _observation_manager:
        _observation_manager.stop()
    _observation_manager = ObservationManager()
    return _observation_manager

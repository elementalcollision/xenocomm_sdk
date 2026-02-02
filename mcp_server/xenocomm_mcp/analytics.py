"""
XenoComm Flow Analytics

Provides aggregation, persistence, and analytics capabilities for
the observation system.
"""

from __future__ import annotations

import json
import gzip
import threading
from datetime import datetime, timedelta
from pathlib import Path
from dataclasses import dataclass, field
from typing import Any, Callable
from collections import defaultdict
from enum import Enum

from .observation import (
    EventBus,
    FlowEvent,
    FlowType,
    EventSeverity,
    ObservationManager,
)


# ==================== Event Persistence ====================

class EventPersistence:
    """
    Persists observation events to files for later analysis.

    Supports:
    - JSON Lines format for easy parsing
    - Optional gzip compression
    - Automatic file rotation by size or time
    - Async writing to avoid blocking
    """

    def __init__(
        self,
        base_path: str | Path = "./xenocomm_logs",
        max_file_size_mb: float = 10.0,
        compress: bool = True,
        buffer_size: int = 100,
    ):
        self.base_path = Path(base_path)
        self.base_path.mkdir(parents=True, exist_ok=True)

        self.max_file_size = int(max_file_size_mb * 1024 * 1024)
        self.compress = compress
        self.buffer_size = buffer_size

        self._buffer: list[FlowEvent] = []
        self._lock = threading.Lock()
        self._current_file: Path | None = None
        self._current_size = 0
        self._file_count = 0

    def _get_log_filename(self) -> Path:
        """Generate a log filename with timestamp."""
        timestamp = datetime.utcnow().strftime("%Y%m%d_%H%M%S")
        ext = ".jsonl.gz" if self.compress else ".jsonl"
        return self.base_path / f"flows_{timestamp}_{self._file_count}{ext}"

    def _rotate_if_needed(self) -> None:
        """Rotate log file if it exceeds max size."""
        if self._current_file and self._current_size >= self.max_file_size:
            self._file_count += 1
            self._current_file = None
            self._current_size = 0

    def _write_buffer(self) -> None:
        """Write buffered events to file."""
        if not self._buffer:
            return

        self._rotate_if_needed()

        if not self._current_file:
            self._current_file = self._get_log_filename()

        events_to_write = self._buffer
        self._buffer = []

        # Write events
        lines = [json.dumps(e.to_dict()) + "\n" for e in events_to_write]
        content = "".join(lines).encode("utf-8")

        if self.compress:
            content = gzip.compress(content)
            mode = "ab"
        else:
            mode = "a"
            content = "".join(lines)

        with open(self._current_file, mode) as f:
            if isinstance(content, bytes):
                f.write(content)
            else:
                f.write(content)

        self._current_size += len(content)

    def write_event(self, event: FlowEvent) -> None:
        """Buffer an event for writing."""
        with self._lock:
            self._buffer.append(event)
            if len(self._buffer) >= self.buffer_size:
                self._write_buffer()

    def flush(self) -> None:
        """Flush any buffered events to disk."""
        with self._lock:
            self._write_buffer()

    def read_events(
        self,
        since: datetime | None = None,
        until: datetime | None = None,
        flow_type: FlowType | None = None,
    ) -> list[FlowEvent]:
        """Read events from persisted files."""
        self.flush()  # Ensure buffer is written

        events = []
        for log_file in sorted(self.base_path.glob("flows_*.jsonl*")):
            try:
                if log_file.suffix == ".gz":
                    with gzip.open(log_file, "rt") as f:
                        lines = f.readlines()
                else:
                    with open(log_file, "r") as f:
                        lines = f.readlines()

                for line in lines:
                    data = json.loads(line)
                    event = self._dict_to_event(data)

                    # Apply filters
                    if since and event.timestamp < since:
                        continue
                    if until and event.timestamp > until:
                        continue
                    if flow_type and event.flow_type != flow_type:
                        continue

                    events.append(event)
            except Exception:
                continue

        return events

    def _dict_to_event(self, data: dict) -> FlowEvent:
        """Convert dictionary to FlowEvent."""
        return FlowEvent(
            event_id=data["event_id"],
            flow_type=FlowType(data["flow_type"]),
            event_name=data["event_name"],
            timestamp=datetime.fromisoformat(data["timestamp"]),
            severity=EventSeverity(data.get("severity", "info")),
            source_agent=data.get("source_agent"),
            target_agent=data.get("target_agent"),
            session_id=data.get("session_id"),
            metrics=data.get("metrics", {}),
            summary=data.get("summary", ""),
            tags=data.get("tags", []),
            parent_event_id=data.get("parent_event_id"),
            duration_ms=data.get("duration_ms"),
        )


# ==================== Flow Analytics ====================

@dataclass
class TimeWindow:
    """A time window for aggregation."""
    start: datetime
    end: datetime
    duration_seconds: float

    @classmethod
    def last_minutes(cls, minutes: int) -> "TimeWindow":
        end = datetime.utcnow()
        start = end - timedelta(minutes=minutes)
        return cls(start=start, end=end, duration_seconds=minutes * 60)

    @classmethod
    def last_hours(cls, hours: int) -> "TimeWindow":
        end = datetime.utcnow()
        start = end - timedelta(hours=hours)
        return cls(start=start, end=end, duration_seconds=hours * 3600)


@dataclass
class FlowMetrics:
    """Aggregated metrics for a flow type."""
    flow_type: FlowType
    event_count: int
    error_count: int
    avg_duration_ms: float | None
    min_duration_ms: float | None
    max_duration_ms: float | None
    events_per_minute: float
    unique_agents: int
    unique_sessions: int
    top_events: list[tuple[str, int]]  # (event_name, count)

    def to_dict(self) -> dict[str, Any]:
        return {
            "flow_type": self.flow_type.value,
            "event_count": self.event_count,
            "error_count": self.error_count,
            "error_rate": self.error_count / max(self.event_count, 1),
            "avg_duration_ms": self.avg_duration_ms,
            "min_duration_ms": self.min_duration_ms,
            "max_duration_ms": self.max_duration_ms,
            "events_per_minute": self.events_per_minute,
            "unique_agents": self.unique_agents,
            "unique_sessions": self.unique_sessions,
            "top_events": dict(self.top_events[:10]),
        }


@dataclass
class AgentMetrics:
    """Metrics for a specific agent."""
    agent_id: str
    total_events: int
    as_source: int
    as_target: int
    alignments: int
    negotiations: int
    collaborations: int
    error_events: int
    last_active: datetime | None

    def to_dict(self) -> dict[str, Any]:
        return {
            "agent_id": self.agent_id,
            "total_events": self.total_events,
            "as_source": self.as_source,
            "as_target": self.as_target,
            "alignments": self.alignments,
            "negotiations": self.negotiations,
            "collaborations": self.collaborations,
            "error_events": self.error_events,
            "last_active": self.last_active.isoformat() if self.last_active else None,
        }


class FlowAnalytics:
    """
    Provides analytics and aggregation over observation events.

    Features:
    - Time-windowed aggregations
    - Flow type breakdowns
    - Agent activity metrics
    - Session analytics
    - Trend detection
    """

    def __init__(self, event_bus: EventBus):
        self.event_bus = event_bus

    def get_flow_metrics(
        self,
        window: TimeWindow | None = None,
    ) -> dict[FlowType, FlowMetrics]:
        """Get aggregated metrics by flow type."""
        if window:
            events = self.event_bus.get_events_since(window.start)
            duration_minutes = window.duration_seconds / 60
        else:
            events = self.event_bus.get_recent_events(10000)
            stats = self.event_bus.get_stats()
            duration_minutes = max(stats["uptime_seconds"] / 60, 1)

        # Group by flow type
        by_type: dict[FlowType, list[FlowEvent]] = defaultdict(list)
        for event in events:
            by_type[event.flow_type].append(event)

        results = {}
        for flow_type, type_events in by_type.items():
            # Calculate metrics
            error_count = sum(
                1 for e in type_events
                if e.severity in (EventSeverity.ERROR, EventSeverity.CRITICAL)
            )

            durations = [e.duration_ms for e in type_events if e.duration_ms]
            avg_duration = sum(durations) / len(durations) if durations else None
            min_duration = min(durations) if durations else None
            max_duration = max(durations) if durations else None

            agents = set()
            sessions = set()
            event_counts: dict[str, int] = defaultdict(int)

            for e in type_events:
                if e.source_agent:
                    agents.add(e.source_agent)
                if e.target_agent:
                    agents.add(e.target_agent)
                if e.session_id:
                    sessions.add(e.session_id)
                event_counts[e.event_name] += 1

            top_events = sorted(event_counts.items(), key=lambda x: -x[1])

            results[flow_type] = FlowMetrics(
                flow_type=flow_type,
                event_count=len(type_events),
                error_count=error_count,
                avg_duration_ms=avg_duration,
                min_duration_ms=min_duration,
                max_duration_ms=max_duration,
                events_per_minute=len(type_events) / duration_minutes,
                unique_agents=len(agents),
                unique_sessions=len(sessions),
                top_events=top_events,
            )

        return results

    def get_agent_metrics(
        self,
        agent_id: str | None = None,
        window: TimeWindow | None = None,
    ) -> dict[str, AgentMetrics] | AgentMetrics:
        """Get metrics for agents."""
        if window:
            events = self.event_bus.get_events_since(window.start)
        else:
            events = self.event_bus.get_recent_events(10000)

        # Aggregate by agent
        agent_data: dict[str, dict] = defaultdict(lambda: {
            "total": 0,
            "as_source": 0,
            "as_target": 0,
            "alignments": 0,
            "negotiations": 0,
            "collaborations": 0,
            "errors": 0,
            "last_active": None,
        })

        for event in events:
            for aid in [event.source_agent, event.target_agent]:
                if aid:
                    data = agent_data[aid]
                    data["total"] += 1
                    if event.source_agent == aid:
                        data["as_source"] += 1
                    if event.target_agent == aid:
                        data["as_target"] += 1
                    if event.flow_type == FlowType.ALIGNMENT:
                        data["alignments"] += 1
                    elif event.flow_type == FlowType.NEGOTIATION:
                        data["negotiations"] += 1
                    elif event.flow_type == FlowType.COLLABORATION:
                        data["collaborations"] += 1
                    if event.severity in (EventSeverity.ERROR, EventSeverity.CRITICAL):
                        data["errors"] += 1
                    if not data["last_active"] or event.timestamp > data["last_active"]:
                        data["last_active"] = event.timestamp

        results = {}
        for aid, data in agent_data.items():
            results[aid] = AgentMetrics(
                agent_id=aid,
                total_events=data["total"],
                as_source=data["as_source"],
                as_target=data["as_target"],
                alignments=data["alignments"],
                negotiations=data["negotiations"],
                collaborations=data["collaborations"],
                error_events=data["errors"],
                last_active=data["last_active"],
            )

        if agent_id:
            return results.get(agent_id, AgentMetrics(
                agent_id=agent_id, total_events=0, as_source=0, as_target=0,
                alignments=0, negotiations=0, collaborations=0,
                error_events=0, last_active=None
            ))

        return results

    def get_throughput_trend(
        self,
        bucket_minutes: int = 5,
        num_buckets: int = 12,
    ) -> list[dict[str, Any]]:
        """Get throughput trend over time buckets."""
        now = datetime.utcnow()
        buckets = []

        for i in range(num_buckets - 1, -1, -1):
            bucket_start = now - timedelta(minutes=bucket_minutes * (i + 1))
            bucket_end = now - timedelta(minutes=bucket_minutes * i)

            events = [
                e for e in self.event_bus.get_recent_events(10000)
                if bucket_start <= e.timestamp < bucket_end
            ]

            buckets.append({
                "start": bucket_start.isoformat(),
                "end": bucket_end.isoformat(),
                "event_count": len(events),
                "events_per_minute": len(events) / bucket_minutes,
            })

        return buckets

    def get_error_summary(
        self,
        window: TimeWindow | None = None,
    ) -> dict[str, Any]:
        """Get a summary of errors."""
        if window:
            events = self.event_bus.get_events_since(window.start)
        else:
            events = self.event_bus.get_recent_events(10000)

        error_events = [
            e for e in events
            if e.severity in (EventSeverity.ERROR, EventSeverity.CRITICAL)
        ]

        # Group by event name
        by_name: dict[str, list[FlowEvent]] = defaultdict(list)
        for e in error_events:
            by_name[e.event_name].append(e)

        # Group by flow type
        by_type: dict[str, int] = defaultdict(int)
        for e in error_events:
            by_type[e.flow_type.value] += 1

        return {
            "total_errors": len(error_events),
            "total_events": len(events),
            "error_rate": len(error_events) / max(len(events), 1),
            "by_event_name": {
                name: len(evts) for name, evts in sorted(
                    by_name.items(), key=lambda x: -len(x[1])
                )[:10]
            },
            "by_flow_type": dict(by_type),
            "recent_errors": [e.to_dict() for e in error_events[-5:]],
        }

    def detect_anomalies(
        self,
        window: TimeWindow | None = None,
        threshold_std_dev: float = 2.0,
    ) -> list[dict[str, Any]]:
        """Detect anomalous patterns in the event stream."""
        if window:
            events = self.event_bus.get_events_since(window.start)
        else:
            events = self.event_bus.get_recent_events(10000)

        anomalies = []

        # Check for sudden spikes in error rate
        total = len(events)
        errors = sum(
            1 for e in events
            if e.severity in (EventSeverity.ERROR, EventSeverity.CRITICAL)
        )
        error_rate = errors / max(total, 1)

        if error_rate > 0.1:  # More than 10% errors
            anomalies.append({
                "type": "high_error_rate",
                "description": f"Error rate is {error_rate:.1%}",
                "severity": "warning" if error_rate < 0.2 else "critical",
                "value": error_rate,
            })

        # Check for stalled flow types
        stats = self.event_bus.get_stats()
        expected_types = {FlowType.ALIGNMENT, FlowType.NEGOTIATION, FlowType.WORKFLOW}
        active_types = {FlowType(t) for t in stats["type_counts"].keys()}
        missing = expected_types - active_types

        if missing and stats["total_events"] > 100:
            anomalies.append({
                "type": "missing_flow_types",
                "description": f"No events for: {[m.value for m in missing]}",
                "severity": "info",
                "missing_types": [m.value for m in missing],
            })

        # Check for unusual agent patterns
        agent_metrics = self.get_agent_metrics(window=window)
        if isinstance(agent_metrics, dict):
            for aid, metrics in agent_metrics.items():
                if metrics.error_events > metrics.total_events * 0.3:
                    anomalies.append({
                        "type": "agent_high_errors",
                        "description": f"Agent '{aid}' has {metrics.error_events} errors",
                        "severity": "warning",
                        "agent_id": aid,
                        "error_rate": metrics.error_events / max(metrics.total_events, 1),
                    })

        return anomalies


# ==================== Alerting System ====================

class AlertSeverity(Enum):
    """Alert severity levels."""
    INFO = "info"
    WARNING = "warning"
    CRITICAL = "critical"


@dataclass
class Alert:
    """Represents an alert."""
    alert_id: str
    title: str
    description: str
    severity: AlertSeverity
    timestamp: datetime
    source_event: FlowEvent | None = None
    metadata: dict[str, Any] = field(default_factory=dict)
    acknowledged: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "alert_id": self.alert_id,
            "title": self.title,
            "description": self.description,
            "severity": self.severity.value,
            "timestamp": self.timestamp.isoformat(),
            "source_event_id": self.source_event.event_id if self.source_event else None,
            "metadata": self.metadata,
            "acknowledged": self.acknowledged,
        }


class AlertingSystem:
    """
    Monitors event stream and generates alerts based on rules.

    Features:
    - Configurable alert rules
    - Multiple severity levels
    - Alert callbacks for integration
    - Deduplication
    """

    def __init__(self, event_bus: EventBus):
        self.event_bus = event_bus
        self.alerts: list[Alert] = []
        self._rules: list[Callable[[FlowEvent], Alert | None]] = []
        self._callbacks: list[Callable[[Alert], None]] = []
        self._lock = threading.Lock()
        self._alert_count = 0

        # Subscribe to events
        event_bus.subscribe("alerting", self._on_event)

        # Add default rules
        self._add_default_rules()

    def _add_default_rules(self) -> None:
        """Add default alerting rules."""
        # Rule: Alert on critical errors
        def critical_error_rule(event: FlowEvent) -> Alert | None:
            if event.severity == EventSeverity.CRITICAL:
                return Alert(
                    alert_id=self._gen_id(),
                    title="Critical Error",
                    description=event.summary,
                    severity=AlertSeverity.CRITICAL,
                    timestamp=datetime.utcnow(),
                    source_event=event,
                )
            return None

        # Rule: Alert on rollbacks
        def rollback_rule(event: FlowEvent) -> Alert | None:
            if "rollback" in event.event_name.lower():
                return Alert(
                    alert_id=self._gen_id(),
                    title="Protocol Rollback",
                    description=f"Rollback triggered: {event.summary}",
                    severity=AlertSeverity.WARNING,
                    timestamp=datetime.utcnow(),
                    source_event=event,
                )
            return None

        # Rule: Alert on workflow failures
        def workflow_failure_rule(event: FlowEvent) -> Alert | None:
            if (event.flow_type == FlowType.WORKFLOW and
                event.severity == EventSeverity.ERROR):
                return Alert(
                    alert_id=self._gen_id(),
                    title="Workflow Failed",
                    description=event.summary,
                    severity=AlertSeverity.WARNING,
                    timestamp=datetime.utcnow(),
                    source_event=event,
                )
            return None

        self._rules.extend([
            critical_error_rule,
            rollback_rule,
            workflow_failure_rule,
        ])

    def _gen_id(self) -> str:
        """Generate alert ID."""
        self._alert_count += 1
        return f"alert_{self._alert_count}"

    def _on_event(self, event: FlowEvent) -> None:
        """Process an event through rules."""
        for rule in self._rules:
            try:
                alert = rule(event)
                if alert:
                    self._emit_alert(alert)
            except Exception:
                pass

    def _emit_alert(self, alert: Alert) -> None:
        """Emit an alert."""
        with self._lock:
            self.alerts.append(alert)

            # Keep only recent alerts
            if len(self.alerts) > 1000:
                self.alerts = self.alerts[-500:]

        # Notify callbacks
        for callback in self._callbacks:
            try:
                callback(alert)
            except Exception:
                pass

    def add_rule(self, rule: Callable[[FlowEvent], Alert | None]) -> None:
        """Add a custom alerting rule."""
        self._rules.append(rule)

    def add_callback(self, callback: Callable[[Alert], None]) -> None:
        """Add an alert callback."""
        self._callbacks.append(callback)

    def get_alerts(
        self,
        severity: AlertSeverity | None = None,
        acknowledged: bool | None = None,
        limit: int = 100,
    ) -> list[Alert]:
        """Get alerts with optional filtering."""
        with self._lock:
            alerts = list(self.alerts)

        if severity:
            alerts = [a for a in alerts if a.severity == severity]
        if acknowledged is not None:
            alerts = [a for a in alerts if a.acknowledged == acknowledged]

        return alerts[-limit:]

    def acknowledge_alert(self, alert_id: str) -> bool:
        """Acknowledge an alert."""
        with self._lock:
            for alert in self.alerts:
                if alert.alert_id == alert_id:
                    alert.acknowledged = True
                    return True
        return False

    def get_summary(self) -> dict[str, Any]:
        """Get alert summary."""
        with self._lock:
            alerts = list(self.alerts)

        unacked = [a for a in alerts if not a.acknowledged]
        by_severity = defaultdict(int)
        for a in unacked:
            by_severity[a.severity.value] += 1

        return {
            "total_alerts": len(alerts),
            "unacknowledged": len(unacked),
            "by_severity": dict(by_severity),
            "recent_critical": [
                a.to_dict() for a in alerts
                if a.severity == AlertSeverity.CRITICAL
            ][-5:],
        }


# ==================== Enhanced Observation Manager ====================

class EnhancedObservationManager(ObservationManager):
    """
    ObservationManager with analytics, persistence, and alerting.
    """

    def __init__(
        self,
        max_history: int = 10000,
        persist_to: str | Path | None = None,
        enable_alerting: bool = True,
    ):
        super().__init__(max_history)

        # Analytics
        self.analytics = FlowAnalytics(self.event_bus)

        # Persistence
        self.persistence: EventPersistence | None = None
        if persist_to:
            self.persistence = EventPersistence(persist_to)
            self.event_bus.subscribe("persistence", self.persistence.write_event)

        # Alerting
        self.alerting: AlertingSystem | None = None
        if enable_alerting:
            self.alerting = AlertingSystem(self.event_bus)

    def stop(self) -> None:
        """Stop and flush persistence."""
        super().stop()
        if self.persistence:
            self.persistence.flush()

    def get_analytics_summary(self) -> dict[str, Any]:
        """Get comprehensive analytics summary."""
        flow_metrics = self.analytics.get_flow_metrics()
        agent_metrics = self.analytics.get_agent_metrics()

        return {
            "flow_metrics": {
                ft.value: fm.to_dict()
                for ft, fm in flow_metrics.items()
            },
            "agent_metrics": {
                aid: am.to_dict()
                for aid, am in (agent_metrics.items() if isinstance(agent_metrics, dict) else {})
            },
            "throughput_trend": self.analytics.get_throughput_trend(),
            "error_summary": self.analytics.get_error_summary(),
            "anomalies": self.analytics.detect_anomalies(),
            "alerts": self.alerting.get_summary() if self.alerting else None,
        }

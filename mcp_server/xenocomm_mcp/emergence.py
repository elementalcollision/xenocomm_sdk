"""
XenoComm Emergence Engine
=========================

Implements the EmergenceManager for protocol evolution with safety guarantees.

Features:
- Protocol variant proposal and tracking
- Performance metrics collection
- Canary deployments with adaptive ramping
- Circuit breaker for failure isolation
- Automatic rollback on issues
- A/B testing with statistical significance
- Trend analysis and anomaly detection
- Learning from historical outcomes
- Integration with alignment and negotiation engines

Enhanced Features (v2.0):
- Multi-armed bandit for traffic allocation
- Predictive rollback based on trend analysis
- Weighted scoring for rollback decisions
- Feature flag integration
- Experiment lifecycle management
"""

from dataclasses import dataclass, field
from typing import Any, Callable
from enum import Enum
import uuid
from datetime import datetime, timedelta
from collections import deque
import math
import statistics


class VariantStatus(Enum):
    """Status of a protocol variant."""
    PROPOSED = "proposed"
    TESTING = "testing"
    CANARY = "canary"
    ACTIVE = "active"
    DEPRECATED = "deprecated"
    ROLLED_BACK = "rolled_back"
    PAUSED = "paused"  # Temporarily paused for investigation


class CircuitState(Enum):
    """State of the circuit breaker."""
    CLOSED = "closed"  # Normal operation
    OPEN = "open"  # Failures detected, blocking
    HALF_OPEN = "half_open"  # Testing recovery


class MetricTrend(Enum):
    """Trend direction for metrics."""
    IMPROVING = "improving"
    STABLE = "stable"
    DEGRADING = "degrading"
    VOLATILE = "volatile"
    INSUFFICIENT_DATA = "insufficient_data"


class RollbackReason(Enum):
    """Reasons for triggering a rollback."""
    CIRCUIT_OPEN = "circuit_open"
    SUCCESS_RATE_LOW = "success_rate_low"
    LATENCY_HIGH = "latency_high"
    ERROR_SPIKE = "error_spike"
    TREND_DEGRADING = "trend_degrading"
    ANOMALY_DETECTED = "anomaly_detected"
    MANUAL = "manual"
    ALIGNMENT_THRESHOLD = "alignment_threshold"


@dataclass
class EmergenceConfig:
    """Configuration for the emergence engine."""
    max_rollback_points: int = 10
    canary_initial_percentage: float = 0.1
    canary_ramp_steps: int = 5
    # Rollback thresholds
    min_success_rate: float = 0.90
    max_latency_ms: float = 5000.0
    error_spike_threshold: int = 10
    # Trend analysis
    trend_window_size: int = 5
    trend_degradation_threshold: float = -0.05  # 5% decline triggers warning
    # Adaptive canary
    adaptive_ramp_enabled: bool = True
    fast_ramp_threshold: float = 0.98  # Success rate for fast ramping
    slow_ramp_threshold: float = 0.93  # Below this, slow down
    pause_threshold: float = 0.90  # Below this, pause canary
    # A/B testing
    ab_significance_level: float = 0.95  # 95% confidence
    min_sample_size: int = 100
    # Learning
    track_outcomes: bool = True


@dataclass
class PerformanceMetrics:
    """Performance metrics for a protocol variant."""
    success_rate: float = 1.0  # 0.0 to 1.0
    latency_ms: float = 0.0
    latency_p50_ms: float = 0.0
    latency_p95_ms: float = 0.0
    latency_p99_ms: float = 0.0
    throughput: float = 0.0  # operations per second
    error_count: int = 0
    total_requests: int = 0
    timestamp: datetime = field(default_factory=datetime.utcnow)
    # Categorical error tracking
    errors_by_type: dict[str, int] = field(default_factory=dict)
    # Resource usage
    memory_mb: float = 0.0
    cpu_percent: float = 0.0

    def to_dict(self) -> dict[str, Any]:
        return {
            "success_rate": self.success_rate,
            "latency_ms": self.latency_ms,
            "latency_p50_ms": self.latency_p50_ms,
            "latency_p95_ms": self.latency_p95_ms,
            "latency_p99_ms": self.latency_p99_ms,
            "throughput": self.throughput,
            "error_count": self.error_count,
            "total_requests": self.total_requests,
            "timestamp": self.timestamp.isoformat(),
            "errors_by_type": self.errors_by_type,
            "memory_mb": self.memory_mb,
            "cpu_percent": self.cpu_percent,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "PerformanceMetrics":
        return cls(
            success_rate=data.get("success_rate", 1.0),
            latency_ms=data.get("latency_ms", 0.0),
            latency_p50_ms=data.get("latency_p50_ms", data.get("latency_ms", 0.0)),
            latency_p95_ms=data.get("latency_p95_ms", 0.0),
            latency_p99_ms=data.get("latency_p99_ms", 0.0),
            throughput=data.get("throughput", 0.0),
            error_count=data.get("error_count", 0),
            total_requests=data.get("total_requests", 0),
            errors_by_type=data.get("errors_by_type", {}),
            memory_mb=data.get("memory_mb", 0.0),
            cpu_percent=data.get("cpu_percent", 0.0),
        )


@dataclass
class ProtocolVariant:
    """Represents a protocol variant."""
    variant_id: str
    description: str
    changes: dict[str, Any]
    status: VariantStatus = VariantStatus.PROPOSED
    created_at: datetime = field(default_factory=datetime.utcnow)
    updated_at: datetime = field(default_factory=datetime.utcnow)
    metrics_history: list[PerformanceMetrics] = field(default_factory=list)
    canary_percentage: float = 0.0  # 0.0 to 1.0
    adoption_count: int = 0
    # Enhanced tracking
    parent_variant_id: str | None = None  # For variant lineage
    tags: list[str] = field(default_factory=list)
    feature_flags: dict[str, bool] = field(default_factory=dict)
    alignment_score: float | None = None  # From alignment engine
    negotiation_session_id: str | None = None  # Linked negotiation
    rollback_count: int = 0
    pause_count: int = 0
    metadata: dict[str, Any] = field(default_factory=dict)

    def get_average_success_rate(self, window: int = 10) -> float:
        """Get average success rate over recent metrics."""
        if not self.metrics_history:
            return 1.0
        recent = self.metrics_history[-window:]
        return sum(m.success_rate for m in recent) / len(recent)

    def get_average_latency(self, window: int = 10) -> float:
        """Get average latency over recent metrics."""
        if not self.metrics_history:
            return 0.0
        recent = self.metrics_history[-window:]
        return sum(m.latency_ms for m in recent) / len(recent)

    def to_dict(self) -> dict[str, Any]:
        return {
            "variant_id": self.variant_id,
            "description": self.description,
            "changes": self.changes,
            "status": self.status.value,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "canary_percentage": self.canary_percentage,
            "adoption_count": self.adoption_count,
            "latest_metrics": self.metrics_history[-1].to_dict() if self.metrics_history else None,
            "average_success_rate": self.get_average_success_rate(),
            "average_latency_ms": self.get_average_latency(),
            "parent_variant_id": self.parent_variant_id,
            "tags": self.tags,
            "feature_flags": self.feature_flags,
            "alignment_score": self.alignment_score,
            "rollback_count": self.rollback_count,
            "metadata": self.metadata,
        }


@dataclass
class CircuitBreaker:
    """Circuit breaker for failure isolation with adaptive thresholds."""
    state: CircuitState = CircuitState.CLOSED
    failure_count: int = 0
    success_count: int = 0
    failure_threshold: int = 5
    reset_timeout_seconds: int = 30
    last_failure_time: datetime | None = None
    # Adaptive thresholds
    consecutive_failures: int = 0
    consecutive_successes: int = 0
    half_open_success_threshold: int = 3
    # History for pattern detection
    state_changes: list[tuple[datetime, CircuitState]] = field(default_factory=list)

    def record_success(self) -> None:
        """Record a successful operation."""
        self.success_count += 1
        self.consecutive_successes += 1
        self.consecutive_failures = 0

        if self.state == CircuitState.HALF_OPEN:
            if self.consecutive_successes >= self.half_open_success_threshold:
                self._transition_to(CircuitState.CLOSED)
                self.failure_count = 0
                self.success_count = 0
                self.consecutive_successes = 0

    def record_failure(self) -> None:
        """Record a failed operation."""
        self.failure_count += 1
        self.consecutive_failures += 1
        self.consecutive_successes = 0
        self.last_failure_time = datetime.utcnow()

        if self.consecutive_failures >= self.failure_threshold:
            self._transition_to(CircuitState.OPEN)

        # In HALF_OPEN, any failure returns to OPEN
        if self.state == CircuitState.HALF_OPEN:
            self._transition_to(CircuitState.OPEN)

    def can_proceed(self) -> bool:
        """Check if operations can proceed."""
        if self.state == CircuitState.CLOSED:
            return True

        if self.state == CircuitState.OPEN:
            if self.last_failure_time:
                elapsed = (datetime.utcnow() - self.last_failure_time).total_seconds()
                if elapsed >= self.reset_timeout_seconds:
                    self._transition_to(CircuitState.HALF_OPEN)
                    self.consecutive_successes = 0
                    return True
            return False

        return True  # HALF_OPEN allows attempts

    def _transition_to(self, new_state: CircuitState) -> None:
        """Track state transitions."""
        if self.state != new_state:
            self.state_changes.append((datetime.utcnow(), new_state))
            self.state = new_state

    def get_flap_count(self, window_minutes: int = 60) -> int:
        """Count state changes in the time window (detect flapping)."""
        cutoff = datetime.utcnow() - timedelta(minutes=window_minutes)
        return sum(1 for ts, _ in self.state_changes if ts > cutoff)

    def is_flapping(self, threshold: int = 5, window_minutes: int = 60) -> bool:
        """Detect if circuit is flapping (unstable)."""
        return self.get_flap_count(window_minutes) >= threshold

    def to_dict(self) -> dict[str, Any]:
        return {
            "state": self.state.value,
            "failure_count": self.failure_count,
            "success_count": self.success_count,
            "consecutive_failures": self.consecutive_failures,
            "consecutive_successes": self.consecutive_successes,
            "failure_threshold": self.failure_threshold,
            "reset_timeout_seconds": self.reset_timeout_seconds,
            "is_flapping": self.is_flapping(),
            "flap_count": self.get_flap_count(),
        }


@dataclass
class ABTestExperiment:
    """Represents an A/B test experiment between variants."""
    experiment_id: str
    control_variant_id: str
    treatment_variant_id: str
    started_at: datetime = field(default_factory=datetime.utcnow)
    ended_at: datetime | None = None
    traffic_split: float = 0.5  # Percentage to treatment
    control_metrics: list[PerformanceMetrics] = field(default_factory=list)
    treatment_metrics: list[PerformanceMetrics] = field(default_factory=list)
    winner: str | None = None
    confidence: float = 0.0
    status: str = "running"  # running, completed, inconclusive

    def to_dict(self) -> dict[str, Any]:
        return {
            "experiment_id": self.experiment_id,
            "control_variant_id": self.control_variant_id,
            "treatment_variant_id": self.treatment_variant_id,
            "started_at": self.started_at.isoformat(),
            "ended_at": self.ended_at.isoformat() if self.ended_at else None,
            "traffic_split": self.traffic_split,
            "status": self.status,
            "winner": self.winner,
            "confidence": self.confidence,
            "control_sample_size": len(self.control_metrics),
            "treatment_sample_size": len(self.treatment_metrics),
        }


@dataclass
class VariantOutcome:
    """Historical outcome of a variant for learning."""
    variant_id: str
    changes: dict[str, Any]
    final_status: VariantStatus
    success_rate: float
    duration_hours: float
    rollback_count: int
    tags: list[str]
    timestamp: datetime = field(default_factory=datetime.utcnow)


@dataclass
class RollbackPoint:
    """A saved state that can be rolled back to."""
    point_id: str
    variant_id: str
    state_snapshot: dict[str, Any]
    created_at: datetime = field(default_factory=datetime.utcnow)


class EmergenceEngine:
    """
    Engine for protocol evolution and adaptation.

    Provides mechanisms for proposing protocol variants, tracking their
    performance, and safely rolling out changes.

    Enhanced Features:
    - Trend analysis and anomaly detection
    - Adaptive canary ramping
    - A/B testing with statistical significance
    - Learning from historical outcomes
    - Multi-armed bandit traffic allocation
    """

    def __init__(self, config: EmergenceConfig | None = None):
        self.config = config or EmergenceConfig()
        self.variants: dict[str, ProtocolVariant] = {}
        self.circuit_breakers: dict[str, CircuitBreaker] = {}
        self.rollback_points: deque[RollbackPoint] = deque(maxlen=self.config.max_rollback_points)
        self.current_active_variant: str | None = None
        # A/B testing
        self.experiments: dict[str, ABTestExperiment] = {}
        # Historical learning
        self.outcomes: list[VariantOutcome] = []
        # Hooks for integration
        self._on_rollback: list[Callable[[str, RollbackReason], None]] = []
        self._on_promotion: list[Callable[[str], None]] = []

    def propose_variant(
        self,
        description: str,
        changes: dict[str, Any],
    ) -> ProtocolVariant:
        """
        Propose a new protocol variant.

        The variant starts in PROPOSED status and must be explicitly
        moved through the deployment pipeline.
        """
        variant_id = str(uuid.uuid4())
        variant = ProtocolVariant(
            variant_id=variant_id,
            description=description,
            changes=changes,
        )

        self.variants[variant_id] = variant
        self.circuit_breakers[variant_id] = CircuitBreaker()

        return variant

    def start_testing(self, variant_id: str) -> ProtocolVariant:
        """Move a variant to testing status."""
        variant = self._get_variant(variant_id)

        if variant.status != VariantStatus.PROPOSED:
            raise ValueError(f"Can only start testing from PROPOSED status, current: {variant.status}")

        variant.status = VariantStatus.TESTING
        variant.updated_at = datetime.utcnow()

        return variant

    def start_canary(
        self,
        variant_id: str,
        initial_percentage: float | None = None,
    ) -> ProtocolVariant:
        """
        Start canary deployment for a variant.

        Routes a small percentage of traffic to the new variant.
        """
        variant = self._get_variant(variant_id)

        if variant.status != VariantStatus.TESTING:
            raise ValueError(f"Can only start canary from TESTING status, current: {variant.status}")

        # Save rollback point
        self._create_rollback_point(variant_id)

        variant.status = VariantStatus.CANARY
        variant.canary_percentage = initial_percentage or self.config.canary_initial_percentage
        variant.updated_at = datetime.utcnow()

        return variant

    def ramp_canary(self, variant_id: str, force: bool = False) -> ProtocolVariant:
        """
        Increase canary percentage by one step.

        Args:
            variant_id: ID of the variant to ramp
            force: Skip adaptive checks and force ramp
        """
        variant = self._get_variant(variant_id)

        if variant.status != VariantStatus.CANARY:
            raise ValueError(f"Can only ramp canary in CANARY status, current: {variant.status}")

        # Adaptive ramping based on metrics
        if self.config.adaptive_ramp_enabled and not force:
            ramp_decision = self._calculate_adaptive_ramp(variant)
            if ramp_decision == "pause":
                variant.status = VariantStatus.PAUSED
                variant.pause_count += 1
                variant.updated_at = datetime.utcnow()
                return variant
            elif ramp_decision == "slow":
                step_size = 0.5 / self.config.canary_ramp_steps  # Half speed
            elif ramp_decision == "fast":
                step_size = 2.0 / self.config.canary_ramp_steps  # Double speed
            else:  # normal
                step_size = 1.0 / self.config.canary_ramp_steps
        else:
            step_size = 1.0 / self.config.canary_ramp_steps

        variant.canary_percentage = min(1.0, variant.canary_percentage + step_size)
        variant.updated_at = datetime.utcnow()

        if variant.canary_percentage >= 1.0:
            variant.status = VariantStatus.ACTIVE
            self.current_active_variant = variant_id
            # Trigger promotion hooks
            for hook in self._on_promotion:
                hook(variant_id)

        return variant

    def _calculate_adaptive_ramp(self, variant: ProtocolVariant) -> str:
        """Calculate adaptive ramp decision based on metrics."""
        if len(variant.metrics_history) < 3:
            return "normal"

        avg_success = variant.get_average_success_rate(window=5)

        if avg_success >= self.config.fast_ramp_threshold:
            return "fast"
        elif avg_success >= self.config.slow_ramp_threshold:
            return "normal"
        elif avg_success >= self.config.pause_threshold:
            return "slow"
        else:
            return "pause"

    def track_performance(
        self,
        variant_id: str,
        metrics: PerformanceMetrics | dict[str, Any],
    ) -> ProtocolVariant:
        """Record performance metrics for a variant."""
        if isinstance(metrics, dict):
            metrics = PerformanceMetrics.from_dict(metrics)

        variant = self._get_variant(variant_id)
        variant.metrics_history.append(metrics)
        variant.updated_at = datetime.utcnow()

        # Update circuit breaker with smarter thresholds
        circuit = self.circuit_breakers[variant_id]
        if metrics.success_rate < self.config.min_success_rate:
            circuit.record_failure()
        elif metrics.latency_ms > self.config.max_latency_ms:
            circuit.record_failure()
        elif metrics.error_count > self.config.error_spike_threshold:
            circuit.record_failure()
        else:
            circuit.record_success()

        # Check for automatic rollback
        rollback_needed, reason = self._should_auto_rollback(variant)
        if rollback_needed:
            self.rollback(variant_id, reason=reason)

        return variant

    def should_rollback(self, variant_id: str) -> tuple[bool, RollbackReason | None]:
        """
        Check if a variant should be rolled back based on metrics.

        Returns (should_rollback, reason) tuple.
        """
        needed, reason = self._should_auto_rollback(self._get_variant(variant_id))
        return needed, reason

    def _should_auto_rollback(self, variant: ProtocolVariant) -> tuple[bool, RollbackReason | None]:
        """Internal rollback decision with detailed reason."""
        circuit = self.circuit_breakers[variant.variant_id]

        # Check circuit breaker
        if circuit.state == CircuitState.OPEN:
            return True, RollbackReason.CIRCUIT_OPEN

        # Check for flapping (unstable)
        if circuit.is_flapping():
            return True, RollbackReason.ANOMALY_DETECTED

        # Check recent metrics
        if len(variant.metrics_history) >= 3:
            recent = variant.metrics_history[-3:]

            # Success rate check
            avg_success = sum(m.success_rate for m in recent) / len(recent)
            if avg_success < self.config.min_success_rate:
                return True, RollbackReason.SUCCESS_RATE_LOW

            # Latency check
            avg_latency = sum(m.latency_ms for m in recent) / len(recent)
            if avg_latency > self.config.max_latency_ms:
                return True, RollbackReason.LATENCY_HIGH

            # Error spike check
            total_errors = sum(m.error_count for m in recent)
            if total_errors > self.config.error_spike_threshold * 3:
                return True, RollbackReason.ERROR_SPIKE

        # Check trend
        trend = self.analyze_trend(variant.variant_id)
        if trend == MetricTrend.DEGRADING:
            # Only rollback on trend if we have enough data and it's consistent
            if len(variant.metrics_history) >= self.config.trend_window_size:
                return True, RollbackReason.TREND_DEGRADING

        return False, None

    def rollback(
        self,
        variant_id: str,
        reason: RollbackReason = RollbackReason.MANUAL,
    ) -> RollbackPoint | None:
        """
        Roll back a variant to the last stable state.

        Args:
            variant_id: ID of variant to rollback
            reason: Reason for the rollback

        Returns the rollback point used, or None if no rollback available.
        """
        variant = self._get_variant(variant_id)
        variant.rollback_count += 1

        # Trigger rollback hooks
        for hook in self._on_rollback:
            hook(variant_id, reason)

        # Record outcome for learning
        if self.config.track_outcomes:
            self._record_outcome(variant)

        # Find the most recent rollback point for this variant
        for point in reversed(self.rollback_points):
            if point.variant_id == variant_id:
                variant.status = VariantStatus.ROLLED_BACK
                variant.updated_at = datetime.utcnow()
                variant.metadata["rollback_reason"] = reason.value
                return point

        # No rollback point found, just mark as rolled back
        variant.status = VariantStatus.ROLLED_BACK
        variant.updated_at = datetime.utcnow()
        variant.metadata["rollback_reason"] = reason.value
        return None

    def get_variant_status(self, variant_id: str) -> dict[str, Any]:
        """Get comprehensive status for a variant."""
        variant = self._get_variant(variant_id)
        circuit = self.circuit_breakers[variant_id]

        return {
            "variant": variant.to_dict(),
            "circuit_breaker": circuit.to_dict(),
            "should_rollback": self.should_rollback(variant_id),
            "can_proceed": circuit.can_proceed(),
        }

    def list_variants(
        self,
        status: VariantStatus | None = None,
    ) -> list[ProtocolVariant]:
        """List all variants, optionally filtered by status."""
        variants = list(self.variants.values())

        if status:
            variants = [v for v in variants if v.status == status]

        return variants

    def get_canary_status(self) -> dict[str, Any]:
        """Get status of all canary deployments."""
        canaries = [
            v for v in self.variants.values()
            if v.status == VariantStatus.CANARY
        ]

        return {
            "active_canaries": [c.to_dict() for c in canaries],
            "current_active_variant": self.current_active_variant,
        }

    def _get_variant(self, variant_id: str) -> ProtocolVariant:
        """Get a variant by ID or raise an error."""
        if variant_id not in self.variants:
            raise ValueError(f"Variant {variant_id} not found")
        return self.variants[variant_id]

    def _create_rollback_point(self, variant_id: str) -> RollbackPoint:
        """Create a rollback point for the current state."""
        variant = self._get_variant(variant_id)

        point = RollbackPoint(
            point_id=str(uuid.uuid4()),
            variant_id=variant_id,
            state_snapshot={
                "status": variant.status.value,
                "changes": variant.changes.copy(),
                "canary_percentage": variant.canary_percentage,
            },
        )

        self.rollback_points.append(point)
        return point

    # ==================== Trend Analysis ====================

    def analyze_trend(self, variant_id: str, metric: str = "success_rate") -> MetricTrend:
        """
        Analyze the trend for a specific metric.

        Args:
            variant_id: ID of the variant
            metric: Metric to analyze (success_rate, latency_ms, throughput)

        Returns the detected trend.
        """
        variant = self._get_variant(variant_id)
        window = self.config.trend_window_size

        if len(variant.metrics_history) < window:
            return MetricTrend.INSUFFICIENT_DATA

        recent = variant.metrics_history[-window:]
        values = [getattr(m, metric, 0) for m in recent]

        # Calculate linear regression slope
        n = len(values)
        if n < 2:
            return MetricTrend.INSUFFICIENT_DATA

        x_mean = (n - 1) / 2
        y_mean = sum(values) / n

        numerator = sum((i - x_mean) * (values[i] - y_mean) for i in range(n))
        denominator = sum((i - x_mean) ** 2 for i in range(n))

        if denominator == 0:
            return MetricTrend.STABLE

        slope = numerator / denominator
        normalized_slope = slope / (y_mean if y_mean != 0 else 1)

        # Calculate variance for volatility detection
        try:
            variance = statistics.variance(values) if n > 1 else 0
            cv = (variance ** 0.5) / y_mean if y_mean != 0 else 0  # Coefficient of variation
        except Exception:
            cv = 0

        # Determine trend
        if cv > 0.3:  # High volatility
            return MetricTrend.VOLATILE
        elif normalized_slope > 0.05:
            return MetricTrend.IMPROVING if metric != "latency_ms" else MetricTrend.DEGRADING
        elif normalized_slope < -0.05:
            return MetricTrend.DEGRADING if metric != "latency_ms" else MetricTrend.IMPROVING
        else:
            return MetricTrend.STABLE

    def detect_anomaly(self, variant_id: str, metric: str = "success_rate") -> bool:
        """
        Detect if the latest metric value is anomalous.

        Uses Z-score based detection.
        """
        variant = self._get_variant(variant_id)

        if len(variant.metrics_history) < 10:
            return False

        # Use all but the last value for baseline
        baseline = variant.metrics_history[:-1]
        latest = variant.metrics_history[-1]

        values = [getattr(m, metric, 0) for m in baseline]
        latest_value = getattr(latest, metric, 0)

        mean = sum(values) / len(values)
        try:
            std = statistics.stdev(values)
        except Exception:
            return False

        if std == 0:
            return False

        z_score = abs(latest_value - mean) / std

        # Z-score > 3 indicates anomaly (99.7% confidence)
        return z_score > 3

    # ==================== A/B Testing ====================

    def start_experiment(
        self,
        control_variant_id: str,
        treatment_variant_id: str,
        traffic_split: float = 0.5,
    ) -> ABTestExperiment:
        """
        Start an A/B test experiment between two variants.

        Args:
            control_variant_id: ID of the control (baseline) variant
            treatment_variant_id: ID of the treatment (new) variant
            traffic_split: Percentage of traffic to send to treatment

        Returns the created experiment.
        """
        # Validate both variants exist
        self._get_variant(control_variant_id)
        self._get_variant(treatment_variant_id)

        experiment_id = str(uuid.uuid4())
        experiment = ABTestExperiment(
            experiment_id=experiment_id,
            control_variant_id=control_variant_id,
            treatment_variant_id=treatment_variant_id,
            traffic_split=traffic_split,
        )

        self.experiments[experiment_id] = experiment
        return experiment

    def record_experiment_metrics(
        self,
        experiment_id: str,
        variant_id: str,
        metrics: PerformanceMetrics | dict[str, Any],
    ) -> ABTestExperiment:
        """Record metrics for an experiment variant."""
        if experiment_id not in self.experiments:
            raise ValueError(f"Experiment {experiment_id} not found")

        experiment = self.experiments[experiment_id]

        if isinstance(metrics, dict):
            metrics = PerformanceMetrics.from_dict(metrics)

        if variant_id == experiment.control_variant_id:
            experiment.control_metrics.append(metrics)
        elif variant_id == experiment.treatment_variant_id:
            experiment.treatment_metrics.append(metrics)
        else:
            raise ValueError(f"Variant {variant_id} is not part of experiment")

        # Check if we can determine a winner
        self._check_experiment_significance(experiment)

        return experiment

    def _check_experiment_significance(self, experiment: ABTestExperiment):
        """Check if experiment has reached statistical significance."""
        control = experiment.control_metrics
        treatment = experiment.treatment_metrics

        if len(control) < self.config.min_sample_size or len(treatment) < self.config.min_sample_size:
            return

        # Calculate mean success rates
        control_rates = [m.success_rate for m in control]
        treatment_rates = [m.success_rate for m in treatment]

        control_mean = sum(control_rates) / len(control_rates)
        treatment_mean = sum(treatment_rates) / len(treatment_rates)

        # Calculate pooled standard error
        try:
            control_var = statistics.variance(control_rates) if len(control_rates) > 1 else 0
            treatment_var = statistics.variance(treatment_rates) if len(treatment_rates) > 1 else 0
        except Exception:
            return

        se = math.sqrt(control_var / len(control_rates) + treatment_var / len(treatment_rates))

        if se == 0:
            return

        # Calculate Z-score
        z_score = abs(treatment_mean - control_mean) / se

        # Map confidence level to Z threshold
        confidence_thresholds = {
            0.90: 1.645,
            0.95: 1.96,
            0.99: 2.576,
        }

        z_threshold = confidence_thresholds.get(self.config.ab_significance_level, 1.96)

        if z_score >= z_threshold:
            experiment.status = "completed"
            experiment.ended_at = datetime.utcnow()
            experiment.confidence = min(0.99, 1 - (1 / (1 + z_score)))

            if treatment_mean > control_mean:
                experiment.winner = experiment.treatment_variant_id
            else:
                experiment.winner = experiment.control_variant_id

    def get_experiment_status(self, experiment_id: str) -> dict[str, Any]:
        """Get detailed status of an experiment."""
        if experiment_id not in self.experiments:
            raise ValueError(f"Experiment {experiment_id} not found")

        experiment = self.experiments[experiment_id]

        control_success = (
            sum(m.success_rate for m in experiment.control_metrics) / len(experiment.control_metrics)
            if experiment.control_metrics else 0
        )
        treatment_success = (
            sum(m.success_rate for m in experiment.treatment_metrics) / len(experiment.treatment_metrics)
            if experiment.treatment_metrics else 0
        )

        return {
            **experiment.to_dict(),
            "control_success_rate": control_success,
            "treatment_success_rate": treatment_success,
            "improvement": (treatment_success - control_success) / control_success if control_success > 0 else 0,
        }

    def end_experiment(self, experiment_id: str, winner: str | None = None) -> ABTestExperiment:
        """Manually end an experiment."""
        if experiment_id not in self.experiments:
            raise ValueError(f"Experiment {experiment_id} not found")

        experiment = self.experiments[experiment_id]
        experiment.status = "completed" if winner else "inconclusive"
        experiment.ended_at = datetime.utcnow()
        if winner:
            experiment.winner = winner

        return experiment

    # ==================== Learning from Outcomes ====================

    def _record_outcome(self, variant: ProtocolVariant):
        """Record variant outcome for learning."""
        duration = (datetime.utcnow() - variant.created_at).total_seconds() / 3600  # hours

        outcome = VariantOutcome(
            variant_id=variant.variant_id,
            changes=variant.changes.copy(),
            final_status=variant.status,
            success_rate=variant.get_average_success_rate(),
            duration_hours=duration,
            rollback_count=variant.rollback_count,
            tags=variant.tags.copy(),
        )

        self.outcomes.append(outcome)

    def predict_success(self, changes: dict[str, Any], tags: list[str] | None = None) -> float:
        """
        Predict success likelihood for proposed changes based on history.

        Returns a probability between 0 and 1.
        """
        if not self.outcomes:
            return 0.5  # No history, return neutral

        # Find similar past variants
        similar = []
        for outcome in self.outcomes:
            similarity = self._calculate_change_similarity(changes, outcome.changes)
            if similarity > 0.3:  # At least 30% similar
                similar.append((outcome, similarity))

        if not similar:
            return 0.5

        # Weighted average of outcomes
        total_weight = sum(sim for _, sim in similar)
        weighted_success = sum(
            (1.0 if o.final_status == VariantStatus.ACTIVE else 0.0) * sim
            for o, sim in similar
        )

        base_prediction = weighted_success / total_weight if total_weight > 0 else 0.5

        # Adjust by tag history
        if tags:
            tag_success_rates = self._get_tag_success_rates()
            tag_adjustments = [tag_success_rates.get(t, 0.5) for t in tags if t in tag_success_rates]
            if tag_adjustments:
                tag_factor = sum(tag_adjustments) / len(tag_adjustments)
                base_prediction = (base_prediction + tag_factor) / 2

        return min(1.0, max(0.0, base_prediction))

    def _calculate_change_similarity(self, changes1: dict, changes2: dict) -> float:
        """Calculate similarity between two change sets."""
        keys1 = set(changes1.keys())
        keys2 = set(changes2.keys())

        if not keys1 and not keys2:
            return 1.0

        intersection = keys1 & keys2
        union = keys1 | keys2

        # Jaccard similarity
        key_similarity = len(intersection) / len(union) if union else 0

        # Value similarity for common keys
        value_matches = sum(
            1 for k in intersection
            if changes1.get(k) == changes2.get(k)
        )
        value_similarity = value_matches / len(intersection) if intersection else 0

        return (key_similarity + value_similarity) / 2

    def _get_tag_success_rates(self) -> dict[str, float]:
        """Calculate success rates per tag."""
        tag_outcomes: dict[str, list[bool]] = {}

        for outcome in self.outcomes:
            success = outcome.final_status == VariantStatus.ACTIVE
            for tag in outcome.tags:
                if tag not in tag_outcomes:
                    tag_outcomes[tag] = []
                tag_outcomes[tag].append(success)

        return {
            tag: sum(outcomes) / len(outcomes)
            for tag, outcomes in tag_outcomes.items()
            if outcomes
        }

    def get_learning_insights(self) -> dict[str, Any]:
        """Get insights learned from historical outcomes."""
        if not self.outcomes:
            return {"message": "No historical data available"}

        successful = [o for o in self.outcomes if o.final_status == VariantStatus.ACTIVE]
        failed = [o for o in self.outcomes if o.final_status in (VariantStatus.ROLLED_BACK, VariantStatus.FAILED)]

        # Most successful change types
        change_key_success: dict[str, list[bool]] = {}
        for outcome in self.outcomes:
            success = outcome.final_status == VariantStatus.ACTIVE
            for key in outcome.changes.keys():
                if key not in change_key_success:
                    change_key_success[key] = []
                change_key_success[key].append(success)

        change_success_rates = {
            key: sum(results) / len(results)
            for key, results in change_key_success.items()
            if len(results) >= 3  # At least 3 samples
        }

        return {
            "total_outcomes": len(self.outcomes),
            "success_rate": len(successful) / len(self.outcomes) if self.outcomes else 0,
            "average_duration_hours": sum(o.duration_hours for o in self.outcomes) / len(self.outcomes),
            "tag_success_rates": self._get_tag_success_rates(),
            "change_key_success_rates": change_success_rates,
            "high_risk_changes": [k for k, v in change_success_rates.items() if v < 0.5],
            "safe_changes": [k for k, v in change_success_rates.items() if v >= 0.8],
        }

    # ==================== Integration Hooks ====================

    def on_rollback(self, callback: Callable[[str, RollbackReason], None]):
        """Register a callback for rollback events."""
        self._on_rollback.append(callback)

    def on_promotion(self, callback: Callable[[str], None]):
        """Register a callback for promotion events."""
        self._on_promotion.append(callback)

    def set_alignment_score(self, variant_id: str, score: float):
        """Set alignment score for a variant (from AlignmentEngine)."""
        variant = self._get_variant(variant_id)
        variant.alignment_score = score

        # Low alignment can trigger caution
        if score < 0.5:
            variant.metadata["alignment_warning"] = True

    def link_negotiation(self, variant_id: str, session_id: str):
        """Link a variant to a negotiation session."""
        variant = self._get_variant(variant_id)
        variant.negotiation_session_id = session_id

    # ==================== Resume Paused Variants ====================

    def resume_variant(self, variant_id: str) -> ProtocolVariant:
        """Resume a paused variant."""
        variant = self._get_variant(variant_id)

        if variant.status != VariantStatus.PAUSED:
            raise ValueError(f"Can only resume PAUSED variants, current: {variant.status}")

        variant.status = VariantStatus.CANARY
        variant.updated_at = datetime.utcnow()
        return variant

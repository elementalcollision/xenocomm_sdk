"""
XenoComm Emergence Engine
=========================

Implements the EmergenceManager for protocol evolution with safety guarantees.

Features:
- Protocol variant proposal and tracking
- Performance metrics collection
- Canary deployments
- Circuit breaker for failure isolation
- Automatic rollback on issues
"""

from dataclasses import dataclass, field
from typing import Any
from enum import Enum
import uuid
from datetime import datetime
from collections import deque


class VariantStatus(Enum):
    """Status of a protocol variant."""
    PROPOSED = "proposed"
    TESTING = "testing"
    CANARY = "canary"
    ACTIVE = "active"
    DEPRECATED = "deprecated"
    ROLLED_BACK = "rolled_back"


class CircuitState(Enum):
    """State of the circuit breaker."""
    CLOSED = "closed"  # Normal operation
    OPEN = "open"  # Failures detected, blocking
    HALF_OPEN = "half_open"  # Testing recovery


@dataclass
class PerformanceMetrics:
    """Performance metrics for a protocol variant."""
    success_rate: float = 1.0  # 0.0 to 1.0
    latency_ms: float = 0.0
    throughput: float = 0.0  # operations per second
    error_count: int = 0
    total_requests: int = 0
    timestamp: datetime = field(default_factory=datetime.utcnow)

    def to_dict(self) -> dict[str, Any]:
        return {
            "success_rate": self.success_rate,
            "latency_ms": self.latency_ms,
            "throughput": self.throughput,
            "error_count": self.error_count,
            "total_requests": self.total_requests,
            "timestamp": self.timestamp.isoformat(),
        }


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
        }


@dataclass
class CircuitBreaker:
    """Circuit breaker for failure isolation."""
    state: CircuitState = CircuitState.CLOSED
    failure_count: int = 0
    success_count: int = 0
    failure_threshold: int = 5
    reset_timeout_seconds: int = 30
    last_failure_time: datetime | None = None

    def record_success(self) -> None:
        """Record a successful operation."""
        self.success_count += 1
        if self.state == CircuitState.HALF_OPEN:
            if self.success_count >= 3:
                self.state = CircuitState.CLOSED
                self.failure_count = 0
                self.success_count = 0

    def record_failure(self) -> None:
        """Record a failed operation."""
        self.failure_count += 1
        self.last_failure_time = datetime.utcnow()

        if self.failure_count >= self.failure_threshold:
            self.state = CircuitState.OPEN

    def can_proceed(self) -> bool:
        """Check if operations can proceed."""
        if self.state == CircuitState.CLOSED:
            return True

        if self.state == CircuitState.OPEN:
            if self.last_failure_time:
                elapsed = (datetime.utcnow() - self.last_failure_time).total_seconds()
                if elapsed >= self.reset_timeout_seconds:
                    self.state = CircuitState.HALF_OPEN
                    self.success_count = 0
                    return True
            return False

        return True  # HALF_OPEN allows attempts

    def to_dict(self) -> dict[str, Any]:
        return {
            "state": self.state.value,
            "failure_count": self.failure_count,
            "success_count": self.success_count,
            "failure_threshold": self.failure_threshold,
            "reset_timeout_seconds": self.reset_timeout_seconds,
        }


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
    """

    def __init__(
        self,
        max_rollback_points: int = 10,
        canary_initial_percentage: float = 0.1,
        canary_ramp_steps: int = 5,
    ):
        self.variants: dict[str, ProtocolVariant] = {}
        self.circuit_breakers: dict[str, CircuitBreaker] = {}
        self.rollback_points: deque[RollbackPoint] = deque(maxlen=max_rollback_points)
        self.canary_initial_percentage = canary_initial_percentage
        self.canary_ramp_steps = canary_ramp_steps
        self.current_active_variant: str | None = None

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
        variant.canary_percentage = initial_percentage or self.canary_initial_percentage
        variant.updated_at = datetime.utcnow()

        return variant

    def ramp_canary(self, variant_id: str) -> ProtocolVariant:
        """Increase canary percentage by one step."""
        variant = self._get_variant(variant_id)

        if variant.status != VariantStatus.CANARY:
            raise ValueError(f"Can only ramp canary in CANARY status, current: {variant.status}")

        step_size = 1.0 / self.canary_ramp_steps
        variant.canary_percentage = min(1.0, variant.canary_percentage + step_size)
        variant.updated_at = datetime.utcnow()

        if variant.canary_percentage >= 1.0:
            variant.status = VariantStatus.ACTIVE
            self.current_active_variant = variant_id

        return variant

    def track_performance(
        self,
        variant_id: str,
        metrics: PerformanceMetrics | dict[str, Any],
    ) -> ProtocolVariant:
        """Record performance metrics for a variant."""
        if isinstance(metrics, dict):
            metrics = PerformanceMetrics(
                success_rate=metrics.get("success_rate", 1.0),
                latency_ms=metrics.get("latency_ms", 0.0),
                throughput=metrics.get("throughput", 0.0),
                error_count=metrics.get("error_count", 0),
                total_requests=metrics.get("total_requests", 0),
            )

        variant = self._get_variant(variant_id)
        variant.metrics_history.append(metrics)
        variant.updated_at = datetime.utcnow()

        # Update circuit breaker
        circuit = self.circuit_breakers[variant_id]
        if metrics.success_rate < 0.95:
            circuit.record_failure()
        else:
            circuit.record_success()

        return variant

    def should_rollback(self, variant_id: str) -> bool:
        """
        Check if a variant should be rolled back based on metrics.

        Uses circuit breaker state and recent performance.
        """
        variant = self._get_variant(variant_id)
        circuit = self.circuit_breakers[variant_id]

        # Check circuit breaker
        if circuit.state == CircuitState.OPEN:
            return True

        # Check recent metrics
        if len(variant.metrics_history) >= 3:
            recent = variant.metrics_history[-3:]
            avg_success = sum(m.success_rate for m in recent) / len(recent)
            if avg_success < 0.9:
                return True

        return False

    def rollback(self, variant_id: str) -> RollbackPoint | None:
        """
        Roll back a variant to the last stable state.

        Returns the rollback point used, or None if no rollback available.
        """
        variant = self._get_variant(variant_id)

        # Find the most recent rollback point for this variant
        for point in reversed(self.rollback_points):
            if point.variant_id == variant_id:
                variant.status = VariantStatus.ROLLED_BACK
                variant.updated_at = datetime.utcnow()
                return point

        # No rollback point found, just mark as rolled back
        variant.status = VariantStatus.ROLLED_BACK
        variant.updated_at = datetime.utcnow()
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

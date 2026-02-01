"""
XenoComm Orchestrator
=====================

The orchestration layer that coordinates Alignment, Negotiation, and Emergence
engines into cohesive workflows for real-world agentic scenarios.

Key Workflows:
- Agent Onboarding: Register, verify capabilities, establish baseline
- Collaboration Setup: Alignment check → Negotiation → Session ready
- Protocol Evolution: Monitor → Propose → Test → Deploy with safety
- Multi-Agent Coordination: Orchestrate multiple agent pairs

Design Principles:
- Alignment is a prerequisite for negotiation
- Negotiation results inform emergence decisions
- Emergence feedback loops back to alignment verification
"""

from dataclasses import dataclass, field
from typing import Any, Callable
from enum import Enum
from datetime import datetime
import uuid

from .alignment import (
    AlignmentEngine, AgentContext, AlignmentResult, AlignmentStatus
)
from .negotiation import (
    NegotiationEngine, NegotiableParams, NegotiationSession, NegotiationState
)
from .emergence import (
    EmergenceEngine, ProtocolVariant, PerformanceMetrics, VariantStatus
)


class WorkflowState(Enum):
    """State of an orchestrated workflow."""
    PENDING = "pending"
    ALIGNING = "aligning"
    NEGOTIATING = "negotiating"
    ACTIVE = "active"
    EVOLVING = "evolving"
    SUSPENDED = "suspended"
    COMPLETED = "completed"
    FAILED = "failed"


class CollaborationReadiness(Enum):
    """Readiness level for agent collaboration."""
    NOT_READY = "not_ready"
    ALIGNMENT_NEEDED = "alignment_needed"
    NEGOTIATION_NEEDED = "negotiation_needed"
    READY = "ready"
    OPTIMAL = "optimal"  # Aligned + negotiated + stable protocol


@dataclass
class WorkflowMetrics:
    """Metrics collected during workflow execution."""
    alignment_duration_ms: float = 0.0
    negotiation_duration_ms: float = 0.0
    total_duration_ms: float = 0.0
    alignment_attempts: int = 0
    negotiation_attempts: int = 0
    alignment_score: float = 0.0
    success: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "alignment_duration_ms": self.alignment_duration_ms,
            "negotiation_duration_ms": self.negotiation_duration_ms,
            "total_duration_ms": self.total_duration_ms,
            "alignment_attempts": self.alignment_attempts,
            "negotiation_attempts": self.negotiation_attempts,
            "alignment_score": self.alignment_score,
            "success": self.success,
        }


@dataclass
class CollaborationSession:
    """
    Represents an active collaboration between two agents.

    Tracks the full lifecycle from alignment through active communication.
    """
    session_id: str
    agent_a_id: str
    agent_b_id: str
    state: WorkflowState = WorkflowState.PENDING
    alignment_results: dict[str, AlignmentResult] | None = None
    negotiation_session: NegotiationSession | None = None
    active_variant_id: str | None = None
    metrics: WorkflowMetrics = field(default_factory=WorkflowMetrics)
    created_at: datetime = field(default_factory=datetime.utcnow)
    updated_at: datetime = field(default_factory=datetime.utcnow)
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "session_id": self.session_id,
            "agent_a_id": self.agent_a_id,
            "agent_b_id": self.agent_b_id,
            "state": self.state.value,
            "alignment_results": {
                k: v.to_dict() for k, v in self.alignment_results.items()
            } if self.alignment_results else None,
            "negotiation_session": self.negotiation_session.to_dict() if self.negotiation_session else None,
            "active_variant_id": self.active_variant_id,
            "metrics": self.metrics.to_dict(),
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "metadata": self.metadata,
        }


@dataclass
class OrchestratorConfig:
    """Configuration for the orchestrator."""
    # Alignment thresholds
    min_alignment_confidence: float = 0.6
    required_aligned_strategies: int = 3  # Out of 5
    alignment_retry_limit: int = 3

    # Negotiation settings
    negotiation_timeout_ms: int = 30000
    auto_accept_threshold: float = 0.9  # Auto-accept if alignment is very high

    # Emergence settings
    enable_auto_evolution: bool = True
    evolution_check_interval_ms: int = 60000
    min_requests_before_evolution: int = 100

    # Workflow settings
    require_alignment_for_negotiation: bool = True
    require_negotiation_for_communication: bool = True


class XenoCommOrchestrator:
    """
    Central orchestrator for XenoComm agent coordination.

    Coordinates the three engines (Alignment, Negotiation, Emergence) into
    cohesive workflows for real-world agentic scenarios.
    """

    def __init__(
        self,
        config: OrchestratorConfig | None = None,
        alignment_engine: AlignmentEngine | None = None,
        negotiation_engine: NegotiationEngine | None = None,
        emergence_engine: EmergenceEngine | None = None,
    ):
        self.config = config or OrchestratorConfig()
        self.alignment = alignment_engine or AlignmentEngine()
        self.negotiation = negotiation_engine or NegotiationEngine()
        self.emergence = emergence_engine or EmergenceEngine()

        self.sessions: dict[str, CollaborationSession] = {}
        self.agent_registry: dict[str, AgentContext] = {}

        # Event hooks for extensibility
        self._hooks: dict[str, list[Callable]] = {
            "on_alignment_complete": [],
            "on_negotiation_complete": [],
            "on_session_ready": [],
            "on_evolution_triggered": [],
        }

    # =========================================================================
    # AGENT MANAGEMENT
    # =========================================================================

    def register_agent(self, context: AgentContext) -> dict[str, Any]:
        """
        Register an agent with the orchestrator.

        This is the entry point for any agent wanting to participate
        in XenoComm coordination.
        """
        self.agent_registry[context.agent_id] = context
        self.alignment.register_agent(context)

        return {
            "status": "registered",
            "agent_id": context.agent_id,
            "capabilities_count": len(context.capabilities),
            "ready_for_collaboration": True,
        }

    def get_agent(self, agent_id: str) -> AgentContext | None:
        """Get a registered agent's context."""
        return self.agent_registry.get(agent_id)

    def update_agent(self, agent_id: str, updates: dict[str, Any]) -> AgentContext:
        """Update an agent's context."""
        if agent_id not in self.agent_registry:
            raise ValueError(f"Agent {agent_id} not registered")

        context = self.agent_registry[agent_id]

        if "knowledge_domains" in updates:
            context.knowledge_domains = updates["knowledge_domains"]
        if "goals" in updates:
            context.goals = updates["goals"]
        if "terminology" in updates:
            context.terminology.update(updates["terminology"])
        if "assumptions" in updates:
            context.assumptions = updates["assumptions"]
        if "context_params" in updates:
            context.context_params.update(updates["context_params"])
        if "capabilities" in updates:
            context.capabilities.update(updates["capabilities"])

        return context

    # =========================================================================
    # COLLABORATION WORKFLOW
    # =========================================================================

    def initiate_collaboration(
        self,
        agent_a_id: str,
        agent_b_id: str,
        required_domains: list[str] | None = None,
        proposed_params: dict[str, Any] | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> CollaborationSession:
        """
        Initiate a full collaboration workflow between two agents.

        This is the main entry point for establishing agent collaboration.
        It orchestrates: Alignment → Negotiation → Session Ready

        Args:
            agent_a_id: Initiating agent
            agent_b_id: Target agent
            required_domains: Knowledge domains required for this collaboration
            proposed_params: Initial negotiation parameters
            metadata: Additional context for the collaboration

        Returns:
            CollaborationSession with current state and results
        """
        # Validate agents exist
        agent_a = self.agent_registry.get(agent_a_id)
        agent_b = self.agent_registry.get(agent_b_id)

        if not agent_a:
            raise ValueError(f"Agent {agent_a_id} not registered")
        if not agent_b:
            raise ValueError(f"Agent {agent_b_id} not registered")

        # Create session
        session = CollaborationSession(
            session_id=str(uuid.uuid4()),
            agent_a_id=agent_a_id,
            agent_b_id=agent_b_id,
            state=WorkflowState.ALIGNING,
            metadata=metadata or {},
        )

        start_time = datetime.utcnow()

        # Step 1: Alignment Check
        session.metrics.alignment_attempts = 1
        alignment_start = datetime.utcnow()

        alignment_results = self.alignment.full_alignment_check(
            agent_a, agent_b, required_domains
        )
        session.alignment_results = alignment_results

        alignment_end = datetime.utcnow()
        session.metrics.alignment_duration_ms = (
            (alignment_end - alignment_start).total_seconds() * 1000
        )

        # Calculate alignment score
        aligned_count = sum(
            1 for r in alignment_results.values()
            if r.status == AlignmentStatus.ALIGNED
        )
        partial_count = sum(
            1 for r in alignment_results.values()
            if r.status == AlignmentStatus.PARTIAL
        )
        session.metrics.alignment_score = (
            aligned_count + 0.5 * partial_count
        ) / len(alignment_results)

        # Trigger alignment hooks
        self._trigger_hooks("on_alignment_complete", session)

        # Check if alignment is sufficient
        if self.config.require_alignment_for_negotiation:
            if aligned_count < self.config.required_aligned_strategies:
                if session.metrics.alignment_score < self.config.min_alignment_confidence:
                    session.state = WorkflowState.FAILED
                    session.metadata["failure_reason"] = "Insufficient alignment"
                    self.sessions[session.session_id] = session
                    return session

        # Step 2: Negotiation
        session.state = WorkflowState.NEGOTIATING
        negotiation_start = datetime.utcnow()
        session.metrics.negotiation_attempts = 1

        # Use default params if none provided
        params = NegotiableParams.from_dict(proposed_params or {})

        # High alignment can trigger auto-optimized params
        if session.metrics.alignment_score >= self.config.auto_accept_threshold:
            params = self._optimize_params_for_agents(agent_a, agent_b, params)

        neg_session = self.negotiation.initiate_session(
            agent_a_id, agent_b_id, params
        )
        session.negotiation_session = neg_session

        negotiation_end = datetime.utcnow()
        session.metrics.negotiation_duration_ms = (
            (negotiation_end - negotiation_start).total_seconds() * 1000
        )

        # Trigger negotiation hooks
        self._trigger_hooks("on_negotiation_complete", session)

        # Step 3: Mark as active (negotiation still needs responder action)
        session.state = WorkflowState.ACTIVE
        session.metrics.success = True
        session.metrics.total_duration_ms = (
            (datetime.utcnow() - start_time).total_seconds() * 1000
        )

        # Trigger session ready hooks
        self._trigger_hooks("on_session_ready", session)

        self.sessions[session.session_id] = session
        session.updated_at = datetime.utcnow()

        return session

    def check_collaboration_readiness(
        self,
        agent_a_id: str,
        agent_b_id: str,
    ) -> dict[str, Any]:
        """
        Quick check of collaboration readiness without starting a session.

        Useful for UI/UX to show users whether agents can collaborate.
        """
        agent_a = self.agent_registry.get(agent_a_id)
        agent_b = self.agent_registry.get(agent_b_id)

        if not agent_a or not agent_b:
            return {
                "readiness": CollaborationReadiness.NOT_READY.value,
                "reason": "One or both agents not registered",
                "missing_agents": [
                    aid for aid in [agent_a_id, agent_b_id]
                    if aid not in self.agent_registry
                ],
            }

        # Quick alignment check
        results = self.alignment.full_alignment_check(agent_a, agent_b)

        aligned_count = sum(
            1 for r in results.values()
            if r.status == AlignmentStatus.ALIGNED
        )

        # Check for existing sessions
        existing_session = self._find_session(agent_a_id, agent_b_id)

        if existing_session and existing_session.state == WorkflowState.ACTIVE:
            readiness = CollaborationReadiness.OPTIMAL
        elif aligned_count >= self.config.required_aligned_strategies:
            readiness = CollaborationReadiness.NEGOTIATION_NEEDED
        elif aligned_count >= 2:
            readiness = CollaborationReadiness.ALIGNMENT_NEEDED
        else:
            readiness = CollaborationReadiness.NOT_READY

        return {
            "readiness": readiness.value,
            "alignment_summary": {
                name: result.status.value
                for name, result in results.items()
            },
            "aligned_strategies": aligned_count,
            "existing_session": existing_session.session_id if existing_session else None,
            "recommendations": self._generate_readiness_recommendations(results),
        }

    # =========================================================================
    # NEGOTIATION ORCHESTRATION
    # =========================================================================

    def complete_negotiation(
        self,
        session_id: str,
        responder_id: str,
        response: str,
        counter_params: dict[str, Any] | None = None,
    ) -> CollaborationSession:
        """
        Complete negotiation from the responder's side.

        Orchestrates the full negotiation flow based on response type.
        """
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Session {session_id} not found")

        if not session.negotiation_session:
            raise ValueError("Session has no active negotiation")

        neg_session = session.negotiation_session

        # Process based on response
        if response == "accept":
            self.negotiation.receive_proposal(neg_session.session_id, responder_id)
            self.negotiation.respond_accept(neg_session.session_id, responder_id)
            # Auto-finalize
            self.negotiation.finalize_session(
                neg_session.session_id, session.agent_a_id
            )
        elif response == "counter":
            self.negotiation.receive_proposal(neg_session.session_id, responder_id)
            self.negotiation.respond_counter(
                neg_session.session_id, responder_id, counter_params or {}
            )
        elif response == "reject":
            self.negotiation.respond_reject(
                neg_session.session_id, responder_id,
                counter_params.get("reason") if counter_params else None
            )
            session.state = WorkflowState.FAILED
            session.metadata["failure_reason"] = "Negotiation rejected"

        session.updated_at = datetime.utcnow()
        return session

    def accept_counter_and_finalize(
        self,
        session_id: str,
        initiator_id: str,
    ) -> CollaborationSession:
        """Accept a counter-proposal and finalize negotiation."""
        session = self.sessions.get(session_id)
        if not session or not session.negotiation_session:
            raise ValueError("Invalid session")

        neg_session = session.negotiation_session
        self.negotiation.accept_counter(neg_session.session_id, initiator_id)
        self.negotiation.finalize_session(neg_session.session_id, initiator_id)

        session.updated_at = datetime.utcnow()
        return session

    # =========================================================================
    # EMERGENCE ORCHESTRATION
    # =========================================================================

    def propose_protocol_evolution(
        self,
        session_id: str,
        description: str,
        changes: dict[str, Any],
    ) -> dict[str, Any]:
        """
        Propose a protocol evolution for a collaboration session.

        Links the emergence variant to the session for tracking.
        """
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Session {session_id} not found")

        variant = self.emergence.propose_variant(description, changes)

        # Link to session
        session.metadata["pending_variants"] = session.metadata.get(
            "pending_variants", []
        ) + [variant.variant_id]

        self._trigger_hooks("on_evolution_triggered", session, variant)

        return {
            "variant": variant.to_dict(),
            "session_id": session_id,
            "status": "proposed",
        }

    def evolve_session_protocol(
        self,
        session_id: str,
        variant_id: str,
    ) -> dict[str, Any]:
        """
        Progress a variant through the evolution pipeline for a session.

        Handles: testing → canary → active with safety checks.
        """
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Session {session_id} not found")

        variant = self.emergence.variants.get(variant_id)
        if not variant:
            raise ValueError(f"Variant {variant_id} not found")

        # Progress based on current status
        if variant.status == VariantStatus.PROPOSED:
            self.emergence.start_testing(variant_id)
            return {"variant": variant.to_dict(), "action": "started_testing"}

        elif variant.status == VariantStatus.TESTING:
            self.emergence.start_canary(variant_id)
            return {"variant": variant.to_dict(), "action": "started_canary"}

        elif variant.status == VariantStatus.CANARY:
            # Check if safe to ramp
            if self.emergence.should_rollback(variant_id):
                self.emergence.rollback(variant_id)
                return {"variant": variant.to_dict(), "action": "rolled_back"}
            else:
                self.emergence.ramp_canary(variant_id)
                if variant.status == VariantStatus.ACTIVE:
                    session.active_variant_id = variant_id
                    session.updated_at = datetime.utcnow()
                return {"variant": variant.to_dict(), "action": "ramped_canary"}

        elif variant.status == VariantStatus.ACTIVE:
            return {"variant": variant.to_dict(), "action": "already_active"}

        else:
            return {"variant": variant.to_dict(), "action": "no_action",
                    "reason": f"Variant in {variant.status.value} status"}

    def report_session_metrics(
        self,
        session_id: str,
        success_rate: float,
        latency_ms: float,
        throughput: float = 0.0,
        error_count: int = 0,
        total_requests: int = 0,
    ) -> dict[str, Any]:
        """
        Report performance metrics for a session.

        If the session has an active variant, metrics are tracked for
        emergence decisions.
        """
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Session {session_id} not found")

        result = {"session_id": session_id, "metrics_recorded": True}

        # Track for active variant if exists
        if session.active_variant_id:
            metrics = PerformanceMetrics(
                success_rate=success_rate,
                latency_ms=latency_ms,
                throughput=throughput,
                error_count=error_count,
                total_requests=total_requests,
            )
            self.emergence.track_performance(session.active_variant_id, metrics)

            # Check if evolution needed
            if self.emergence.should_rollback(session.active_variant_id):
                result["warning"] = "Variant performance degraded, consider rollback"
                result["should_rollback"] = True

        return result

    # =========================================================================
    # SESSION MANAGEMENT
    # =========================================================================

    def get_session(self, session_id: str) -> CollaborationSession | None:
        """Get a collaboration session by ID."""
        return self.sessions.get(session_id)

    def list_sessions(
        self,
        agent_id: str | None = None,
        state: WorkflowState | None = None,
    ) -> list[CollaborationSession]:
        """List sessions with optional filters."""
        sessions = list(self.sessions.values())

        if agent_id:
            sessions = [
                s for s in sessions
                if s.agent_a_id == agent_id or s.agent_b_id == agent_id
            ]

        if state:
            sessions = [s for s in sessions if s.state == state]

        return sessions

    def suspend_session(self, session_id: str, reason: str | None = None) -> CollaborationSession:
        """Temporarily suspend a collaboration session."""
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Session {session_id} not found")

        session.state = WorkflowState.SUSPENDED
        session.metadata["suspend_reason"] = reason
        session.updated_at = datetime.utcnow()

        return session

    def resume_session(self, session_id: str) -> CollaborationSession:
        """Resume a suspended session."""
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Session {session_id} not found")

        if session.state != WorkflowState.SUSPENDED:
            raise ValueError("Session is not suspended")

        session.state = WorkflowState.ACTIVE
        session.metadata.pop("suspend_reason", None)
        session.updated_at = datetime.utcnow()

        return session

    def close_session(self, session_id: str) -> CollaborationSession:
        """Close a collaboration session."""
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Session {session_id} not found")

        session.state = WorkflowState.COMPLETED
        session.updated_at = datetime.utcnow()

        # Close negotiation if active
        if session.negotiation_session:
            try:
                self.negotiation.close_session(
                    session.negotiation_session.session_id,
                    session.agent_a_id,
                    "Session closed"
                )
            except:
                pass  # Already closed

        return session

    # =========================================================================
    # HOOKS & EXTENSIBILITY
    # =========================================================================

    def add_hook(self, event: str, callback: Callable) -> None:
        """Add a callback hook for an event."""
        if event in self._hooks:
            self._hooks[event].append(callback)

    def remove_hook(self, event: str, callback: Callable) -> None:
        """Remove a callback hook."""
        if event in self._hooks and callback in self._hooks[event]:
            self._hooks[event].remove(callback)

    def _trigger_hooks(self, event: str, *args, **kwargs) -> None:
        """Trigger all hooks for an event."""
        for callback in self._hooks.get(event, []):
            try:
                callback(*args, **kwargs)
            except Exception:
                pass  # Don't let hook errors break workflow

    # =========================================================================
    # HELPER METHODS
    # =========================================================================

    def _find_session(
        self,
        agent_a_id: str,
        agent_b_id: str,
    ) -> CollaborationSession | None:
        """Find an existing session between two agents."""
        for session in self.sessions.values():
            if session.state not in (WorkflowState.COMPLETED, WorkflowState.FAILED):
                if (session.agent_a_id == agent_a_id and session.agent_b_id == agent_b_id) or \
                   (session.agent_a_id == agent_b_id and session.agent_b_id == agent_a_id):
                    return session
        return None

    def _optimize_params_for_agents(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
        base_params: NegotiableParams,
    ) -> NegotiableParams:
        """
        Optimize negotiation parameters based on agent capabilities.

        When agents are highly aligned, we can make smart defaults.
        """
        params = NegotiableParams.from_dict(base_params.to_dict())

        # Check for shared capabilities
        caps_a = set(agent_a.capabilities.keys())
        caps_b = set(agent_b.capabilities.keys())
        shared_caps = caps_a & caps_b

        # If both support msgpack, prefer it
        if "msgpack" in shared_caps:
            params.data_format = "msgpack"
            params.compression = None  # msgpack is already compact

        # If both support high throughput, increase message size
        if "high_throughput" in shared_caps:
            params.max_message_size = 10 * 1024 * 1024  # 10MB

        # If both support streaming
        if "streaming" in shared_caps:
            params.custom_params["streaming_enabled"] = True

        return params

    def _generate_readiness_recommendations(
        self,
        alignment_results: dict[str, AlignmentResult],
    ) -> list[str]:
        """Generate recommendations based on alignment results."""
        recommendations = []

        for name, result in alignment_results.items():
            if result.status == AlignmentStatus.MISALIGNED:
                recommendations.extend(result.recommendations[:2])
            elif result.status == AlignmentStatus.PARTIAL:
                if result.recommendations:
                    recommendations.append(result.recommendations[0])

        return recommendations[:5]  # Top 5 recommendations

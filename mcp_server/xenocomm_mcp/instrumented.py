"""
XenoComm Instrumented Components

Wrappers that add observation/telemetry to the core XenoComm components.
These emit events to the observation system for flow visualization.
"""

from __future__ import annotations

from typing import Any
from functools import wraps

from .orchestrator import XenoCommOrchestrator, CollaborationSession, OrchestratorConfig
from .alignment import AgentContext, AlignmentEngine
from .negotiation import NegotiationEngine, NegotiableParams, NegotiationSession
from .emergence import EmergenceEngine, ProtocolVariant, PerformanceMetrics
from .workflows import (
    WorkflowManager,
    MultiAgentOnboardingWorkflow,
    ProtocolEvolutionWorkflow,
    ErrorRecoveryWorkflow,
    ConflictResolutionWorkflow,
    WorkflowExecution,
)
from .observation import (
    ObservationManager,
    get_observation_manager,
    FlowType,
    EventSeverity,
)


class InstrumentedOrchestrator(XenoCommOrchestrator):
    """
    XenoComm Orchestrator with observation instrumentation.

    Automatically emits events for all major operations to enable
    flow visualization and monitoring.
    """

    def __init__(
        self,
        config: OrchestratorConfig | None = None,
        observation_manager: ObservationManager | None = None,
        **kwargs
    ):
        super().__init__(config, **kwargs)
        self.obs = observation_manager or get_observation_manager()

        # Wrap engines with instrumentation
        self._original_alignment = self.alignment
        self._original_negotiation = self.negotiation
        self._original_emergence = self.emergence

    def register_agent(self, context: AgentContext) -> dict[str, Any]:
        """Register agent with observation."""
        result = super().register_agent(context)

        self.obs.agent_sensor.agent_registered(
            agent_id=context.agent_id,
            capabilities=list(context.capabilities.keys()),
            domains=context.knowledge_domains,
        )

        return result

    def deregister_agent(self, agent_id: str, reason: str = "") -> dict[str, Any]:
        """Deregister agent with observation."""
        if agent_id in self.agent_registry:
            del self.agent_registry[agent_id]

            self.obs.agent_sensor.agent_deregistered(
                agent_id=agent_id,
                reason=reason,
            )

        return {"status": "deregistered", "agent_id": agent_id}

    def initiate_collaboration(
        self,
        agent_a_id: str,
        agent_b_id: str,
        required_domains: list[str] | None = None,
        proposed_params: dict[str, Any] | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> CollaborationSession:
        """Initiate collaboration with observation."""
        # Start alignment span
        align_span = self.obs.alignment_sensor.alignment_started(
            agent_a=agent_a_id,
            agent_b=agent_b_id,
            session_id=f"{agent_a_id}:{agent_b_id}",
        )

        # Run the actual collaboration
        session = super().initiate_collaboration(
            agent_a_id=agent_a_id,
            agent_b_id=agent_b_id,
            required_domains=required_domains,
            proposed_params=proposed_params,
            metadata=metadata,
        )

        # Emit alignment completion
        alignment_results = session.alignment_results or {}
        aligned_count = sum(
            1 for r in alignment_results.values()
            if hasattr(r, 'confidence') and r.confidence > 0.7
        )

        self.obs.alignment_sensor.alignment_completed(
            span_id=align_span,
            score=session.metrics.alignment_score,
            dimensions_checked=len(alignment_results),
            aligned_count=aligned_count,
        )

        # Emit collaboration session event
        self.obs.collaboration_sensor.session_created(
            session_id=session.session_id,
            agent_a=agent_a_id,
            agent_b=agent_b_id,
        )

        return session

    def run_full_alignment_check(
        self,
        agent_a_id: str,
        agent_b_id: str,
        required_domains: list[str] | None = None,
    ) -> dict[str, Any]:
        """Run alignment check with observation."""
        span_id = self.obs.alignment_sensor.alignment_started(
            agent_a=agent_a_id,
            agent_b=agent_b_id,
            session_id=f"align:{agent_a_id}:{agent_b_id}",
        )

        # Get agents
        agent_a = self.agent_registry.get(agent_a_id)
        agent_b = self.agent_registry.get(agent_b_id)

        if not agent_a or not agent_b:
            return {"error": "Agent not found", "aligned": False}

        # Run alignment
        results = self.alignment.full_alignment_check(agent_a, agent_b)

        # Calculate metrics
        total = len(results)
        aligned = sum(1 for r in results.values() if r.confidence > 0.7)
        score = aligned / max(total, 1)

        self.obs.alignment_sensor.alignment_completed(
            span_id=span_id,
            score=score,
            dimensions_checked=total,
            aligned_count=aligned,
        )

        return {
            "aligned": score > 0.5,
            "score": score,
            "dimensions": total,
            "aligned_count": aligned,
            "results": {k: r.to_dict() for k, r in results.items()},
        }


class InstrumentedNegotiationEngine(NegotiationEngine):
    """Negotiation engine with observation instrumentation."""

    def __init__(self, observation_manager: ObservationManager | None = None, **kwargs):
        super().__init__(**kwargs)
        self.obs = observation_manager or get_observation_manager()
        self._active_spans: dict[str, str] = {}

    def initiate(
        self,
        initiator_id: str,
        responder_id: str,
        proposed_params: NegotiableParams | None = None,
    ) -> NegotiationSession:
        """Initiate negotiation with observation."""
        session = super().initiate(initiator_id, responder_id, proposed_params)

        span_id = self.obs.negotiation_sensor.negotiation_initiated(
            initiator=initiator_id,
            responder=responder_id,
            session_id=session.session_id,
        )
        self._active_spans[session.session_id] = span_id

        if proposed_params:
            param_count = sum(1 for v in [
                proposed_params.protocol_version,
                proposed_params.data_format,
                proposed_params.compression,
                proposed_params.encryption,
            ] if v is not None)

            self.obs.negotiation_sensor.proposal_made(
                session_id=session.session_id,
                proposer=initiator_id,
                param_count=param_count,
            )

        return session

    def respond(
        self,
        session_id: str,
        responder_id: str,
        response: str,
        counter_params: NegotiableParams | None = None,
        reason: str | None = None,
    ) -> NegotiationSession:
        """Respond to negotiation with observation."""
        session = super().respond(session_id, responder_id, response, counter_params, reason)

        if response == "counter" and counter_params:
            changes = sum(1 for v in [
                counter_params.protocol_version,
                counter_params.data_format,
                counter_params.compression,
            ] if v is not None)

            self.obs.negotiation_sensor.counter_proposal(
                session_id=session_id,
                responder=responder_id,
                changes=changes,
            )

        return session

    def finalize(self, session_id: str, initiator_id: str) -> NegotiationSession:
        """Finalize negotiation with observation."""
        session = super().finalize(session_id, initiator_id)

        if session_id in self._active_spans:
            span_id = self._active_spans.pop(session_id)
            rounds = len(session.rounds) if hasattr(session, 'rounds') else 1

            self.obs.negotiation_sensor.negotiation_completed(
                span_id=span_id,
                outcome="accepted",
                rounds=rounds,
            )

        return session


class InstrumentedEmergenceEngine(EmergenceEngine):
    """Emergence engine with observation instrumentation."""

    def __init__(self, observation_manager: ObservationManager | None = None, **kwargs):
        super().__init__(**kwargs)
        self.obs = observation_manager or get_observation_manager()

    def propose_variant(
        self,
        description: str,
        changes: dict[str, Any],
    ) -> ProtocolVariant:
        """Propose variant with observation."""
        variant = super().propose_variant(description, changes)

        self.obs.emergence_sensor.variant_proposed(
            variant_id=variant.variant_id,
            description=description,
            change_count=len(changes),
        )

        return variant

    def start_canary(
        self,
        variant_id: str,
        initial_percentage: float = 0.1,
    ) -> ProtocolVariant:
        """Start canary with observation."""
        variant = super().start_canary(variant_id, initial_percentage)

        self.obs.emergence_sensor.canary_started(
            variant_id=variant_id,
            percentage=initial_percentage,
        )

        return variant

    def ramp_canary(self, variant_id: str) -> ProtocolVariant:
        """Ramp canary with observation."""
        variant = self.variants.get(variant_id)
        old_pct = variant.canary_percentage if variant else 0

        variant = super().ramp_canary(variant_id)

        self.obs.emergence_sensor.canary_ramped(
            variant_id=variant_id,
            old_pct=old_pct,
            new_pct=variant.canary_percentage,
        )

        # Check if fully rolled out
        if variant.canary_percentage >= 1.0:
            self.obs.emergence_sensor.variant_activated(variant_id)

        return variant

    def rollback(self, variant_id: str) -> dict[str, Any]:
        """Rollback with observation."""
        result = super().rollback(variant_id)

        self.obs.emergence_sensor.variant_rolled_back(
            variant_id=variant_id,
            reason=result.get("reason", "manual rollback"),
        )

        return result

    def start_experiment(
        self,
        control_variant_id: str,
        treatment_variant_id: str,
        traffic_split: float = 0.5,
    ) -> Any:
        """Start A/B experiment with observation."""
        experiment = super().start_experiment(
            control_variant_id,
            treatment_variant_id,
            traffic_split,
        )

        self.obs.emergence_sensor.experiment_started(
            experiment_id=experiment.experiment_id,
            control=control_variant_id,
            treatment=treatment_variant_id,
        )

        return experiment


class InstrumentedWorkflowManager(WorkflowManager):
    """Workflow manager with observation instrumentation."""

    def __init__(
        self,
        orchestrator: XenoCommOrchestrator,
        observation_manager: ObservationManager | None = None,
    ):
        super().__init__(orchestrator)
        self.obs = observation_manager or get_observation_manager()
        self._workflow_spans: dict[str, str] = {}

        # Wrap individual workflows
        self._wrap_onboarding()
        self._wrap_evolution()
        self._wrap_recovery()
        self._wrap_conflict()

    def _wrap_onboarding(self):
        """Wrap onboarding workflow methods."""
        original_start = self.onboarding.start
        original_step = self.onboarding.execute_step

        @wraps(original_start)
        def instrumented_start(*args, **kwargs):
            execution = original_start(*args, **kwargs)
            span_id = self.obs.workflow_sensor.workflow_started(
                workflow_type="multi_agent_onboarding",
                execution_id=execution.execution_id,
                step_count=len(execution.steps),
            )
            self._workflow_spans[execution.execution_id] = span_id
            return execution

        @wraps(original_step)
        def instrumented_step(execution_id: str):
            execution = self.onboarding.executions.get(execution_id)
            if execution:
                step_idx = execution.current_step_index
                if step_idx < len(execution.steps):
                    self.obs.workflow_sensor.step_started(
                        execution_id=execution_id,
                        step_name=execution.steps[step_idx].name,
                        step_index=step_idx,
                    )

            result = original_step(execution_id)

            if execution and step_idx < len(execution.steps):
                success = execution.steps[step_idx].status.value == "completed"
                self.obs.workflow_sensor.step_completed(
                    execution_id=execution_id,
                    step_name=execution.steps[step_idx].name,
                    success=success,
                )

            return result

        self.onboarding.start = instrumented_start
        self.onboarding.execute_step = instrumented_step

    def _wrap_evolution(self):
        """Wrap evolution workflow methods."""
        original_start = self.evolution.start

        @wraps(original_start)
        def instrumented_start(*args, **kwargs):
            execution = original_start(*args, **kwargs)
            span_id = self.obs.workflow_sensor.workflow_started(
                workflow_type="protocol_evolution",
                execution_id=execution.execution_id,
                step_count=len(execution.steps),
            )
            self._workflow_spans[execution.execution_id] = span_id
            return execution

        self.evolution.start = instrumented_start

    def _wrap_recovery(self):
        """Wrap recovery workflow methods."""
        original_start = self.recovery.start

        @wraps(original_start)
        def instrumented_start(*args, **kwargs):
            execution = original_start(*args, **kwargs)
            span_id = self.obs.workflow_sensor.workflow_started(
                workflow_type="error_recovery",
                execution_id=execution.execution_id,
                step_count=len(execution.steps),
            )
            self._workflow_spans[execution.execution_id] = span_id
            return execution

        self.recovery.start = instrumented_start

    def _wrap_conflict(self):
        """Wrap conflict workflow methods."""
        original_start = self.conflict.start

        @wraps(original_start)
        def instrumented_start(*args, **kwargs):
            execution = original_start(*args, **kwargs)
            span_id = self.obs.workflow_sensor.workflow_started(
                workflow_type="conflict_resolution",
                execution_id=execution.execution_id,
                step_count=len(execution.steps),
            )
            self._workflow_spans[execution.execution_id] = span_id
            return execution

        self.conflict.start = instrumented_start


def create_instrumented_system(
    config: OrchestratorConfig | None = None,
) -> tuple[InstrumentedOrchestrator, InstrumentedWorkflowManager, ObservationManager]:
    """
    Create a fully instrumented XenoComm system.

    Returns:
        Tuple of (orchestrator, workflow_manager, observation_manager)
    """
    obs = get_observation_manager()
    obs.start()

    orchestrator = InstrumentedOrchestrator(config=config, observation_manager=obs)
    orchestrator.negotiation = InstrumentedNegotiationEngine(observation_manager=obs)
    orchestrator.emergence = InstrumentedEmergenceEngine(observation_manager=obs)

    workflow_manager = InstrumentedWorkflowManager(orchestrator, obs)

    return orchestrator, workflow_manager, obs

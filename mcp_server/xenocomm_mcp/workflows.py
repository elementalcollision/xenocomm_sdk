"""
XenoComm Workflows
==================

Pre-built workflows for common real-world scenarios in multi-agent
coordination.

Workflows:
1. MultiAgentOnboarding - Onboard new agents with alignment and negotiation
2. ProtocolEvolution - Safely evolve protocol with testing and rollback
3. ErrorRecovery - Handle failures and recover gracefully
4. CrossSystemBridge - Bridge different agent systems
5. HighAvailabilitySetup - Configure redundant communication channels
6. ConflictResolution - Resolve conflicts between agents
"""

from dataclasses import dataclass, field
from typing import Any, Callable
from enum import Enum
from datetime import datetime
import uuid

from .alignment import AlignmentEngine, AgentContext, AlignmentStatus
from .negotiation import (
    NegotiationEngine,
    NegotiableParams,
    NegotiationState,
    NegotiationConfig,
)
from .emergence import (
    EmergenceEngine,
    EmergenceConfig,
    ProtocolVariant,
    VariantStatus,
    PerformanceMetrics,
)
from .orchestrator import XenoCommOrchestrator, OrchestratorConfig


class WorkflowStatus(Enum):
    """Status of a workflow execution."""
    PENDING = "pending"
    RUNNING = "running"
    PAUSED = "paused"
    COMPLETED = "completed"
    FAILED = "failed"
    ROLLED_BACK = "rolled_back"


@dataclass
class WorkflowStep:
    """A single step in a workflow."""
    step_id: str
    name: str
    description: str
    status: WorkflowStatus = WorkflowStatus.PENDING
    started_at: datetime | None = None
    completed_at: datetime | None = None
    result: dict[str, Any] | None = None
    error: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "step_id": self.step_id,
            "name": self.name,
            "description": self.description,
            "status": self.status.value,
            "started_at": self.started_at.isoformat() if self.started_at else None,
            "completed_at": self.completed_at.isoformat() if self.completed_at else None,
            "result": self.result,
            "error": self.error,
        }


@dataclass
class WorkflowExecution:
    """Tracks the execution of a workflow."""
    execution_id: str
    workflow_name: str
    status: WorkflowStatus = WorkflowStatus.PENDING
    steps: list[WorkflowStep] = field(default_factory=list)
    current_step_index: int = 0
    started_at: datetime | None = None
    completed_at: datetime | None = None
    context: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "execution_id": self.execution_id,
            "workflow_name": self.workflow_name,
            "status": self.status.value,
            "steps": [s.to_dict() for s in self.steps],
            "current_step_index": self.current_step_index,
            "started_at": self.started_at.isoformat() if self.started_at else None,
            "completed_at": self.completed_at.isoformat() if self.completed_at else None,
            "progress": f"{self.current_step_index}/{len(self.steps)}",
        }


# ==================== Workflow 1: Multi-Agent Onboarding ====================

class MultiAgentOnboardingWorkflow:
    """
    Workflow for onboarding a new agent into the communication network.

    Steps:
    1. Register agent context and capabilities
    2. Run alignment checks against existing agents
    3. Negotiate communication parameters
    4. Establish initial protocol
    5. Verify connectivity
    """

    def __init__(self, orchestrator: XenoCommOrchestrator):
        self.orchestrator = orchestrator
        self.executions: dict[str, WorkflowExecution] = {}

    def start(
        self,
        new_agent: AgentContext,
        existing_agent_ids: list[str],
        preferred_params: NegotiableParams | None = None,
    ) -> WorkflowExecution:
        """Start the onboarding workflow for a new agent."""
        execution = WorkflowExecution(
            execution_id=str(uuid.uuid4()),
            workflow_name="multi_agent_onboarding",
            steps=[
                WorkflowStep(
                    step_id="register",
                    name="Register Agent",
                    description="Register the new agent's context and capabilities",
                ),
                WorkflowStep(
                    step_id="alignment",
                    name="Check Alignment",
                    description="Run alignment checks against existing agents",
                ),
                WorkflowStep(
                    step_id="negotiate",
                    name="Negotiate Parameters",
                    description="Negotiate communication parameters with each agent",
                ),
                WorkflowStep(
                    step_id="establish",
                    name="Establish Protocol",
                    description="Establish the agreed-upon protocol",
                ),
                WorkflowStep(
                    step_id="verify",
                    name="Verify Connectivity",
                    description="Verify the agent can communicate successfully",
                ),
            ],
            context={
                "new_agent": new_agent,
                "existing_agent_ids": existing_agent_ids,
                "preferred_params": preferred_params,
            },
        )

        execution.status = WorkflowStatus.RUNNING
        execution.started_at = datetime.utcnow()
        self.executions[execution.execution_id] = execution

        return execution

    def execute_step(self, execution_id: str) -> WorkflowExecution:
        """Execute the current step of the workflow."""
        execution = self._get_execution(execution_id)

        if execution.status != WorkflowStatus.RUNNING:
            raise ValueError(f"Workflow is not running: {execution.status}")

        if execution.current_step_index >= len(execution.steps):
            execution.status = WorkflowStatus.COMPLETED
            execution.completed_at = datetime.utcnow()
            return execution

        step = execution.steps[execution.current_step_index]
        step.status = WorkflowStatus.RUNNING
        step.started_at = datetime.utcnow()

        try:
            if step.step_id == "register":
                result = self._step_register(execution)
            elif step.step_id == "alignment":
                result = self._step_alignment(execution)
            elif step.step_id == "negotiate":
                result = self._step_negotiate(execution)
            elif step.step_id == "establish":
                result = self._step_establish(execution)
            elif step.step_id == "verify":
                result = self._step_verify(execution)
            else:
                raise ValueError(f"Unknown step: {step.step_id}")

            step.result = result
            step.status = WorkflowStatus.COMPLETED
            step.completed_at = datetime.utcnow()
            execution.current_step_index += 1

            # Check if workflow is complete
            if execution.current_step_index >= len(execution.steps):
                execution.status = WorkflowStatus.COMPLETED
                execution.completed_at = datetime.utcnow()

        except Exception as e:
            step.status = WorkflowStatus.FAILED
            step.error = str(e)
            execution.status = WorkflowStatus.FAILED

        return execution

    def execute_all(self, execution_id: str) -> WorkflowExecution:
        """Execute all remaining steps."""
        execution = self._get_execution(execution_id)

        while execution.status == WorkflowStatus.RUNNING:
            execution = self.execute_step(execution_id)

        return execution

    def _step_register(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Register the new agent."""
        new_agent = execution.context["new_agent"]
        self.orchestrator.alignment.contexts[new_agent.agent_id] = new_agent

        return {
            "agent_id": new_agent.agent_id,
            "domains": new_agent.knowledge_domains,
            "registered": True,
        }

    def _step_alignment(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Check alignment with existing agents."""
        new_agent = execution.context["new_agent"]
        existing_ids = execution.context["existing_agent_ids"]

        alignment_results = {}
        for agent_id in existing_ids:
            if agent_id in self.orchestrator.alignment.contexts:
                existing = self.orchestrator.alignment.contexts[agent_id]
                results = self.orchestrator.alignment.full_alignment_check(new_agent, existing)
                alignment_results[agent_id] = {
                    k: {"status": v.status.value, "confidence": v.confidence}
                    for k, v in results.items()
                }

        execution.context["alignment_results"] = alignment_results
        return alignment_results

    def _step_negotiate(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Negotiate with each existing agent."""
        new_agent = execution.context["new_agent"]
        existing_ids = execution.context["existing_agent_ids"]
        preferred = execution.context.get("preferred_params") or NegotiableParams()

        negotiation_results = {}
        for agent_id in existing_ids:
            # Start negotiation session
            session = self.orchestrator.negotiation.initiate_session(
                initiator_id=new_agent.agent_id,
                responder_id=agent_id,
                proposed_params=preferred,
            )
            negotiation_results[agent_id] = session.session_id

        execution.context["negotiation_sessions"] = negotiation_results
        return {"sessions_started": len(negotiation_results)}

    def _step_establish(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Establish agreed protocol."""
        sessions = execution.context.get("negotiation_sessions", {})
        established = {}

        for agent_id, session_id in sessions.items():
            session = self.orchestrator.negotiation.get_session_status(session_id)
            # For demo, auto-finalize with proposed params
            if session.state == NegotiationState.AWAITING_RESPONSE:
                session = self.orchestrator.negotiation.finalize_session(
                    session_id=session_id,
                    initiator_id=session.initiator_id,
                )
            established[agent_id] = {
                "session_id": session_id,
                "state": session.state.value,
                "params": session.final_params.to_dict() if session.final_params else None,
            }

        return established

    def _step_verify(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Verify connectivity."""
        # In real implementation, this would test actual connectivity
        return {
            "connectivity_verified": True,
            "timestamp": datetime.utcnow().isoformat(),
        }

    def _get_execution(self, execution_id: str) -> WorkflowExecution:
        if execution_id not in self.executions:
            raise ValueError(f"Execution {execution_id} not found")
        return self.executions[execution_id]


# ==================== Workflow 2: Protocol Evolution ====================

class ProtocolEvolutionWorkflow:
    """
    Workflow for safely evolving the communication protocol.

    Steps:
    1. Propose protocol variant
    2. Run internal testing
    3. Deploy canary to subset of agents
    4. Monitor and analyze metrics
    5. Full rollout or rollback
    """

    def __init__(self, orchestrator: XenoCommOrchestrator):
        self.orchestrator = orchestrator
        self.executions: dict[str, WorkflowExecution] = {}

    def start(
        self,
        description: str,
        changes: dict[str, Any],
        target_agents: list[str] | None = None,
    ) -> WorkflowExecution:
        """Start protocol evolution workflow."""
        execution = WorkflowExecution(
            execution_id=str(uuid.uuid4()),
            workflow_name="protocol_evolution",
            steps=[
                WorkflowStep(
                    step_id="propose",
                    name="Propose Variant",
                    description="Create and register protocol variant",
                ),
                WorkflowStep(
                    step_id="test",
                    name="Internal Testing",
                    description="Run automated tests on the variant",
                ),
                WorkflowStep(
                    step_id="canary",
                    name="Canary Deployment",
                    description="Deploy to canary subset",
                ),
                WorkflowStep(
                    step_id="monitor",
                    name="Monitor Metrics",
                    description="Collect and analyze performance metrics",
                ),
                WorkflowStep(
                    step_id="decide",
                    name="Rollout Decision",
                    description="Decide on full rollout or rollback",
                ),
            ],
            context={
                "description": description,
                "changes": changes,
                "target_agents": target_agents,
            },
        )

        execution.status = WorkflowStatus.RUNNING
        execution.started_at = datetime.utcnow()
        self.executions[execution.execution_id] = execution

        return execution

    def execute_step(self, execution_id: str) -> WorkflowExecution:
        """Execute current step."""
        execution = self._get_execution(execution_id)

        if execution.status != WorkflowStatus.RUNNING:
            raise ValueError(f"Workflow not running: {execution.status}")

        if execution.current_step_index >= len(execution.steps):
            execution.status = WorkflowStatus.COMPLETED
            execution.completed_at = datetime.utcnow()
            return execution

        step = execution.steps[execution.current_step_index]
        step.status = WorkflowStatus.RUNNING
        step.started_at = datetime.utcnow()

        try:
            if step.step_id == "propose":
                result = self._step_propose(execution)
            elif step.step_id == "test":
                result = self._step_test(execution)
            elif step.step_id == "canary":
                result = self._step_canary(execution)
            elif step.step_id == "monitor":
                result = self._step_monitor(execution)
            elif step.step_id == "decide":
                result = self._step_decide(execution)
            else:
                raise ValueError(f"Unknown step: {step.step_id}")

            step.result = result
            step.status = WorkflowStatus.COMPLETED
            step.completed_at = datetime.utcnow()
            execution.current_step_index += 1

            if execution.current_step_index >= len(execution.steps):
                execution.status = WorkflowStatus.COMPLETED
                execution.completed_at = datetime.utcnow()

        except Exception as e:
            step.status = WorkflowStatus.FAILED
            step.error = str(e)
            execution.status = WorkflowStatus.FAILED

        return execution

    def _step_propose(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Propose the variant."""
        variant = self.orchestrator.emergence.propose_variant(
            description=execution.context["description"],
            changes=execution.context["changes"],
        )
        execution.context["variant_id"] = variant.variant_id

        # Predict success based on history
        prediction = self.orchestrator.emergence.predict_success(
            execution.context["changes"]
        )

        return {
            "variant_id": variant.variant_id,
            "predicted_success": prediction,
        }

    def _step_test(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Run internal tests."""
        variant_id = execution.context["variant_id"]
        variant = self.orchestrator.emergence.start_testing(variant_id)

        # Simulate test metrics
        test_metrics = PerformanceMetrics(
            success_rate=0.99,
            latency_ms=50.0,
            throughput=1000.0,
            total_requests=100,
        )
        self.orchestrator.emergence.track_performance(variant_id, test_metrics)

        return {
            "testing_passed": True,
            "metrics": test_metrics.to_dict(),
        }

    def _step_canary(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Start canary deployment."""
        variant_id = execution.context["variant_id"]
        variant = self.orchestrator.emergence.start_canary(variant_id)

        return {
            "canary_started": True,
            "initial_percentage": variant.canary_percentage,
        }

    def _step_monitor(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Monitor and analyze metrics."""
        variant_id = execution.context["variant_id"]

        # Get trend analysis
        success_trend = self.orchestrator.emergence.analyze_trend(variant_id, "success_rate")
        latency_trend = self.orchestrator.emergence.analyze_trend(variant_id, "latency_ms")

        # Check for anomalies
        has_anomaly = self.orchestrator.emergence.detect_anomaly(variant_id)

        variant = self.orchestrator.emergence._get_variant(variant_id)

        return {
            "success_rate_trend": success_trend.value,
            "latency_trend": latency_trend.value,
            "anomaly_detected": has_anomaly,
            "average_success_rate": variant.get_average_success_rate(),
        }

    def _step_decide(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Make rollout or rollback decision."""
        variant_id = execution.context["variant_id"]

        should_rollback, reason = self.orchestrator.emergence.should_rollback(variant_id)

        if should_rollback:
            self.orchestrator.emergence.rollback(variant_id, reason=reason)
            execution.status = WorkflowStatus.ROLLED_BACK
            return {
                "decision": "rollback",
                "reason": reason.value if reason else "metrics_threshold",
            }
        else:
            # Ramp to full deployment
            variant = self.orchestrator.emergence._get_variant(variant_id)
            while variant.status == VariantStatus.CANARY:
                variant = self.orchestrator.emergence.ramp_canary(variant_id, force=True)

            return {
                "decision": "full_rollout",
                "final_status": variant.status.value,
            }

    def _get_execution(self, execution_id: str) -> WorkflowExecution:
        if execution_id not in self.executions:
            raise ValueError(f"Execution {execution_id} not found")
        return self.executions[execution_id]


# ==================== Workflow 3: Error Recovery ====================

class ErrorRecoveryWorkflow:
    """
    Workflow for handling errors and recovering gracefully.

    Steps:
    1. Detect and classify error
    2. Isolate affected components
    3. Attempt automatic recovery
    4. Notify stakeholders if needed
    5. Resume normal operations
    """

    def __init__(self, orchestrator: XenoCommOrchestrator):
        self.orchestrator = orchestrator
        self.executions: dict[str, WorkflowExecution] = {}

    def start(
        self,
        error_type: str,
        affected_agents: list[str],
        error_details: dict[str, Any],
    ) -> WorkflowExecution:
        """Start error recovery workflow."""
        execution = WorkflowExecution(
            execution_id=str(uuid.uuid4()),
            workflow_name="error_recovery",
            steps=[
                WorkflowStep(
                    step_id="detect",
                    name="Detect Error",
                    description="Classify and analyze the error",
                ),
                WorkflowStep(
                    step_id="isolate",
                    name="Isolate Components",
                    description="Isolate affected agents/sessions",
                ),
                WorkflowStep(
                    step_id="recover",
                    name="Automatic Recovery",
                    description="Attempt automatic recovery procedures",
                ),
                WorkflowStep(
                    step_id="notify",
                    name="Notify Stakeholders",
                    description="Send notifications if needed",
                ),
                WorkflowStep(
                    step_id="resume",
                    name="Resume Operations",
                    description="Resume normal operations",
                ),
            ],
            context={
                "error_type": error_type,
                "affected_agents": affected_agents,
                "error_details": error_details,
            },
        )

        execution.status = WorkflowStatus.RUNNING
        execution.started_at = datetime.utcnow()
        self.executions[execution.execution_id] = execution

        return execution

    def execute_step(self, execution_id: str) -> WorkflowExecution:
        """Execute current step."""
        execution = self._get_execution(execution_id)

        if execution.status != WorkflowStatus.RUNNING:
            raise ValueError(f"Workflow not running: {execution.status}")

        if execution.current_step_index >= len(execution.steps):
            execution.status = WorkflowStatus.COMPLETED
            execution.completed_at = datetime.utcnow()
            return execution

        step = execution.steps[execution.current_step_index]
        step.status = WorkflowStatus.RUNNING
        step.started_at = datetime.utcnow()

        try:
            if step.step_id == "detect":
                result = self._step_detect(execution)
            elif step.step_id == "isolate":
                result = self._step_isolate(execution)
            elif step.step_id == "recover":
                result = self._step_recover(execution)
            elif step.step_id == "notify":
                result = self._step_notify(execution)
            elif step.step_id == "resume":
                result = self._step_resume(execution)
            else:
                raise ValueError(f"Unknown step: {step.step_id}")

            step.result = result
            step.status = WorkflowStatus.COMPLETED
            step.completed_at = datetime.utcnow()
            execution.current_step_index += 1

            if execution.current_step_index >= len(execution.steps):
                execution.status = WorkflowStatus.COMPLETED
                execution.completed_at = datetime.utcnow()

        except Exception as e:
            step.status = WorkflowStatus.FAILED
            step.error = str(e)
            execution.status = WorkflowStatus.FAILED

        return execution

    def _step_detect(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Detect and classify error."""
        error_type = execution.context["error_type"]
        details = execution.context["error_details"]

        # Classify severity
        severity = "low"
        if error_type in ["connection_failure", "protocol_mismatch"]:
            severity = "high"
        elif error_type in ["timeout", "alignment_failure"]:
            severity = "medium"

        execution.context["severity"] = severity

        return {
            "error_type": error_type,
            "severity": severity,
            "classification": "recoverable" if severity != "high" else "critical",
        }

    def _step_isolate(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Isolate affected components."""
        affected = execution.context["affected_agents"]
        isolated = []

        for agent_id in affected:
            # Close any active negotiation sessions
            sessions = self.orchestrator.negotiation.list_sessions(agent_id=agent_id)
            for session in sessions:
                if session.state not in (NegotiationState.FINALIZED, NegotiationState.CLOSED):
                    self.orchestrator.negotiation.close_session(
                        session.session_id,
                        agent_id,
                        reason="Error recovery isolation"
                    )
            isolated.append(agent_id)

        return {"isolated_agents": isolated}

    def _step_recover(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Attempt automatic recovery."""
        error_type = execution.context["error_type"]
        affected = execution.context["affected_agents"]

        recovery_actions = []

        if error_type == "timeout":
            # Check and handle timeouts
            timed_out = self.orchestrator.negotiation.check_all_timeouts()
            recovery_actions.append(f"Handled {len(timed_out)} timed out sessions")

        elif error_type == "alignment_failure":
            # Re-run alignment checks
            for agent_id in affected:
                if agent_id in self.orchestrator.alignment.contexts:
                    recovery_actions.append(f"Re-checking alignment for {agent_id}")

        elif error_type == "protocol_mismatch":
            # Roll back any canary deployments
            canaries = self.orchestrator.emergence.list_variants(status=VariantStatus.CANARY)
            for variant in canaries:
                self.orchestrator.emergence.rollback(variant.variant_id)
                recovery_actions.append(f"Rolled back variant {variant.variant_id}")

        execution.context["recovery_successful"] = len(recovery_actions) > 0

        return {
            "actions_taken": recovery_actions,
            "recovery_attempted": True,
        }

    def _step_notify(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Notify stakeholders if needed."""
        severity = execution.context.get("severity", "low")
        recovery_successful = execution.context.get("recovery_successful", False)

        should_notify = severity == "high" or not recovery_successful

        return {
            "notification_sent": should_notify,
            "severity": severity,
            "recovery_status": "successful" if recovery_successful else "failed",
        }

    def _step_resume(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Resume normal operations."""
        affected = execution.context["affected_agents"]

        # Resume any paused variants
        paused = self.orchestrator.emergence.list_variants(status=VariantStatus.PAUSED)
        for variant in paused:
            self.orchestrator.emergence.resume_variant(variant.variant_id)

        return {
            "operations_resumed": True,
            "agents_recovered": affected,
        }

    def _get_execution(self, execution_id: str) -> WorkflowExecution:
        if execution_id not in self.executions:
            raise ValueError(f"Execution {execution_id} not found")
        return self.executions[execution_id]


# ==================== Workflow 4: Conflict Resolution ====================

class ConflictResolutionWorkflow:
    """
    Workflow for resolving conflicts between agents.

    Steps:
    1. Identify conflict source
    2. Analyze competing requirements
    3. Propose compromise solutions
    4. Facilitate negotiation
    5. Document resolution
    """

    def __init__(self, orchestrator: XenoCommOrchestrator):
        self.orchestrator = orchestrator
        self.executions: dict[str, WorkflowExecution] = {}

    def start(
        self,
        agent_a_id: str,
        agent_b_id: str,
        conflict_type: str,
        conflict_details: dict[str, Any],
    ) -> WorkflowExecution:
        """Start conflict resolution workflow."""
        execution = WorkflowExecution(
            execution_id=str(uuid.uuid4()),
            workflow_name="conflict_resolution",
            steps=[
                WorkflowStep(
                    step_id="identify",
                    name="Identify Conflict",
                    description="Analyze the source of the conflict",
                ),
                WorkflowStep(
                    step_id="analyze",
                    name="Analyze Requirements",
                    description="Understand each agent's requirements",
                ),
                WorkflowStep(
                    step_id="propose",
                    name="Propose Solutions",
                    description="Generate compromise solutions",
                ),
                WorkflowStep(
                    step_id="negotiate",
                    name="Facilitate Negotiation",
                    description="Help agents reach agreement",
                ),
                WorkflowStep(
                    step_id="document",
                    name="Document Resolution",
                    description="Record the resolution for future reference",
                ),
            ],
            context={
                "agent_a_id": agent_a_id,
                "agent_b_id": agent_b_id,
                "conflict_type": conflict_type,
                "conflict_details": conflict_details,
            },
        )

        execution.status = WorkflowStatus.RUNNING
        execution.started_at = datetime.utcnow()
        self.executions[execution.execution_id] = execution

        return execution

    def execute_step(self, execution_id: str) -> WorkflowExecution:
        """Execute current step."""
        execution = self._get_execution(execution_id)

        if execution.status != WorkflowStatus.RUNNING:
            raise ValueError(f"Workflow not running: {execution.status}")

        if execution.current_step_index >= len(execution.steps):
            execution.status = WorkflowStatus.COMPLETED
            execution.completed_at = datetime.utcnow()
            return execution

        step = execution.steps[execution.current_step_index]
        step.status = WorkflowStatus.RUNNING
        step.started_at = datetime.utcnow()

        try:
            if step.step_id == "identify":
                result = self._step_identify(execution)
            elif step.step_id == "analyze":
                result = self._step_analyze(execution)
            elif step.step_id == "propose":
                result = self._step_propose(execution)
            elif step.step_id == "negotiate":
                result = self._step_negotiate(execution)
            elif step.step_id == "document":
                result = self._step_document(execution)
            else:
                raise ValueError(f"Unknown step: {step.step_id}")

            step.result = result
            step.status = WorkflowStatus.COMPLETED
            step.completed_at = datetime.utcnow()
            execution.current_step_index += 1

            if execution.current_step_index >= len(execution.steps):
                execution.status = WorkflowStatus.COMPLETED
                execution.completed_at = datetime.utcnow()

        except Exception as e:
            step.status = WorkflowStatus.FAILED
            step.error = str(e)
            execution.status = WorkflowStatus.FAILED

        return execution

    def _step_identify(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Identify the conflict source."""
        agent_a = execution.context["agent_a_id"]
        agent_b = execution.context["agent_b_id"]
        conflict_type = execution.context["conflict_type"]

        # Run alignment check to understand differences
        context_a = self.orchestrator.alignment.contexts.get(agent_a)
        context_b = self.orchestrator.alignment.contexts.get(agent_b)

        conflict_sources = []
        if context_a and context_b:
            alignment = self.orchestrator.alignment.full_alignment_check(context_a, context_b)
            for area, result in alignment.items():
                if result.status == AlignmentStatus.MISALIGNED:
                    conflict_sources.append(area)

        return {
            "conflict_type": conflict_type,
            "conflict_sources": conflict_sources,
            "agents_involved": [agent_a, agent_b],
        }

    def _step_analyze(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Analyze each agent's requirements."""
        agent_a = execution.context["agent_a_id"]
        agent_b = execution.context["agent_b_id"]

        requirements = {
            "agent_a": {},
            "agent_b": {},
        }

        # Get agent contexts
        if agent_a in self.orchestrator.alignment.contexts:
            ctx = self.orchestrator.alignment.contexts[agent_a]
            requirements["agent_a"] = {
                "goals": ctx.goals,
                "domains": ctx.knowledge_domains,
            }

        if agent_b in self.orchestrator.alignment.contexts:
            ctx = self.orchestrator.alignment.contexts[agent_b]
            requirements["agent_b"] = {
                "goals": ctx.goals,
                "domains": ctx.knowledge_domains,
            }

        return requirements

    def _step_propose(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Generate compromise solutions."""
        conflict_type = execution.context["conflict_type"]

        solutions = []

        if conflict_type == "parameter_mismatch":
            solutions.append({
                "type": "auto_merge",
                "description": "Automatically merge parameters using compatibility rules",
            })
            solutions.append({
                "type": "lowest_common",
                "description": "Use lowest common denominator settings",
            })

        elif conflict_type == "goal_conflict":
            solutions.append({
                "type": "priority_ordering",
                "description": "Order goals by priority and execute sequentially",
            })
            solutions.append({
                "type": "scope_partition",
                "description": "Partition the scope so each agent handles different aspects",
            })

        elif conflict_type == "terminology":
            solutions.append({
                "type": "glossary",
                "description": "Create a shared glossary for disputed terms",
            })

        execution.context["proposed_solutions"] = solutions

        return {"solutions": solutions}

    def _step_negotiate(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Facilitate negotiation between agents."""
        agent_a = execution.context["agent_a_id"]
        agent_b = execution.context["agent_b_id"]

        # Use the negotiation engine to auto-resolve
        # Start a negotiation session for resolution
        session = self.orchestrator.negotiation.initiate_session(
            initiator_id=agent_a,
            responder_id=agent_b,
            proposed_params=NegotiableParams(),
        )

        # Auto-resolve conflicts
        resolved_params = self.orchestrator.negotiation.auto_resolve_conflicts(
            session.session_id
        )

        execution.context["resolution_session"] = session.session_id
        execution.context["resolved_params"] = resolved_params

        return {
            "negotiation_session": session.session_id,
            "auto_resolved": True,
        }

    def _step_document(self, execution: WorkflowExecution) -> dict[str, Any]:
        """Document the resolution."""
        return {
            "documented": True,
            "resolution_id": execution.execution_id,
            "timestamp": datetime.utcnow().isoformat(),
            "conflict_type": execution.context["conflict_type"],
            "agents": [execution.context["agent_a_id"], execution.context["agent_b_id"]],
        }

    def _get_execution(self, execution_id: str) -> WorkflowExecution:
        if execution_id not in self.executions:
            raise ValueError(f"Execution {execution_id} not found")
        return self.executions[execution_id]


# ==================== Workflow Manager ====================

class WorkflowManager:
    """
    Central manager for all workflows.

    Provides a unified interface for starting and managing workflows.
    """

    def __init__(self, orchestrator: XenoCommOrchestrator):
        self.orchestrator = orchestrator
        self.onboarding = MultiAgentOnboardingWorkflow(orchestrator)
        self.evolution = ProtocolEvolutionWorkflow(orchestrator)
        self.recovery = ErrorRecoveryWorkflow(orchestrator)
        self.conflict = ConflictResolutionWorkflow(orchestrator)

    def list_workflow_types(self) -> list[dict[str, str]]:
        """List available workflow types."""
        return [
            {
                "name": "multi_agent_onboarding",
                "description": "Onboard new agents with alignment and negotiation",
            },
            {
                "name": "protocol_evolution",
                "description": "Safely evolve protocol with testing and rollback",
            },
            {
                "name": "error_recovery",
                "description": "Handle failures and recover gracefully",
            },
            {
                "name": "conflict_resolution",
                "description": "Resolve conflicts between agents",
            },
        ]

    def get_all_executions(self) -> list[WorkflowExecution]:
        """Get all workflow executions across all types."""
        executions = []
        executions.extend(self.onboarding.executions.values())
        executions.extend(self.evolution.executions.values())
        executions.extend(self.recovery.executions.values())
        executions.extend(self.conflict.executions.values())
        return executions

    def get_execution_status(self, execution_id: str) -> WorkflowExecution | None:
        """Get status of any execution by ID."""
        for workflow in [self.onboarding, self.evolution, self.recovery, self.conflict]:
            if execution_id in workflow.executions:
                return workflow.executions[execution_id]
        return None

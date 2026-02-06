"""
KFM Lifecycle Engine - Evolutionary Agent Lifecycle Management

Based on the Agentic Evolution framework by David Graham, this module implements
the Kill/Fuck/Marry (KFM) tripartite selection logic for managing agent lifecycles,
protocol evolution, and collaboration decisions.

The three operators:
- KILL (K): Elimination - Remove obsolete, failing, or misaligned components
- FUCK (F): Adaptation - Experiment, iterate, and explore without commitment
- MARRY (M): Integration - Stabilize, standardize, and commit to proven components

Reference: https://github.com/elementalcollision/Agentic_Evolution
"""

from dataclasses import dataclass, field
from datetime import datetime, timedelta
from enum import Enum
from typing import Any, Optional
import math


class LifecyclePhase(Enum):
    """Current phase in the KFM lifecycle."""
    PROPOSED = "proposed"       # Initial submission, not yet evaluated
    EXPERIMENTAL = "experimental"  # F-phase: Testing and adaptation
    STABLE = "stable"           # M-phase: Production-ready, committed
    DEPRECATED = "deprecated"   # K-phase: Scheduled for removal
    ELIMINATED = "eliminated"   # K-phase complete: Removed from system


class SelectionOperator(Enum):
    """The three selection operators from Agentic Evolution."""
    KILL = "kill"    # Eliminate: deprecate, delete, archive
    FUCK = "fuck"    # Adapt: experiment, mutate, iterate
    MARRY = "marry"  # Integrate: stabilize, standardize, commit


@dataclass
class PerformanceMetrics:
    """Metrics for evaluating agent/protocol performance."""
    success_rate: float = 0.0        # Task completion rate (0-1)
    error_rate: float = 0.0          # Failure frequency (0-1)
    latency_ms: float = 0.0          # Average response time
    throughput: float = 0.0          # Operations per second
    uptime: float = 1.0              # Availability (0-1)
    resource_cost: float = 0.0       # Computational/memory cost
    maintenance_cost: float = 0.0    # Human intervention frequency


@dataclass
class AlignmentMetrics:
    """Metrics for evaluating alignment with system goals."""
    goal_alignment: float = 0.5      # How well goals match system objectives (0-1)
    knowledge_coverage: float = 0.5  # Domain expertise breadth (0-1)
    conflict_frequency: float = 0.0  # How often conflicts occur (0-1)
    collaboration_success: float = 0.5  # Success rate in multi-agent tasks (0-1)
    protocol_compliance: float = 1.0 # Adherence to communication protocols (0-1)


@dataclass
class EvolutionPotential:
    """Metrics for evaluating adaptation potential."""
    innovation_score: float = 0.5    # Novel behavior generation (0-1)
    learning_rate: float = 0.5       # Speed of improvement (0-1)
    adaptability: float = 0.5        # Response to changing conditions (0-1)
    pattern_emergence: float = 0.0   # New patterns detected (0-1)
    variant_success: float = 0.0     # Success of proposed changes (0-1)


@dataclass
class KFMScores:
    """Computed KFM selection scores."""
    kill_score: float = 0.0          # Likelihood of elimination
    fuck_score: float = 0.0          # Suitability for experimentation
    marry_score: float = 0.0         # Readiness for integration
    recommendation: SelectionOperator = SelectionOperator.FUCK
    confidence: float = 0.0          # Confidence in recommendation (0-1)
    reasoning: list[str] = field(default_factory=list)


@dataclass
class LifecycleState:
    """Complete state of an entity in the KFM lifecycle."""
    entity_id: str
    entity_type: str  # "agent", "protocol", "variant"
    phase: LifecyclePhase
    created_at: datetime
    updated_at: datetime
    phase_entered_at: datetime
    performance: PerformanceMetrics
    alignment: AlignmentMetrics
    evolution: EvolutionPotential
    scores: KFMScores
    metadata: dict[str, Any] = field(default_factory=dict)
    history: list[dict] = field(default_factory=list)


class KFMWeightingConfig:
    """Configuration for KFM weighting algorithms."""

    # Phase transition thresholds
    MARRY_THRESHOLD: float = 0.75      # Score needed to promote to stable
    KILL_THRESHOLD: float = 0.60       # Score needed to trigger deprecation
    EXPERIMENTAL_MIN_DURATION: timedelta = timedelta(hours=1)  # Min time in F-phase

    # Weight factors for KILL score calculation
    KILL_WEIGHTS = {
        "failure_rate": 0.30,
        "maintenance_cost": 0.20,
        "conflict_frequency": 0.20,
        "resource_inefficiency": 0.15,
        "alignment_deficit": 0.15,
    }

    # Weight factors for FUCK score calculation
    FUCK_WEIGHTS = {
        "innovation_potential": 0.25,
        "learning_rate": 0.20,
        "adaptability": 0.20,
        "pattern_emergence": 0.15,
        "alignment_gap_recoverability": 0.20,
    }

    # Weight factors for MARRY score calculation
    MARRY_WEIGHTS = {
        "reliability": 0.25,
        "goal_alignment": 0.25,
        "collaboration_success": 0.20,
        "protocol_compliance": 0.15,
        "stability_duration": 0.15,
    }


class KFMLifecycleEngine:
    """
    Engine for managing agent/protocol lifecycle using KFM selection logic.

    This implements the evolutionary framework from Agentic Evolution,
    applying tripartite selection (Kill/Fuck/Marry) to manage digital
    entities in a multi-agent system.
    """

    def __init__(self, config: KFMWeightingConfig | None = None):
        self.config = config or KFMWeightingConfig()
        self.entities: dict[str, LifecycleState] = {}
        self._phase_transitions: list[dict] = []

    def register_entity(
        self,
        entity_id: str,
        entity_type: str,
        initial_phase: LifecyclePhase = LifecyclePhase.PROPOSED,
        metadata: dict[str, Any] | None = None,
    ) -> LifecycleState:
        """Register a new entity for lifecycle management."""
        now = datetime.utcnow()
        state = LifecycleState(
            entity_id=entity_id,
            entity_type=entity_type,
            phase=initial_phase,
            created_at=now,
            updated_at=now,
            phase_entered_at=now,
            performance=PerformanceMetrics(),
            alignment=AlignmentMetrics(),
            evolution=EvolutionPotential(),
            scores=KFMScores(),
            metadata=metadata or {},
        )
        self.entities[entity_id] = state
        return state

    def update_metrics(
        self,
        entity_id: str,
        performance: PerformanceMetrics | None = None,
        alignment: AlignmentMetrics | None = None,
        evolution: EvolutionPotential | None = None,
    ) -> LifecycleState:
        """Update metrics for an entity and recompute KFM scores."""
        state = self.entities.get(entity_id)
        if not state:
            raise ValueError(f"Entity {entity_id} not found")

        if performance:
            state.performance = performance
        if alignment:
            state.alignment = alignment
        if evolution:
            state.evolution = evolution

        state.updated_at = datetime.utcnow()

        # Recompute KFM scores
        state.scores = self._compute_kfm_scores(state)

        return state

    def _compute_kfm_scores(self, state: LifecycleState) -> KFMScores:
        """Compute KFM selection scores based on current metrics."""
        perf = state.performance
        align = state.alignment
        evol = state.evolution

        reasoning = []

        # === KILL SCORE ===
        # High when: failures, high maintenance, conflicts, resource waste
        k_failure = perf.error_rate
        k_maintenance = perf.maintenance_cost
        k_conflict = align.conflict_frequency
        k_resource = 1 - (1 / (1 + perf.resource_cost))  # Normalize
        k_alignment = 1 - align.goal_alignment

        kill_score = (
            self.config.KILL_WEIGHTS["failure_rate"] * k_failure +
            self.config.KILL_WEIGHTS["maintenance_cost"] * k_maintenance +
            self.config.KILL_WEIGHTS["conflict_frequency"] * k_conflict +
            self.config.KILL_WEIGHTS["resource_inefficiency"] * k_resource +
            self.config.KILL_WEIGHTS["alignment_deficit"] * k_alignment
        )

        if kill_score > 0.5:
            if k_failure > 0.3:
                reasoning.append(f"High failure rate ({k_failure:.1%})")
            if k_conflict > 0.3:
                reasoning.append(f"Frequent conflicts ({k_conflict:.1%})")

        # === FUCK SCORE ===
        # High when: innovation potential, learning capability, adaptable
        f_innovation = evol.innovation_score
        f_learning = evol.learning_rate
        f_adaptability = evol.adaptability
        f_patterns = evol.pattern_emergence
        # Can recover from current alignment issues
        f_recoverability = evol.adaptability * (1 - align.goal_alignment)

        fuck_score = (
            self.config.FUCK_WEIGHTS["innovation_potential"] * f_innovation +
            self.config.FUCK_WEIGHTS["learning_rate"] * f_learning +
            self.config.FUCK_WEIGHTS["adaptability"] * f_adaptability +
            self.config.FUCK_WEIGHTS["pattern_emergence"] * f_patterns +
            self.config.FUCK_WEIGHTS["alignment_gap_recoverability"] * f_recoverability
        )

        if fuck_score > 0.5:
            if f_innovation > 0.6:
                reasoning.append(f"High innovation potential ({f_innovation:.1%})")
            if f_learning > 0.6:
                reasoning.append(f"Fast learner ({f_learning:.1%})")

        # === MARRY SCORE ===
        # High when: reliable, aligned, good collaborator, compliant
        m_reliability = perf.success_rate * perf.uptime
        m_alignment = align.goal_alignment
        m_collaboration = align.collaboration_success
        m_compliance = align.protocol_compliance

        # Stability bonus for time in current phase
        time_in_phase = (datetime.utcnow() - state.phase_entered_at).total_seconds()
        stability_hours = time_in_phase / 3600
        m_stability = min(1.0, stability_hours / 24)  # Max out at 24 hours

        marry_score = (
            self.config.MARRY_WEIGHTS["reliability"] * m_reliability +
            self.config.MARRY_WEIGHTS["goal_alignment"] * m_alignment +
            self.config.MARRY_WEIGHTS["collaboration_success"] * m_collaboration +
            self.config.MARRY_WEIGHTS["protocol_compliance"] * m_compliance +
            self.config.MARRY_WEIGHTS["stability_duration"] * m_stability
        )

        if marry_score > 0.5:
            if m_reliability > 0.8:
                reasoning.append(f"Highly reliable ({m_reliability:.1%})")
            if m_alignment > 0.8:
                reasoning.append(f"Well aligned ({m_alignment:.1%})")

        # === RECOMMENDATION ===
        scores = {
            SelectionOperator.KILL: kill_score,
            SelectionOperator.FUCK: fuck_score,
            SelectionOperator.MARRY: marry_score,
        }

        # Normalize scores
        total = sum(scores.values())
        if total > 0:
            for op in scores:
                scores[op] /= total

        # Select operator with highest normalized score
        recommendation = max(scores, key=lambda x: scores[x])
        confidence = scores[recommendation]

        # Add recommendation reasoning
        if recommendation == SelectionOperator.KILL:
            reasoning.insert(0, "⚠️ Recommend ELIMINATION")
        elif recommendation == SelectionOperator.FUCK:
            reasoning.insert(0, "🔬 Recommend EXPERIMENTATION")
        else:
            reasoning.insert(0, "💍 Recommend INTEGRATION")

        return KFMScores(
            kill_score=kill_score,
            fuck_score=fuck_score,
            marry_score=marry_score,
            recommendation=recommendation,
            confidence=confidence,
            reasoning=reasoning,
        )

    def evaluate_transition(self, entity_id: str) -> tuple[LifecyclePhase | None, str]:
        """
        Evaluate if an entity should transition to a new phase.

        Returns:
            (new_phase, reason) if transition recommended, (None, "") otherwise
        """
        state = self.entities.get(entity_id)
        if not state:
            return None, "Entity not found"

        scores = state.scores
        current_phase = state.phase

        # Check phase-specific transition rules
        if current_phase == LifecyclePhase.PROPOSED:
            # New entities enter experimental phase for evaluation
            return LifecyclePhase.EXPERIMENTAL, "Initial evaluation period"

        elif current_phase == LifecyclePhase.EXPERIMENTAL:
            # Check minimum duration
            time_in_phase = datetime.utcnow() - state.phase_entered_at
            if time_in_phase < self.config.EXPERIMENTAL_MIN_DURATION:
                return None, "Minimum experimental duration not met"

            # Evaluate for promotion or elimination
            if scores.recommendation == SelectionOperator.MARRY:
                if scores.marry_score >= self.config.MARRY_THRESHOLD:
                    return LifecyclePhase.STABLE, "Proven reliable, promoting to stable"

            if scores.recommendation == SelectionOperator.KILL:
                if scores.kill_score >= self.config.KILL_THRESHOLD:
                    return LifecyclePhase.DEPRECATED, "Performance issues, deprecating"

            return None, "Continue experimentation"

        elif current_phase == LifecyclePhase.STABLE:
            # Stable entities can be deprecated if performance degrades
            if scores.recommendation == SelectionOperator.KILL:
                if scores.kill_score >= self.config.KILL_THRESHOLD:
                    return LifecyclePhase.DEPRECATED, "Performance degraded, deprecating"

            # Or return to experimental if adaptation needed
            if scores.recommendation == SelectionOperator.FUCK:
                if scores.fuck_score > scores.marry_score * 1.2:  # 20% higher
                    return LifecyclePhase.EXPERIMENTAL, "Adaptation needed, returning to experimental"

            return None, "Maintain stable status"

        elif current_phase == LifecyclePhase.DEPRECATED:
            # Deprecated entities proceed to elimination
            time_deprecated = datetime.utcnow() - state.phase_entered_at
            if time_deprecated > timedelta(hours=24):  # Grace period
                return LifecyclePhase.ELIMINATED, "Grace period expired, eliminating"

            # Allow recovery if metrics improve significantly
            if scores.recommendation == SelectionOperator.MARRY:
                return LifecyclePhase.EXPERIMENTAL, "Metrics improved, returning to experimental"

            return None, "In deprecation grace period"

        return None, "No transition applicable"

    def transition_phase(
        self,
        entity_id: str,
        new_phase: LifecyclePhase,
        reason: str,
    ) -> LifecycleState:
        """Execute a phase transition for an entity."""
        state = self.entities.get(entity_id)
        if not state:
            raise ValueError(f"Entity {entity_id} not found")

        old_phase = state.phase

        # Record transition in history
        state.history.append({
            "timestamp": datetime.utcnow().isoformat(),
            "event": "phase_transition",
            "from_phase": old_phase.value,
            "to_phase": new_phase.value,
            "reason": reason,
            "scores": {
                "kill": state.scores.kill_score,
                "fuck": state.scores.fuck_score,
                "marry": state.scores.marry_score,
            },
        })

        # Update state
        state.phase = new_phase
        state.phase_entered_at = datetime.utcnow()
        state.updated_at = datetime.utcnow()

        # Record global transition
        self._phase_transitions.append({
            "entity_id": entity_id,
            "entity_type": state.entity_type,
            "from_phase": old_phase.value,
            "to_phase": new_phase.value,
            "reason": reason,
            "timestamp": datetime.utcnow().isoformat(),
        })

        return state

    def get_lifecycle_dashboard(self) -> dict[str, Any]:
        """Get a dashboard view of all entities and their lifecycle states."""
        by_phase = {phase.value: [] for phase in LifecyclePhase}
        by_recommendation = {op.value: [] for op in SelectionOperator}

        for entity_id, state in self.entities.items():
            summary = {
                "id": entity_id,
                "type": state.entity_type,
                "phase": state.phase.value,
                "kfm_scores": {
                    "K": round(state.scores.kill_score, 3),
                    "F": round(state.scores.fuck_score, 3),
                    "M": round(state.scores.marry_score, 3),
                },
                "recommendation": state.scores.recommendation.value,
                "confidence": round(state.scores.confidence, 3),
            }

            by_phase[state.phase.value].append(summary)
            by_recommendation[state.scores.recommendation.value].append(entity_id)

        return {
            "total_entities": len(self.entities),
            "by_phase": {
                phase: len(entities)
                for phase, entities in by_phase.items()
            },
            "by_recommendation": by_recommendation,
            "recent_transitions": self._phase_transitions[-10:],
            "entities": by_phase,
        }


# === Convenience Functions ===

def compute_agent_kfm_score(
    success_rate: float,
    error_rate: float,
    alignment_score: float,
    collaboration_success: float,
    innovation_potential: float = 0.5,
    time_active_hours: float = 0,
) -> KFMScores:
    """
    Quick computation of KFM scores for an agent.

    Args:
        success_rate: Task completion rate (0-1)
        error_rate: Failure frequency (0-1)
        alignment_score: Overall alignment with system (0-1)
        collaboration_success: Multi-agent task success (0-1)
        innovation_potential: Ability to generate novel behaviors (0-1)
        time_active_hours: Hours since agent became active

    Returns:
        KFMScores with recommendation
    """
    engine = KFMLifecycleEngine()
    state = engine.register_entity("temp", "agent")

    engine.update_metrics(
        "temp",
        performance=PerformanceMetrics(
            success_rate=success_rate,
            error_rate=error_rate,
        ),
        alignment=AlignmentMetrics(
            goal_alignment=alignment_score,
            collaboration_success=collaboration_success,
        ),
        evolution=EvolutionPotential(
            innovation_score=innovation_potential,
        ),
    )

    # Adjust for time factor
    if time_active_hours > 0:
        state.phase_entered_at = datetime.utcnow() - timedelta(hours=time_active_hours)
        state.scores = engine._compute_kfm_scores(state)

    return state.scores


def recommend_agent_action(
    agent_id: str,
    alignment_result: dict,
    performance_history: list[dict],
) -> dict[str, Any]:
    """
    Recommend KFM action for an agent based on alignment and performance.

    Args:
        agent_id: The agent identifier
        alignment_result: Result from full_alignment_check
        performance_history: List of recent performance records

    Returns:
        Recommendation with action and reasoning
    """
    # Extract alignment score
    if "overall_score" in alignment_result:
        alignment = alignment_result["overall_score"]
    elif "error" in alignment_result:
        alignment = 0.0
    else:
        # Compute from strategies
        strategies = ["knowledge", "goals", "terminology", "assumptions", "context"]
        scores = []
        for s in strategies:
            if s in alignment_result and hasattr(alignment_result[s], "confidence"):
                scores.append(alignment_result[s].confidence)
        alignment = sum(scores) / len(scores) if scores else 0.5

    # Extract performance metrics
    if performance_history:
        recent = performance_history[-10:]  # Last 10 records
        success_rate = sum(1 for p in recent if p.get("success", False)) / len(recent)
        error_rate = sum(1 for p in recent if p.get("error", False)) / len(recent)
    else:
        success_rate = 0.5
        error_rate = 0.1

    scores = compute_agent_kfm_score(
        success_rate=success_rate,
        error_rate=error_rate,
        alignment_score=alignment,
        collaboration_success=alignment * success_rate,
    )

    # Map recommendation to actionable guidance
    actions = {
        SelectionOperator.KILL: {
            "action": "deprecate",
            "description": "Schedule agent for removal from the network",
            "steps": [
                "Stop assigning new tasks",
                "Transfer ongoing work to other agents",
                "Archive agent state and learnings",
                "Deregister from system",
            ],
        },
        SelectionOperator.FUCK: {
            "action": "experiment",
            "description": "Continue testing and adaptation",
            "steps": [
                "Assign varied task types to test capabilities",
                "Monitor for pattern emergence",
                "Track learning and adaptation rates",
                "Evaluate alignment improvements",
            ],
        },
        SelectionOperator.MARRY: {
            "action": "integrate",
            "description": "Promote to stable production status",
            "steps": [
                "Mark as production-ready",
                "Increase resource allocation",
                "Prioritize for critical tasks",
                "Establish as collaboration partner",
            ],
        },
    }

    return {
        "agent_id": agent_id,
        "scores": {
            "kill": round(scores.kill_score, 3),
            "fuck": round(scores.fuck_score, 3),
            "marry": round(scores.marry_score, 3),
        },
        "recommendation": scores.recommendation.value,
        "confidence": round(scores.confidence, 3),
        "reasoning": scores.reasoning,
        **actions[scores.recommendation],
    }


# === Singleton Engine ===

_kfm_engine: KFMLifecycleEngine | None = None


def get_kfm_engine() -> KFMLifecycleEngine:
    """Get the singleton KFM lifecycle engine."""
    global _kfm_engine
    if _kfm_engine is None:
        _kfm_engine = KFMLifecycleEngine()
    return _kfm_engine


def reset_kfm_engine() -> None:
    """Reset the KFM engine (for testing)."""
    global _kfm_engine
    _kfm_engine = None

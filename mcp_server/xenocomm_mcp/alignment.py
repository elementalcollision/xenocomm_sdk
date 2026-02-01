"""
XenoComm Alignment Engine
=========================

Implements the CommonGroundFramework strategies as a Python engine that can be
exposed via MCP tools. These strategies help establish mutual understanding
between heterogeneous AI agents.

Strategies:
- Knowledge Verification: Verify shared knowledge between agents
- Goal Alignment: Check goal compatibility using compatibility matrix
- Terminology Alignment: Ensure consistent terminology via mappings
- Assumption Verification: Surface and validate critical assumptions
- Context Synchronization: Align contextual parameters between agents
"""

from dataclasses import dataclass, field
from typing import Any
from enum import Enum
import hashlib
import json


class AlignmentStatus(Enum):
    """Status of an alignment verification."""
    ALIGNED = "aligned"
    MISALIGNED = "misaligned"
    PARTIAL = "partial"
    UNKNOWN = "unknown"


@dataclass
class AlignmentResult:
    """Result of an alignment verification."""
    status: AlignmentStatus
    confidence: float  # 0.0 to 1.0
    details: dict[str, Any] = field(default_factory=dict)
    recommendations: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "status": self.status.value,
            "confidence": self.confidence,
            "details": self.details,
            "recommendations": self.recommendations,
        }


@dataclass
class AgentContext:
    """Context information about an agent."""
    agent_id: str
    capabilities: dict[str, Any] = field(default_factory=dict)
    knowledge_domains: list[str] = field(default_factory=list)
    goals: list[dict[str, Any]] = field(default_factory=list)
    terminology: dict[str, str] = field(default_factory=dict)
    assumptions: list[str] = field(default_factory=list)
    context_params: dict[str, Any] = field(default_factory=dict)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AgentContext":
        return cls(
            agent_id=data.get("agent_id", "unknown"),
            capabilities=data.get("capabilities", {}),
            knowledge_domains=data.get("knowledge_domains", []),
            goals=data.get("goals", []),
            terminology=data.get("terminology", {}),
            assumptions=data.get("assumptions", []),
            context_params=data.get("context_params", {}),
        )


class AlignmentEngine:
    """
    Engine for verifying alignment between AI agents.

    Implements the CommonGroundFramework strategies for establishing
    mutual understanding between heterogeneous agents.
    """

    def __init__(self):
        self.registered_agents: dict[str, AgentContext] = {}
        self.goal_compatibility_matrix: dict[str, dict[str, float]] = {}
        self.terminology_mappings: dict[str, dict[str, str]] = {}

    def register_agent(self, context: AgentContext) -> None:
        """Register an agent's context for alignment verification."""
        self.registered_agents[context.agent_id] = context

    def verify_knowledge(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
        required_domains: list[str] | None = None,
    ) -> AlignmentResult:
        """
        Verify shared knowledge between two agents.

        Checks if agents have overlapping knowledge domains and can
        communicate about required topics.
        """
        domains_a = set(agent_a.knowledge_domains)
        domains_b = set(agent_b.knowledge_domains)

        shared = domains_a & domains_b
        all_domains = domains_a | domains_b

        # Calculate overlap ratio
        overlap_ratio = len(shared) / len(all_domains) if all_domains else 0.0

        # Check required domains
        missing_a = []
        missing_b = []
        if required_domains:
            for domain in required_domains:
                if domain not in domains_a:
                    missing_a.append(domain)
                if domain not in domains_b:
                    missing_b.append(domain)

        # Determine status
        if required_domains and (missing_a or missing_b):
            status = AlignmentStatus.MISALIGNED if (missing_a and missing_b) else AlignmentStatus.PARTIAL
        elif overlap_ratio > 0.5:
            status = AlignmentStatus.ALIGNED
        elif overlap_ratio > 0.2:
            status = AlignmentStatus.PARTIAL
        else:
            status = AlignmentStatus.MISALIGNED

        recommendations = []
        if missing_a:
            recommendations.append(f"Agent {agent_a.agent_id} should acquire knowledge in: {', '.join(missing_a)}")
        if missing_b:
            recommendations.append(f"Agent {agent_b.agent_id} should acquire knowledge in: {', '.join(missing_b)}")
        if overlap_ratio < 0.3:
            recommendations.append("Consider using a translation/mediation layer for cross-domain communication")

        return AlignmentResult(
            status=status,
            confidence=overlap_ratio,
            details={
                "shared_domains": list(shared),
                "agent_a_only": list(domains_a - domains_b),
                "agent_b_only": list(domains_b - domains_a),
                "overlap_ratio": overlap_ratio,
                "missing_required_a": missing_a,
                "missing_required_b": missing_b,
            },
            recommendations=recommendations,
        )

    def verify_goals(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
    ) -> AlignmentResult:
        """
        Verify goal compatibility between two agents.

        Uses a compatibility matrix to check if agents' goals can coexist
        or if there are conflicts that need resolution.
        """
        if not agent_a.goals or not agent_b.goals:
            return AlignmentResult(
                status=AlignmentStatus.UNKNOWN,
                confidence=0.0,
                details={"reason": "One or both agents have no declared goals"},
                recommendations=["Both agents should declare their goals for alignment verification"],
            )

        conflicts = []
        alignments = []

        for goal_a in agent_a.goals:
            goal_type_a = goal_a.get("type", "unknown")
            for goal_b in agent_b.goals:
                goal_type_b = goal_b.get("type", "unknown")

                # Check compatibility matrix
                compat_key = f"{goal_type_a}:{goal_type_b}"
                reverse_key = f"{goal_type_b}:{goal_type_a}"

                compatibility = self.goal_compatibility_matrix.get(
                    compat_key,
                    self.goal_compatibility_matrix.get(reverse_key, 0.5)
                )

                if compatibility < 0.3:
                    conflicts.append({
                        "goal_a": goal_a,
                        "goal_b": goal_b,
                        "compatibility": compatibility,
                    })
                elif compatibility > 0.7:
                    alignments.append({
                        "goal_a": goal_a,
                        "goal_b": goal_b,
                        "compatibility": compatibility,
                    })

        # Calculate overall compatibility
        total_pairs = len(agent_a.goals) * len(agent_b.goals)
        conflict_ratio = len(conflicts) / total_pairs if total_pairs else 0
        alignment_ratio = len(alignments) / total_pairs if total_pairs else 0

        if conflict_ratio > 0.3:
            status = AlignmentStatus.MISALIGNED
        elif alignment_ratio > 0.5:
            status = AlignmentStatus.ALIGNED
        else:
            status = AlignmentStatus.PARTIAL

        recommendations = []
        if conflicts:
            recommendations.append("Resolve goal conflicts before proceeding with collaboration")
            for conflict in conflicts[:3]:  # Top 3 conflicts
                recommendations.append(
                    f"Conflict: {conflict['goal_a'].get('description', 'Goal A')} vs "
                    f"{conflict['goal_b'].get('description', 'Goal B')}"
                )

        return AlignmentResult(
            status=status,
            confidence=alignment_ratio,
            details={
                "conflicts": conflicts,
                "alignments": alignments,
                "conflict_ratio": conflict_ratio,
                "alignment_ratio": alignment_ratio,
            },
            recommendations=recommendations,
        )

    def align_terminology(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
    ) -> AlignmentResult:
        """
        Ensure consistent terminology between two agents.

        Creates mappings between terms used by different agents to ensure
        they understand each other correctly.
        """
        terms_a = set(agent_a.terminology.keys())
        terms_b = set(agent_b.terminology.keys())

        shared_terms = terms_a & terms_b
        unique_a = terms_a - terms_b
        unique_b = terms_b - terms_a

        # Check for semantic conflicts in shared terms
        conflicts = []
        for term in shared_terms:
            def_a = agent_a.terminology[term]
            def_b = agent_b.terminology[term]
            if def_a.lower() != def_b.lower():
                conflicts.append({
                    "term": term,
                    "definition_a": def_a,
                    "definition_b": def_b,
                })

        # Generate mapping recommendations
        suggested_mappings = {}
        for term_a in unique_a:
            def_a = agent_a.terminology[term_a]
            # Look for semantic matches in B's terminology
            for term_b, def_b in agent_b.terminology.items():
                if self._semantic_similarity(def_a, def_b) > 0.7:
                    suggested_mappings[term_a] = term_b
                    break

        conflict_ratio = len(conflicts) / len(shared_terms) if shared_terms else 0

        if conflict_ratio > 0.3:
            status = AlignmentStatus.MISALIGNED
        elif conflict_ratio > 0.1:
            status = AlignmentStatus.PARTIAL
        else:
            status = AlignmentStatus.ALIGNED

        recommendations = []
        if conflicts:
            recommendations.append("Resolve terminology conflicts before communication")
        if suggested_mappings:
            recommendations.append(f"Suggested term mappings: {suggested_mappings}")
        if unique_a:
            recommendations.append(f"Agent B should learn terms: {list(unique_a)[:5]}")
        if unique_b:
            recommendations.append(f"Agent A should learn terms: {list(unique_b)[:5]}")

        return AlignmentResult(
            status=status,
            confidence=1.0 - conflict_ratio,
            details={
                "shared_terms": list(shared_terms),
                "unique_to_a": list(unique_a),
                "unique_to_b": list(unique_b),
                "conflicts": conflicts,
                "suggested_mappings": suggested_mappings,
            },
            recommendations=recommendations,
        )

    def verify_assumptions(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
    ) -> AlignmentResult:
        """
        Surface and validate critical assumptions between agents.

        Identifies assumptions that one agent makes which the other
        may not share, potentially leading to miscommunication.
        """
        assumptions_a = set(agent_a.assumptions)
        assumptions_b = set(agent_b.assumptions)

        shared = assumptions_a & assumptions_b
        unique_a = assumptions_a - assumptions_b
        unique_b = assumptions_b - assumptions_a

        # Calculate assumption alignment
        all_assumptions = assumptions_a | assumptions_b
        alignment_ratio = len(shared) / len(all_assumptions) if all_assumptions else 1.0

        # Identify potentially conflicting assumptions
        conflicts = []
        for a_assumption in unique_a:
            for b_assumption in unique_b:
                if self._assumptions_conflict(a_assumption, b_assumption):
                    conflicts.append({
                        "assumption_a": a_assumption,
                        "assumption_b": b_assumption,
                    })

        if conflicts:
            status = AlignmentStatus.MISALIGNED
        elif alignment_ratio > 0.7:
            status = AlignmentStatus.ALIGNED
        elif alignment_ratio > 0.3:
            status = AlignmentStatus.PARTIAL
        else:
            status = AlignmentStatus.MISALIGNED

        recommendations = []
        if unique_a:
            recommendations.append(f"Agent A should communicate assumptions: {list(unique_a)[:3]}")
        if unique_b:
            recommendations.append(f"Agent B should communicate assumptions: {list(unique_b)[:3]}")
        if conflicts:
            recommendations.append("Critical: Resolve conflicting assumptions before proceeding")

        return AlignmentResult(
            status=status,
            confidence=alignment_ratio,
            details={
                "shared_assumptions": list(shared),
                "unique_to_a": list(unique_a),
                "unique_to_b": list(unique_b),
                "conflicts": conflicts,
            },
            recommendations=recommendations,
        )

    def sync_context(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
        required_params: list[str] | None = None,
    ) -> AlignmentResult:
        """
        Align contextual parameters between two agents.

        Ensures both agents have compatible context for the interaction,
        including environment settings, constraints, and preferences.
        """
        params_a = agent_a.context_params
        params_b = agent_b.context_params

        all_params = set(params_a.keys()) | set(params_b.keys())

        mismatches = []
        matches = []
        missing_a = []
        missing_b = []

        for param in all_params:
            val_a = params_a.get(param)
            val_b = params_b.get(param)

            if val_a is None:
                missing_a.append(param)
            elif val_b is None:
                missing_b.append(param)
            elif val_a != val_b:
                mismatches.append({
                    "param": param,
                    "value_a": val_a,
                    "value_b": val_b,
                })
            else:
                matches.append(param)

        # Check required parameters
        required_missing = []
        if required_params:
            for param in required_params:
                if param not in params_a or param not in params_b:
                    required_missing.append(param)

        # Calculate sync ratio
        total_params = len(all_params)
        sync_ratio = len(matches) / total_params if total_params else 1.0

        if required_missing:
            status = AlignmentStatus.MISALIGNED
        elif mismatches:
            status = AlignmentStatus.PARTIAL
        elif sync_ratio > 0.8:
            status = AlignmentStatus.ALIGNED
        else:
            status = AlignmentStatus.PARTIAL

        recommendations = []
        if required_missing:
            recommendations.append(f"Required parameters missing: {required_missing}")
        if mismatches:
            for m in mismatches[:3]:
                recommendations.append(
                    f"Sync {m['param']}: A={m['value_a']}, B={m['value_b']}"
                )

        return AlignmentResult(
            status=status,
            confidence=sync_ratio,
            details={
                "matched_params": matches,
                "mismatched_params": mismatches,
                "missing_in_a": missing_a,
                "missing_in_b": missing_b,
                "required_missing": required_missing,
            },
            recommendations=recommendations,
        )

    def full_alignment_check(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
        required_domains: list[str] | None = None,
        required_params: list[str] | None = None,
    ) -> dict[str, AlignmentResult]:
        """
        Run all alignment strategies and return comprehensive results.
        """
        return {
            "knowledge": self.verify_knowledge(agent_a, agent_b, required_domains),
            "goals": self.verify_goals(agent_a, agent_b),
            "terminology": self.align_terminology(agent_a, agent_b),
            "assumptions": self.verify_assumptions(agent_a, agent_b),
            "context": self.sync_context(agent_a, agent_b, required_params),
        }

    def _semantic_similarity(self, text_a: str, text_b: str) -> float:
        """
        Calculate semantic similarity between two texts.

        This is a simplified implementation. In production, this would use
        embeddings or more sophisticated NLP.
        """
        words_a = set(text_a.lower().split())
        words_b = set(text_b.lower().split())

        if not words_a or not words_b:
            return 0.0

        intersection = words_a & words_b
        union = words_a | words_b

        return len(intersection) / len(union)

    def _assumptions_conflict(self, assumption_a: str, assumption_b: str) -> bool:
        """
        Check if two assumptions potentially conflict.

        Simplified implementation - looks for negation patterns.
        """
        negations = ["not", "never", "no", "cannot", "won't", "shouldn't"]

        a_lower = assumption_a.lower()
        b_lower = assumption_b.lower()

        # Check if one negates what the other asserts
        for neg in negations:
            if neg in a_lower and neg not in b_lower:
                # Check for similar content
                if self._semantic_similarity(
                    a_lower.replace(neg, ""),
                    b_lower
                ) > 0.5:
                    return True
            if neg in b_lower and neg not in a_lower:
                if self._semantic_similarity(
                    b_lower.replace(neg, ""),
                    a_lower
                ) > 0.5:
                    return True

        return False

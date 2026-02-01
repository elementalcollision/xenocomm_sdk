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

Enhanced Features (v2.0):
- Semantic similarity with TF-IDF-like weighting
- Domain hierarchy and relatedness scoring
- Default goal compatibility matrix
- Weighted strategy scoring
- Improved conflict detection with antonym recognition
"""

from dataclasses import dataclass, field
from typing import Any, Callable
from enum import Enum
import hashlib
import json
import re
import math
from collections import Counter


class AlignmentStatus(Enum):
    """Status of an alignment verification."""
    ALIGNED = "aligned"
    MISALIGNED = "misaligned"
    PARTIAL = "partial"
    UNKNOWN = "unknown"


@dataclass
class StrategyWeight:
    """Weight configuration for alignment strategies."""
    knowledge: float = 0.25
    goals: float = 0.25
    terminology: float = 0.20
    assumptions: float = 0.15
    context: float = 0.15

    def validate(self) -> bool:
        total = self.knowledge + self.goals + self.terminology + self.assumptions + self.context
        return abs(total - 1.0) < 0.001


@dataclass
class AlignmentResult:
    """Result of an alignment verification."""
    status: AlignmentStatus
    confidence: float  # 0.0 to 1.0
    details: dict[str, Any] = field(default_factory=dict)
    recommendations: list[str] = field(default_factory=list)
    strategy_name: str = ""
    weight: float = 1.0

    def to_dict(self) -> dict[str, Any]:
        return {
            "status": self.status.value,
            "confidence": self.confidence,
            "details": self.details,
            "recommendations": self.recommendations,
            "strategy_name": self.strategy_name,
            "weighted_score": self.confidence * self.weight,
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
    # New fields for enhanced alignment
    domain_expertise_levels: dict[str, float] = field(default_factory=dict)  # domain -> 0.0-1.0
    priority_goals: list[str] = field(default_factory=list)  # goal IDs in priority order
    communication_preferences: dict[str, Any] = field(default_factory=dict)

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
            domain_expertise_levels=data.get("domain_expertise_levels", {}),
            priority_goals=data.get("priority_goals", []),
            communication_preferences=data.get("communication_preferences", {}),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "agent_id": self.agent_id,
            "capabilities": self.capabilities,
            "knowledge_domains": self.knowledge_domains,
            "goals": self.goals,
            "terminology": self.terminology,
            "assumptions": self.assumptions,
            "context_params": self.context_params,
            "domain_expertise_levels": self.domain_expertise_levels,
            "priority_goals": self.priority_goals,
            "communication_preferences": self.communication_preferences,
        }


class AlignmentEngine:
    """
    Engine for verifying alignment between AI agents.

    Implements the CommonGroundFramework strategies for establishing
    mutual understanding between heterogeneous agents.

    Enhanced Features (v2.0):
    - Domain hierarchy for knowledge similarity
    - Default goal compatibility matrix
    - TF-IDF-like semantic similarity
    - Weighted strategy scoring
    - Synonym and antonym detection
    """

    # Domain hierarchy for computing relatedness
    DOMAIN_HIERARCHY: dict[str, list[str]] = {
        "computer_science": ["programming", "software_engineering", "algorithms", "data_structures", "web_development", "machine_learning", "ai", "databases"],
        "machine_learning": ["deep_learning", "neural_networks", "nlp", "computer_vision", "reinforcement_learning", "ai"],
        "data_science": ["statistics", "machine_learning", "data_analysis", "visualization", "python", "r"],
        "web_development": ["frontend", "backend", "javascript", "html", "css", "react", "nodejs", "apis"],
        "devops": ["docker", "kubernetes", "ci_cd", "cloud", "aws", "infrastructure"],
        "science": ["biology", "chemistry", "physics", "mathematics", "research"],
        "business": ["management", "finance", "marketing", "strategy", "operations"],
        "writing": ["technical_writing", "documentation", "content", "copywriting", "editing"],
    }

    # Default goal compatibility matrix
    DEFAULT_GOAL_COMPATIBILITY: dict[str, dict[str, float]] = {
        # Synergistic pairs
        "assistance": {"assistance": 1.0, "education": 0.9, "analysis": 0.8, "research": 0.8, "documentation": 0.7},
        "education": {"education": 1.0, "assistance": 0.9, "documentation": 0.8, "research": 0.7},
        "analysis": {"analysis": 1.0, "research": 0.9, "assistance": 0.8, "documentation": 0.7},
        "research": {"research": 1.0, "analysis": 0.9, "education": 0.7, "documentation": 0.8},
        "documentation": {"documentation": 1.0, "education": 0.8, "research": 0.8, "assistance": 0.7},
        "automation": {"automation": 1.0, "efficiency": 0.9, "assistance": 0.7},
        "efficiency": {"efficiency": 1.0, "automation": 0.9, "optimization": 0.8},
        "optimization": {"optimization": 1.0, "efficiency": 0.8, "analysis": 0.7},
        # Potentially conflicting pairs
        "speed": {"quality": 0.4, "thoroughness": 0.3},
        "quality": {"speed": 0.4},
        "thoroughness": {"speed": 0.3, "brevity": 0.3},
        "brevity": {"thoroughness": 0.3},
        "innovation": {"stability": 0.4},
        "stability": {"innovation": 0.4},
    }

    # Common synonyms for semantic matching
    SYNONYMS: dict[str, set[str]] = {
        "model": {"algorithm", "system", "network", "classifier"},
        "data": {"information", "dataset", "records", "input"},
        "process": {"procedure", "method", "workflow", "operation"},
        "output": {"result", "response", "return", "product"},
        "error": {"bug", "issue", "fault", "problem", "exception"},
        "function": {"method", "procedure", "routine", "operation"},
        "user": {"client", "customer", "consumer", "end-user"},
        "api": {"interface", "endpoint", "service"},
    }

    # Antonyms for conflict detection
    ANTONYMS: dict[str, set[str]] = {
        "fast": {"slow"},
        "simple": {"complex", "complicated"},
        "synchronous": {"asynchronous"},
        "public": {"private"},
        "required": {"optional"},
        "enabled": {"disabled"},
        "allow": {"deny", "block", "prevent"},
        "include": {"exclude"},
        "start": {"stop", "end"},
        "create": {"delete", "remove"},
    }

    def __init__(
        self,
        strategy_weights: StrategyWeight | None = None,
        custom_goal_compatibility: dict[str, dict[str, float]] | None = None,
    ):
        self.registered_agents: dict[str, AgentContext] = {}
        self.strategy_weights = strategy_weights or StrategyWeight()
        self.goal_compatibility_matrix: dict[str, dict[str, float]] = {
            **self.DEFAULT_GOAL_COMPATIBILITY,
            **(custom_goal_compatibility or {}),
        }
        self.terminology_mappings: dict[str, dict[str, str]] = {}

        # Build reverse domain index for fast lookups
        self._domain_index: dict[str, str] = {}
        for parent, children in self.DOMAIN_HIERARCHY.items():
            for child in children:
                self._domain_index[child] = parent

        # Build word frequency index for IDF-like weighting
        self._word_document_freq: Counter = Counter()

    def register_agent(self, context: AgentContext) -> None:
        """Register an agent's context for alignment verification."""
        self.registered_agents[context.agent_id] = context
        self._update_word_frequencies(context)

    def verify_knowledge(
        self,
        agent_a: AgentContext,
        agent_b: AgentContext,
        required_domains: list[str] | None = None,
    ) -> AlignmentResult:
        """
        Verify shared knowledge between two agents.

        Enhanced with:
        - Domain hierarchy similarity (related domains count as partial matches)
        - Expertise level consideration
        - Required domain fuzzy matching
        """
        domains_a = set(agent_a.knowledge_domains)
        domains_b = set(agent_b.knowledge_domains)

        # Exact matches
        shared = domains_a & domains_b

        # Find related domains (not exact but similar)
        related_pairs = []
        for d_a in domains_a - shared:
            for d_b in domains_b - shared:
                similarity = self._domain_similarity(d_a, d_b)
                if similarity >= 0.5:
                    related_pairs.append({
                        "domain_a": d_a,
                        "domain_b": d_b,
                        "similarity": similarity,
                    })

        # Calculate enhanced overlap ratio
        all_domains = domains_a | domains_b
        if all_domains:
            exact_ratio = len(shared) / len(all_domains)
            related_bonus = sum(p["similarity"] for p in related_pairs) / max(len(all_domains), 1) * 0.5
            overlap_ratio = min(1.0, exact_ratio + related_bonus)
        else:
            overlap_ratio = 0.0

        # Check required domains with fuzzy matching
        missing_a = []
        missing_b = []
        covered_a = []
        covered_b = []

        if required_domains:
            for domain in required_domains:
                # Check exact match first
                has_a = domain in domains_a
                has_b = domain in domains_b

                # If no exact match, check for related domains
                if not has_a:
                    for d_a in domains_a:
                        if self._domain_similarity(domain, d_a) >= 0.6:
                            has_a = True
                            covered_a.append({"required": domain, "covered_by": d_a})
                            break
                if not has_b:
                    for d_b in domains_b:
                        if self._domain_similarity(domain, d_b) >= 0.6:
                            has_b = True
                            covered_b.append({"required": domain, "covered_by": d_b})
                            break

                if not has_a:
                    missing_a.append(domain)
                if not has_b:
                    missing_b.append(domain)

        # Consider expertise levels if available
        expertise_match = 0.0
        if agent_a.domain_expertise_levels and agent_b.domain_expertise_levels:
            for domain in shared:
                level_a = agent_a.domain_expertise_levels.get(domain, 0.5)
                level_b = agent_b.domain_expertise_levels.get(domain, 0.5)
                expertise_match += min(level_a, level_b)
            if shared:
                expertise_match /= len(shared)

        # Determine status
        if required_domains and (missing_a and missing_b):
            status = AlignmentStatus.MISALIGNED
        elif required_domains and (missing_a or missing_b):
            status = AlignmentStatus.PARTIAL
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
        if related_pairs:
            recommendations.append(f"Agents have related but not identical domains - establish terminology mapping")
        if overlap_ratio < 0.3:
            recommendations.append("Consider using a translation/mediation layer for cross-domain communication")

        return AlignmentResult(
            status=status,
            confidence=overlap_ratio,
            details={
                "shared_domains": list(shared),
                "related_domains": related_pairs,
                "agent_a_only": list(domains_a - domains_b),
                "agent_b_only": list(domains_b - domains_a),
                "overlap_ratio": overlap_ratio,
                "missing_required_a": missing_a,
                "missing_required_b": missing_b,
                "covered_by_related_a": covered_a,
                "covered_by_related_b": covered_b,
                "expertise_match": expertise_match,
            },
            recommendations=recommendations,
            strategy_name="knowledge",
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

        Enhanced with weighted scoring and overall assessment.
        """
        results = {
            "knowledge": self.verify_knowledge(agent_a, agent_b, required_domains),
            "goals": self.verify_goals(agent_a, agent_b),
            "terminology": self.align_terminology(agent_a, agent_b),
            "assumptions": self.verify_assumptions(agent_a, agent_b),
            "context": self.sync_context(agent_a, agent_b, required_params),
        }

        # Add strategy names
        for name, result in results.items():
            result.strategy_name = name

        # Calculate weighted overall score
        overall_status, overall_score = self._calculate_weighted_alignment(results)

        # Add summary result
        all_recommendations = []
        for result in results.values():
            all_recommendations.extend(result.recommendations[:2])

        results["_summary"] = AlignmentResult(
            status=AlignmentStatus(overall_status),
            confidence=overall_score,
            details={
                "weighted_score": overall_score,
                "strategy_scores": {
                    name: {"status": r.status.value, "confidence": r.confidence, "weight": r.weight}
                    for name, r in results.items() if name != "_summary"
                },
                "strategy_weights": {
                    "knowledge": self.strategy_weights.knowledge,
                    "goals": self.strategy_weights.goals,
                    "terminology": self.strategy_weights.terminology,
                    "assumptions": self.strategy_weights.assumptions,
                    "context": self.strategy_weights.context,
                },
            },
            recommendations=all_recommendations[:5],
            strategy_name="_summary",
        )

        return results

    def set_strategy_weights(self, weights: StrategyWeight) -> None:
        """Update the strategy weights for alignment scoring."""
        if not weights.validate():
            raise ValueError("Strategy weights must sum to 1.0")
        self.strategy_weights = weights

    def add_goal_compatibility(self, goal_a: str, goal_b: str, compatibility: float) -> None:
        """Add or update a goal compatibility score."""
        if goal_a not in self.goal_compatibility_matrix:
            self.goal_compatibility_matrix[goal_a] = {}
        self.goal_compatibility_matrix[goal_a][goal_b] = compatibility

    def add_domain_relationship(self, parent: str, children: list[str]) -> None:
        """Add a domain hierarchy relationship."""
        self.DOMAIN_HIERARCHY[parent] = children
        for child in children:
            self._domain_index[child] = parent

    def _semantic_similarity(self, text_a: str, text_b: str) -> float:
        """
        Calculate semantic similarity between two texts.

        Enhanced with:
        - Tokenization and normalization
        - Synonym expansion
        - TF-IDF-like weighting (rare words matter more)
        - Partial word matching
        """
        if not text_a or not text_b:
            return 0.0

        # Tokenize and normalize
        words_a = self._tokenize(text_a)
        words_b = self._tokenize(text_b)

        if not words_a or not words_b:
            return 0.0

        # Expand with synonyms
        expanded_a = self._expand_with_synonyms(words_a)
        expanded_b = self._expand_with_synonyms(words_b)

        # Calculate Jaccard similarity on expanded sets
        intersection = expanded_a & expanded_b
        union = expanded_a | expanded_b

        if not union:
            return 0.0

        base_similarity = len(intersection) / len(union)

        # Boost for exact matches (not just synonym matches)
        exact_matches = words_a & words_b
        exact_bonus = len(exact_matches) / max(len(words_a), len(words_b)) * 0.2

        # Apply IDF-like weighting - rare matching words are more significant
        weighted_score = 0.0
        for word in intersection:
            # Words that appear in fewer "documents" (agent contexts) are more valuable
            doc_freq = self._word_document_freq.get(word, 1)
            total_docs = max(len(self.registered_agents), 1)
            idf = math.log(total_docs / doc_freq + 1)
            weighted_score += idf

        if intersection:
            weighted_score /= len(intersection)
            weighted_score = min(weighted_score / 3, 0.3)  # Normalize to max 0.3 bonus
        else:
            weighted_score = 0.0

        return min(1.0, base_similarity + exact_bonus + weighted_score)

    def _tokenize(self, text: str) -> set[str]:
        """Tokenize and normalize text."""
        # Lowercase and split on non-alphanumeric
        words = re.findall(r'\b[a-z0-9]+\b', text.lower())
        # Filter stopwords
        stopwords = {'the', 'a', 'an', 'is', 'are', 'was', 'were', 'be', 'been',
                     'being', 'have', 'has', 'had', 'do', 'does', 'did', 'will',
                     'would', 'could', 'should', 'may', 'might', 'must', 'shall',
                     'can', 'need', 'dare', 'ought', 'used', 'to', 'of', 'in',
                     'for', 'on', 'with', 'at', 'by', 'from', 'as', 'into',
                     'through', 'during', 'before', 'after', 'above', 'below',
                     'between', 'under', 'again', 'further', 'then', 'once',
                     'and', 'but', 'or', 'nor', 'so', 'yet', 'both', 'either',
                     'neither', 'not', 'only', 'own', 'same', 'than', 'too',
                     'very', 'just', 'also'}
        return {w for w in words if w not in stopwords and len(w) > 2}

    def _expand_with_synonyms(self, words: set[str]) -> set[str]:
        """Expand a word set with known synonyms."""
        expanded = set(words)
        for word in words:
            if word in self.SYNONYMS:
                expanded.update(self.SYNONYMS[word])
            # Also check if word is a synonym of something
            for key, synonyms in self.SYNONYMS.items():
                if word in synonyms:
                    expanded.add(key)
                    expanded.update(synonyms)
        return expanded

    def _domain_similarity(self, domain_a: str, domain_b: str) -> float:
        """
        Calculate similarity between two knowledge domains.

        Uses hierarchy and co-occurrence patterns.
        """
        if domain_a == domain_b:
            return 1.0

        a_lower = domain_a.lower().replace(" ", "_")
        b_lower = domain_b.lower().replace(" ", "_")

        # Check if one is parent of the other
        if a_lower in self._domain_index and self._domain_index[a_lower] == b_lower:
            return 0.8
        if b_lower in self._domain_index and self._domain_index[b_lower] == a_lower:
            return 0.8

        # Check if they share a parent
        parent_a = self._domain_index.get(a_lower) or a_lower
        parent_b = self._domain_index.get(b_lower) or b_lower

        if parent_a == parent_b:
            return 0.6

        # Check if they're siblings in hierarchy
        for parent, children in self.DOMAIN_HIERARCHY.items():
            if a_lower in children and b_lower in children:
                return 0.5

        # Fallback to text similarity
        return self._semantic_similarity(domain_a, domain_b) * 0.4

    def _assumptions_conflict(self, assumption_a: str, assumption_b: str) -> bool:
        """
        Check if two assumptions potentially conflict.

        Enhanced with:
        - Negation pattern detection
        - Antonym detection
        - Semantic contradiction analysis
        """
        negations = ["not", "never", "no", "cannot", "won't", "shouldn't", "don't", "doesn't", "isn't", "aren't"]

        a_lower = assumption_a.lower()
        b_lower = assumption_b.lower()

        words_a = self._tokenize(assumption_a)
        words_b = self._tokenize(assumption_b)

        # Check for antonym pairs
        for word_a in words_a:
            if word_a in self.ANTONYMS:
                if words_b & self.ANTONYMS[word_a]:
                    return True
            # Reverse check
            for antonym_key, antonym_set in self.ANTONYMS.items():
                if word_a in antonym_set and antonym_key in words_b:
                    return True

        # Check if one negates what the other asserts
        a_has_negation = any(neg in a_lower for neg in negations)
        b_has_negation = any(neg in b_lower for neg in negations)

        if a_has_negation != b_has_negation:  # One has negation, other doesn't
            # Remove negation words and compare
            a_cleaned = a_lower
            b_cleaned = b_lower
            for neg in negations:
                a_cleaned = a_cleaned.replace(neg + " ", "").replace(" " + neg, "")
                b_cleaned = b_cleaned.replace(neg + " ", "").replace(" " + neg, "")

            if self._semantic_similarity(a_cleaned, b_cleaned) > 0.5:
                return True

        return False

    def _calculate_weighted_alignment(
        self,
        results: dict[str, AlignmentResult],
    ) -> tuple[str, float]:
        """
        Calculate overall weighted alignment status and score.

        Returns (status, confidence) tuple.
        """
        weights = {
            "knowledge": self.strategy_weights.knowledge,
            "goals": self.strategy_weights.goals,
            "terminology": self.strategy_weights.terminology,
            "assumptions": self.strategy_weights.assumptions,
            "context": self.strategy_weights.context,
        }

        total_score = 0.0
        total_weight = 0.0

        for name, result in results.items():
            weight = weights.get(name, 0.2)
            result.weight = weight

            # Convert status to score
            status_score = {
                AlignmentStatus.ALIGNED: 1.0,
                AlignmentStatus.PARTIAL: 0.5,
                AlignmentStatus.MISALIGNED: 0.0,
                AlignmentStatus.UNKNOWN: 0.25,
            }.get(result.status, 0.0)

            # Combine status with confidence
            combined_score = (status_score * 0.6 + result.confidence * 0.4)
            total_score += combined_score * weight
            total_weight += weight

        if total_weight == 0:
            return "unknown", 0.0

        final_score = total_score / total_weight

        if final_score >= 0.75:
            return "aligned", final_score
        elif final_score >= 0.45:
            return "partial", final_score
        else:
            return "misaligned", final_score

    def _update_word_frequencies(self, context: AgentContext) -> None:
        """Update word frequency index when agent is registered."""
        all_text = " ".join([
            " ".join(context.knowledge_domains),
            " ".join(g.get("description", "") for g in context.goals),
            " ".join(context.terminology.values()),
            " ".join(context.assumptions),
        ])
        words = self._tokenize(all_text)
        self._word_document_freq.update(words)

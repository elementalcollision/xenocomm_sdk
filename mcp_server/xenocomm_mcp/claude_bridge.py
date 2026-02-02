"""
XenoComm Claude Agent Bridge

Enables Claude Cowork agents to:
1. Register as XenoComm participants
2. Negotiate communication protocols with other agents
3. Contribute to dynamic language emergence
4. Be observed via the flow dashboard

This bridge connects the Claude agent ecosystem to XenoComm's
protocol negotiation and emergence capabilities.
"""

from __future__ import annotations

import uuid
import json
import hashlib
from datetime import datetime
from dataclasses import dataclass, field
from typing import Any, Callable
from enum import Enum
from collections import defaultdict

from .alignment import AgentContext
from .observation import (
    ObservationManager,
    FlowEvent,
    FlowType,
    EventSeverity,
    get_observation_manager,
)


# ==================== Agent Session Management ====================

class AgentType(Enum):
    """Types of Claude agents that can connect."""
    COWORK = "cowork"           # Claude Cowork desktop agent
    CODE = "claude_code"        # Claude Code CLI agent
    API = "api_agent"           # API-based agent
    MCP_CLIENT = "mcp_client"   # Generic MCP client
    CUSTOM = "custom"           # Custom agent implementation


@dataclass
class ClaudeAgentSession:
    """Represents an active Claude agent session."""
    session_id: str
    agent_id: str
    agent_type: AgentType
    created_at: datetime
    last_active: datetime
    capabilities: dict[str, Any]
    knowledge_domains: list[str]
    conversation_context: dict[str, Any] = field(default_factory=dict)
    active_collaborations: list[str] = field(default_factory=list)
    message_count: int = 0
    language_contributions: list[dict] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "session_id": self.session_id,
            "agent_id": self.agent_id,
            "agent_type": self.agent_type.value,
            "created_at": self.created_at.isoformat(),
            "last_active": self.last_active.isoformat(),
            "capabilities": self.capabilities,
            "knowledge_domains": self.knowledge_domains,
            "active_collaborations": self.active_collaborations,
            "message_count": self.message_count,
            "language_contributions_count": len(self.language_contributions),
        }


@dataclass
class AgentMessage:
    """A message exchanged between agents."""
    message_id: str
    from_agent: str
    to_agent: str | None  # None = broadcast
    timestamp: datetime
    message_type: str  # "request", "response", "notification", "negotiation"
    content_hash: str  # Hash of content for pattern detection (not raw content)
    intent: str        # High-level intent classification
    protocol_markers: list[str] = field(default_factory=list)
    metadata: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "message_id": self.message_id,
            "from_agent": self.from_agent,
            "to_agent": self.to_agent,
            "timestamp": self.timestamp.isoformat(),
            "message_type": self.message_type,
            "content_hash": self.content_hash,
            "intent": self.intent,
            "protocol_markers": self.protocol_markers,
            "metadata": self.metadata,
        }


# ==================== Language Pattern Detection ====================

@dataclass
class LanguagePattern:
    """A detected communication pattern that could become protocol."""
    pattern_id: str
    name: str
    description: str
    detected_at: datetime
    occurrence_count: int
    participating_agents: set[str]
    intent_sequence: list[str]  # e.g., ["request", "clarify", "confirm", "execute"]
    success_rate: float
    example_exchanges: list[str]  # Message IDs
    promoted_to_protocol: bool = False

    def to_dict(self) -> dict[str, Any]:
        return {
            "pattern_id": self.pattern_id,
            "name": self.name,
            "description": self.description,
            "detected_at": self.detected_at.isoformat(),
            "occurrence_count": self.occurrence_count,
            "participating_agents": list(self.participating_agents),
            "intent_sequence": self.intent_sequence,
            "success_rate": self.success_rate,
            "promoted_to_protocol": self.promoted_to_protocol,
        }


class LanguageEvolutionEngine:
    """
    Detects and evolves communication patterns into dynamic languages.

    Watches agent interactions for:
    - Recurring intent sequences
    - Successful collaboration patterns
    - Novel communication structures

    Promotes successful patterns to protocol variants for A/B testing.
    """

    def __init__(self, observation_manager: ObservationManager):
        self.obs = observation_manager
        self.patterns: dict[str, LanguagePattern] = {}
        self.message_history: list[AgentMessage] = []
        self.intent_sequences: dict[str, list[str]] = defaultdict(list)  # session -> intents
        self._pattern_callbacks: list[Callable[[LanguagePattern], None]] = []

        # Pattern detection thresholds
        self.min_occurrences_for_pattern = 3
        self.min_success_rate_for_promotion = 0.7
        self.sequence_window_size = 10

    def record_message(self, message: AgentMessage, session_id: str) -> None:
        """Record a message and check for emerging patterns."""
        self.message_history.append(message)

        # Track intent sequence for this session
        self.intent_sequences[session_id].append(message.intent)

        # Keep window size manageable
        if len(self.intent_sequences[session_id]) > self.sequence_window_size:
            self.intent_sequences[session_id] = self.intent_sequences[session_id][-self.sequence_window_size:]

        # Emit observation event
        self.obs.event_bus.publish(FlowEvent(
            event_id=str(uuid.uuid4()),
            flow_type=FlowType.COLLABORATION,
            event_name="agent_message",
            timestamp=datetime.utcnow(),
            source_agent=message.from_agent,
            target_agent=message.to_agent,
            session_id=session_id,
            summary=f"{message.message_type}: {message.intent}",
            metrics={
                "message_type": message.message_type,
                "intent": message.intent,
                "markers": message.protocol_markers,
            },
            tags=["language", "message", message.message_type],
        ))

        # Check for pattern emergence
        self._detect_patterns()

    def _detect_patterns(self) -> None:
        """Detect recurring patterns in message sequences."""
        # Look for repeated intent sequences across sessions
        sequence_counts: dict[tuple, list[str]] = defaultdict(list)

        for session_id, intents in self.intent_sequences.items():
            if len(intents) >= 3:
                # Check subsequences of length 3-5
                for length in range(3, min(6, len(intents) + 1)):
                    for i in range(len(intents) - length + 1):
                        seq = tuple(intents[i:i + length])
                        sequence_counts[seq].append(session_id)

        # Check if any sequences meet threshold
        for seq, sessions in sequence_counts.items():
            if len(sessions) >= self.min_occurrences_for_pattern:
                pattern_id = self._sequence_to_id(seq)

                if pattern_id not in self.patterns:
                    # New pattern detected!
                    pattern = LanguagePattern(
                        pattern_id=pattern_id,
                        name=self._generate_pattern_name(seq),
                        description=f"Detected sequence: {' → '.join(seq)}",
                        detected_at=datetime.utcnow(),
                        occurrence_count=len(sessions),
                        participating_agents=set(),  # Will be populated
                        intent_sequence=list(seq),
                        success_rate=0.0,  # Will be calculated
                        example_exchanges=[],
                    )
                    self.patterns[pattern_id] = pattern

                    # Emit pattern detection event
                    self.obs.emergence_sensor.emit(
                        "language_pattern_detected",
                        f"New pattern: {pattern.name}",
                        metrics={
                            "pattern_id": pattern_id,
                            "sequence": list(seq),
                            "occurrences": len(sessions),
                        },
                        tags=["emergence", "language", "pattern"],
                    )

                    # Notify callbacks
                    for callback in self._pattern_callbacks:
                        callback(pattern)
                else:
                    # Update existing pattern
                    self.patterns[pattern_id].occurrence_count = len(sessions)

    def _sequence_to_id(self, seq: tuple) -> str:
        """Generate stable ID for an intent sequence."""
        return hashlib.md5("→".join(seq).encode()).hexdigest()[:12]

    def _generate_pattern_name(self, seq: tuple) -> str:
        """Generate human-readable name for a pattern."""
        if seq[0] == "request" and seq[-1] == "confirm":
            return "Request-Confirm Flow"
        elif "negotiate" in seq:
            return "Negotiation Protocol"
        elif "clarify" in seq:
            return "Clarification Loop"
        elif seq[0] == "propose" and "accept" in seq:
            return "Proposal-Acceptance"
        else:
            return f"Pattern-{self._sequence_to_id(seq)[:6]}"

    def mark_exchange_success(self, session_id: str, success: bool) -> None:
        """Mark an exchange as successful or failed for pattern learning."""
        intents = self.intent_sequences.get(session_id, [])
        if len(intents) >= 3:
            seq = tuple(intents[-5:] if len(intents) >= 5 else intents)
            pattern_id = self._sequence_to_id(seq)

            if pattern_id in self.patterns:
                pattern = self.patterns[pattern_id]
                # Update success rate with exponential moving average
                alpha = 0.3
                pattern.success_rate = alpha * (1.0 if success else 0.0) + (1 - alpha) * pattern.success_rate

                # Check for promotion
                if (pattern.success_rate >= self.min_success_rate_for_promotion and
                    pattern.occurrence_count >= 5 and
                    not pattern.promoted_to_protocol):
                    self._promote_to_protocol(pattern)

    def _promote_to_protocol(self, pattern: LanguagePattern) -> None:
        """Promote a successful pattern to a protocol variant."""
        pattern.promoted_to_protocol = True

        self.obs.emergence_sensor.emit(
            "pattern_promoted_to_protocol",
            f"Pattern '{pattern.name}' promoted to protocol variant",
            severity=EventSeverity.INFO,
            metrics={
                "pattern_id": pattern.pattern_id,
                "success_rate": pattern.success_rate,
                "occurrences": pattern.occurrence_count,
            },
            tags=["emergence", "protocol", "promotion"],
        )

    def on_pattern_detected(self, callback: Callable[[LanguagePattern], None]) -> None:
        """Register callback for pattern detection events."""
        self._pattern_callbacks.append(callback)

    def get_patterns(self) -> list[LanguagePattern]:
        """Get all detected patterns."""
        return list(self.patterns.values())

    def get_pattern_stats(self) -> dict[str, Any]:
        """Get statistics about detected patterns."""
        patterns = list(self.patterns.values())
        promoted = [p for p in patterns if p.promoted_to_protocol]

        return {
            "total_patterns": len(patterns),
            "promoted_to_protocol": len(promoted),
            "avg_success_rate": sum(p.success_rate for p in patterns) / max(len(patterns), 1),
            "total_messages": len(self.message_history),
            "active_sessions": len(self.intent_sequences),
            "top_patterns": [
                p.to_dict() for p in sorted(patterns, key=lambda x: -x.occurrence_count)[:5]
            ],
        }


# ==================== Claude Agent Bridge ====================

class ClaudeAgentBridge:
    """
    Bridge connecting Claude agents to the XenoComm ecosystem.

    Provides:
    - Agent session management
    - Message routing and observation
    - Language evolution tracking
    - Protocol negotiation facilitation
    """

    def __init__(self, observation_manager: ObservationManager | None = None):
        self.obs = observation_manager or get_observation_manager()
        self.sessions: dict[str, ClaudeAgentSession] = {}
        self.language_engine = LanguageEvolutionEngine(self.obs)
        self._message_handlers: dict[str, Callable] = {}

        # Register for pattern events
        self.language_engine.on_pattern_detected(self._on_pattern_detected)

    def register_agent(
        self,
        agent_id: str,
        agent_type: AgentType = AgentType.COWORK,
        capabilities: dict[str, Any] | None = None,
        knowledge_domains: list[str] | None = None,
    ) -> ClaudeAgentSession:
        """Register a Claude agent and create a session."""
        session_id = str(uuid.uuid4())
        now = datetime.utcnow()

        session = ClaudeAgentSession(
            session_id=session_id,
            agent_id=agent_id,
            agent_type=agent_type,
            created_at=now,
            last_active=now,
            capabilities=capabilities or {},
            knowledge_domains=knowledge_domains or [],
        )

        self.sessions[session_id] = session

        # Emit registration event
        self.obs.agent_sensor.agent_registered(
            agent_id=agent_id,
            capabilities=list((capabilities or {}).keys()),
            domains=knowledge_domains or [],
        )

        # Also emit to collaboration flow for dashboard visibility
        self.obs.collaboration_sensor.emit(
            "claude_agent_connected",
            f"Claude agent '{agent_id}' ({agent_type.value}) connected",
            source_agent=agent_id,
            session_id=session_id,
            metrics={
                "agent_type": agent_type.value,
                "capabilities_count": len(capabilities or {}),
                "domains": knowledge_domains or [],
            },
            tags=["claude", "agent", "connection"],
        )

        return session

    def deregister_agent(self, session_id: str, reason: str = "normal") -> None:
        """Deregister an agent session."""
        if session_id in self.sessions:
            session = self.sessions.pop(session_id)

            self.obs.agent_sensor.agent_deregistered(
                agent_id=session.agent_id,
                reason=reason,
            )

    def send_message(
        self,
        session_id: str,
        to_agent: str | None,
        message_type: str,
        intent: str,
        content: str,  # We hash this, don't store raw
        protocol_markers: list[str] | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> AgentMessage:
        """
        Send a message from one agent to another (or broadcast).

        The actual content is hashed for pattern detection - we don't
        store or transmit raw conversation content for privacy.
        """
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Unknown session: {session_id}")

        session.last_active = datetime.utcnow()
        session.message_count += 1

        # Create message with content hash
        message = AgentMessage(
            message_id=str(uuid.uuid4()),
            from_agent=session.agent_id,
            to_agent=to_agent,
            timestamp=datetime.utcnow(),
            message_type=message_type,
            content_hash=hashlib.sha256(content.encode()).hexdigest()[:16],
            intent=intent,
            protocol_markers=protocol_markers or [],
            metadata=metadata or {},
        )

        # Record for language evolution
        self.language_engine.record_message(message, session_id)

        # Route to target if registered
        if to_agent:
            target_session = self._find_agent_session(to_agent)
            if target_session and to_agent in self._message_handlers:
                self._message_handlers[to_agent](message)

        return message

    def _find_agent_session(self, agent_id: str) -> ClaudeAgentSession | None:
        """Find a session by agent ID."""
        for session in self.sessions.values():
            if session.agent_id == agent_id:
                return session
        return None

    def register_message_handler(
        self,
        agent_id: str,
        handler: Callable[[AgentMessage], None],
    ) -> None:
        """Register a handler for messages sent to an agent."""
        self._message_handlers[agent_id] = handler

    def mark_collaboration_outcome(
        self,
        session_id: str,
        success: bool,
        notes: str = "",
    ) -> None:
        """Mark the outcome of a collaboration for learning."""
        self.language_engine.mark_exchange_success(session_id, success)

        session = self.sessions.get(session_id)
        if session:
            self.obs.collaboration_sensor.emit(
                "collaboration_outcome",
                f"Collaboration {'succeeded' if success else 'failed'}: {notes}",
                source_agent=session.agent_id,
                session_id=session_id,
                metrics={"success": success},
                severity=EventSeverity.INFO if success else EventSeverity.WARNING,
                tags=["outcome", "success" if success else "failure"],
            )

    def propose_language_construct(
        self,
        session_id: str,
        construct_type: str,
        definition: dict[str, Any],
        rationale: str,
    ) -> dict[str, Any]:
        """
        Allow an agent to propose a new language construct.

        Constructs can be:
        - "term": A new term/concept definition
        - "pattern": A communication pattern
        - "protocol": A structured interaction protocol
        - "shorthand": An abbreviated form for common exchanges
        """
        session = self.sessions.get(session_id)
        if not session:
            raise ValueError(f"Unknown session: {session_id}")

        contribution = {
            "contribution_id": str(uuid.uuid4()),
            "agent_id": session.agent_id,
            "construct_type": construct_type,
            "definition": definition,
            "rationale": rationale,
            "proposed_at": datetime.utcnow().isoformat(),
            "status": "proposed",
            "votes": {"for": 0, "against": 0},
        }

        session.language_contributions.append(contribution)

        self.obs.emergence_sensor.emit(
            "language_construct_proposed",
            f"Agent '{session.agent_id}' proposed {construct_type}: {definition.get('name', 'unnamed')}",
            source_agent=session.agent_id,
            session_id=session_id,
            metrics={
                "construct_type": construct_type,
                "definition_keys": list(definition.keys()),
            },
            tags=["emergence", "language", "proposal", construct_type],
        )

        return contribution

    def _on_pattern_detected(self, pattern: LanguagePattern) -> None:
        """Handle detected language patterns."""
        # Could trigger notifications to agents, propose protocol variants, etc.
        pass

    def get_active_sessions(self) -> list[dict[str, Any]]:
        """Get all active agent sessions."""
        return [s.to_dict() for s in self.sessions.values()]

    def get_language_stats(self) -> dict[str, Any]:
        """Get language evolution statistics."""
        return self.language_engine.get_pattern_stats()

    def get_bridge_status(self) -> dict[str, Any]:
        """Get overall bridge status."""
        return {
            "active_sessions": len(self.sessions),
            "sessions": [s.to_dict() for s in self.sessions.values()],
            "language_stats": self.language_engine.get_pattern_stats(),
            "message_handlers": list(self._message_handlers.keys()),
        }


# ==================== Intent Classification ====================

class IntentClassifier:
    """
    Classifies message intents for pattern detection.

    This is a simple rule-based classifier. In production,
    this could be enhanced with ML-based classification.
    """

    INTENT_KEYWORDS = {
        "request": ["please", "can you", "would you", "help", "need", "want"],
        "respond": ["here is", "here's", "the answer", "result", "found"],
        "clarify": ["what do you mean", "could you explain", "unclear", "?"],
        "confirm": ["yes", "correct", "confirmed", "agreed", "okay"],
        "reject": ["no", "cannot", "won't", "unable", "disagree"],
        "propose": ["suggest", "propose", "how about", "what if", "idea"],
        "accept": ["accept", "sounds good", "let's do", "approved"],
        "negotiate": ["instead", "alternatively", "counter", "modify"],
        "delegate": ["you handle", "take care of", "your turn", "pass to"],
        "complete": ["done", "finished", "completed", "ready"],
    }

    @classmethod
    def classify(cls, content: str) -> str:
        """Classify the intent of a message."""
        content_lower = content.lower()

        # Check each intent's keywords
        scores = {}
        for intent, keywords in cls.INTENT_KEYWORDS.items():
            score = sum(1 for kw in keywords if kw in content_lower)
            if score > 0:
                scores[intent] = score

        if scores:
            return max(scores, key=scores.get)
        return "general"

    @classmethod
    def extract_protocol_markers(cls, content: str) -> list[str]:
        """Extract protocol markers from message content."""
        markers = []
        content_lower = content.lower()

        if "step" in content_lower or "phase" in content_lower:
            markers.append("structured")
        if "if" in content_lower and "then" in content_lower:
            markers.append("conditional")
        if any(word in content_lower for word in ["first", "second", "finally"]):
            markers.append("sequential")
        if "wait" in content_lower or "pending" in content_lower:
            markers.append("async")
        if "error" in content_lower or "failed" in content_lower:
            markers.append("error_handling")

        return markers


# ==================== Global Instance ====================

_bridge: ClaudeAgentBridge | None = None


def get_claude_bridge() -> ClaudeAgentBridge:
    """Get or create the global Claude agent bridge."""
    global _bridge
    if _bridge is None:
        _bridge = ClaudeAgentBridge()
    return _bridge


def reset_claude_bridge() -> ClaudeAgentBridge:
    """Reset and return a new bridge."""
    global _bridge
    _bridge = ClaudeAgentBridge()
    return _bridge

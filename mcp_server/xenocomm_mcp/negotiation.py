"""
XenoComm Negotiation Engine
===========================

Implements the NegotiationProtocol state machine for dynamic agreement
on communication parameters between agents.

States:
- IDLE: No active session
- INITIATING: Sending initial proposal
- AWAITING_RESPONSE: Waiting for response
- COUNTER_RECEIVED: Received counter-proposal
- FINALIZING: Sending final confirmation
- FINALIZED: Agreement reached
- FAILED: Negotiation failed
- CLOSED: Session closed

Enhanced Features (v2.0):
- Parameter compatibility validation
- Auto-optimization based on capabilities
- Timeout handling with configurable policies
- Negotiation history and analytics
- Multi-round counter-proposal support
"""

from dataclasses import dataclass, field
from typing import Any, Callable
from enum import Enum
import uuid
from datetime import datetime, timedelta


class NegotiationState(Enum):
    """States in the negotiation state machine."""
    IDLE = "idle"
    INITIATING = "initiating"
    AWAITING_RESPONSE = "awaiting_response"
    PROPOSAL_RECEIVED = "proposal_received"
    RESPONDING = "responding"
    COUNTER_RECEIVED = "counter_received"
    AWAITING_FINALIZATION = "awaiting_finalization"
    FINALIZING = "finalizing"
    FINALIZED = "finalized"
    FAILED = "failed"
    CLOSED = "closed"
    TIMED_OUT = "timed_out"


class ParamCompatibility(Enum):
    """Compatibility level for parameter values."""
    COMPATIBLE = "compatible"
    INCOMPATIBLE = "incompatible"
    UPGRADEABLE = "upgradeable"  # Can upgrade with fallback
    NEGOTIABLE = "negotiable"  # Requires negotiation


@dataclass
class NegotiableParams:
    """Parameters that can be negotiated between agents."""
    protocol_version: str = "2.0"
    data_format: str = "json"  # json, msgpack, protobuf, vector_float32, vector_int8
    compression: str | None = None  # gzip, lz4, zstd
    error_correction: str = "checksum"  # none, checksum, reed_solomon
    max_message_size: int = 1024 * 1024  # 1MB default
    timeout_ms: int = 30000  # 30 seconds
    encryption: str = "tls"  # none, tls, aes256
    custom_params: dict[str, Any] = field(default_factory=dict)
    # New parameters for enhanced negotiation
    streaming_enabled: bool = False
    batch_size: int = 1
    retry_policy: str = "exponential"  # none, linear, exponential
    max_retries: int = 3
    priority: int = 5  # 1-10, higher is more urgent

    # Supported formats for compatibility checking
    SUPPORTED_DATA_FORMATS = ["json", "msgpack", "protobuf", "vector_float32", "vector_int8", "cbor"]
    SUPPORTED_COMPRESSION = [None, "gzip", "lz4", "zstd", "snappy"]
    SUPPORTED_ENCRYPTION = ["none", "tls", "aes256", "chacha20"]
    SUPPORTED_ERROR_CORRECTION = ["none", "checksum", "crc32", "reed_solomon"]

    def to_dict(self) -> dict[str, Any]:
        return {
            "protocol_version": self.protocol_version,
            "data_format": self.data_format,
            "compression": self.compression,
            "error_correction": self.error_correction,
            "max_message_size": self.max_message_size,
            "timeout_ms": self.timeout_ms,
            "encryption": self.encryption,
            "streaming_enabled": self.streaming_enabled,
            "batch_size": self.batch_size,
            "retry_policy": self.retry_policy,
            "max_retries": self.max_retries,
            "priority": self.priority,
            "custom_params": self.custom_params,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "NegotiableParams":
        return cls(
            protocol_version=data.get("protocol_version", "2.0"),
            data_format=data.get("data_format", "json"),
            compression=data.get("compression"),
            error_correction=data.get("error_correction", "checksum"),
            max_message_size=data.get("max_message_size", 1024 * 1024),
            timeout_ms=data.get("timeout_ms", 30000),
            encryption=data.get("encryption", "tls"),
            streaming_enabled=data.get("streaming_enabled", False),
            batch_size=data.get("batch_size", 1),
            retry_policy=data.get("retry_policy", "exponential"),
            max_retries=data.get("max_retries", 3),
            priority=data.get("priority", 5),
            custom_params=data.get("custom_params", {}),
        )

    def validate(self) -> tuple[bool, list[str]]:
        """Validate that all parameter values are supported."""
        errors = []

        if self.data_format not in self.SUPPORTED_DATA_FORMATS:
            errors.append(f"Unsupported data_format: {self.data_format}")
        if self.compression not in self.SUPPORTED_COMPRESSION:
            errors.append(f"Unsupported compression: {self.compression}")
        if self.encryption not in self.SUPPORTED_ENCRYPTION:
            errors.append(f"Unsupported encryption: {self.encryption}")
        if self.error_correction not in self.SUPPORTED_ERROR_CORRECTION:
            errors.append(f"Unsupported error_correction: {self.error_correction}")
        if self.max_message_size < 1024 or self.max_message_size > 100 * 1024 * 1024:
            errors.append(f"max_message_size must be between 1KB and 100MB")
        if self.timeout_ms < 1000 or self.timeout_ms > 600000:
            errors.append(f"timeout_ms must be between 1s and 10min")
        if self.priority < 1 or self.priority > 10:
            errors.append(f"priority must be between 1 and 10")

        return len(errors) == 0, errors

    def check_compatibility(self, other: "NegotiableParams") -> dict[str, ParamCompatibility]:
        """Check compatibility with another set of parameters."""
        results = {}

        # Data format compatibility
        if self.data_format == other.data_format:
            results["data_format"] = ParamCompatibility.COMPATIBLE
        elif self.data_format == "json" or other.data_format == "json":
            results["data_format"] = ParamCompatibility.UPGRADEABLE  # JSON is universal fallback
        else:
            results["data_format"] = ParamCompatibility.NEGOTIABLE

        # Compression compatibility
        if self.compression == other.compression:
            results["compression"] = ParamCompatibility.COMPATIBLE
        elif self.compression is None or other.compression is None:
            results["compression"] = ParamCompatibility.UPGRADEABLE
        else:
            results["compression"] = ParamCompatibility.NEGOTIABLE

        # Encryption - stricter compatibility
        if self.encryption == other.encryption:
            results["encryption"] = ParamCompatibility.COMPATIBLE
        elif self.encryption == "none" or other.encryption == "none":
            results["encryption"] = ParamCompatibility.INCOMPATIBLE  # Can't downgrade security
        else:
            results["encryption"] = ParamCompatibility.NEGOTIABLE

        # Message size - take minimum
        if self.max_message_size == other.max_message_size:
            results["max_message_size"] = ParamCompatibility.COMPATIBLE
        else:
            results["max_message_size"] = ParamCompatibility.NEGOTIABLE

        return results

    def merge_with(self, other: "NegotiableParams", prefer_self: bool = True) -> "NegotiableParams":
        """Create a merged parameter set that's compatible with both."""
        return NegotiableParams(
            protocol_version=max(self.protocol_version, other.protocol_version),
            data_format=self.data_format if prefer_self else other.data_format,
            compression=self.compression if self.compression and other.compression else None,
            error_correction=self.error_correction if prefer_self else other.error_correction,
            max_message_size=min(self.max_message_size, other.max_message_size),
            timeout_ms=min(self.timeout_ms, other.timeout_ms),
            encryption=max(self.encryption, other.encryption, key=lambda x: self.SUPPORTED_ENCRYPTION.index(x) if x in self.SUPPORTED_ENCRYPTION else 0),
            streaming_enabled=self.streaming_enabled and other.streaming_enabled,
            batch_size=min(self.batch_size, other.batch_size),
            retry_policy=self.retry_policy if prefer_self else other.retry_policy,
            max_retries=max(self.max_retries, other.max_retries),
            priority=max(self.priority, other.priority),
            custom_params={**other.custom_params, **self.custom_params} if prefer_self else {**self.custom_params, **other.custom_params},
        )


@dataclass
class NegotiationRound:
    """Represents a single round of negotiation."""
    round_number: int
    proposer_id: str
    params: NegotiableParams
    response: str | None = None  # accept, counter, reject
    timestamp: datetime = field(default_factory=datetime.utcnow)


class TimeoutPolicy(Enum):
    """How to handle session timeouts."""
    FAIL = "fail"  # Mark session as failed
    EXTEND = "extend"  # Auto-extend timeout
    NOTIFY = "notify"  # Notify parties, keep session
    AUTO_ACCEPT = "auto_accept"  # Accept last proposal


@dataclass
class NegotiationConfig:
    """Configuration for the negotiation engine."""
    default_timeout_ms: int = 30000
    max_rounds: int = 10
    timeout_policy: TimeoutPolicy = TimeoutPolicy.FAIL
    auto_extend_count: int = 2  # How many times to auto-extend
    require_validation: bool = True  # Validate params before accepting
    enable_auto_optimization: bool = True  # Auto-suggest optimal params
    track_history: bool = True  # Track negotiation rounds


@dataclass
class NegotiationSession:
    """Represents an active negotiation session."""
    session_id: str
    initiator_id: str
    responder_id: str
    state: NegotiationState
    proposed_params: NegotiableParams
    counter_params: NegotiableParams | None = None
    final_params: NegotiableParams | None = None
    created_at: datetime = field(default_factory=datetime.utcnow)
    updated_at: datetime = field(default_factory=datetime.utcnow)
    expires_at: datetime | None = None
    failure_reason: str | None = None
    # Enhanced tracking
    rounds: list["NegotiationRound"] = field(default_factory=list)
    extend_count: int = 0
    alignment_score: float | None = None  # From alignment engine
    metadata: dict[str, Any] = field(default_factory=dict)

    def is_expired(self) -> bool:
        """Check if the session has timed out."""
        if self.expires_at is None:
            return False
        return datetime.utcnow() > self.expires_at

    def time_remaining_ms(self) -> int | None:
        """Get milliseconds until timeout."""
        if self.expires_at is None:
            return None
        delta = self.expires_at - datetime.utcnow()
        return max(0, int(delta.total_seconds() * 1000))

    def add_round(self, proposer_id: str, params: NegotiableParams, response: str | None = None):
        """Add a negotiation round to history."""
        self.rounds.append(NegotiationRound(
            round_number=len(self.rounds) + 1,
            proposer_id=proposer_id,
            params=params,
            response=response,
        ))

    def to_dict(self) -> dict[str, Any]:
        return {
            "session_id": self.session_id,
            "initiator_id": self.initiator_id,
            "responder_id": self.responder_id,
            "state": self.state.value,
            "proposed_params": self.proposed_params.to_dict(),
            "counter_params": self.counter_params.to_dict() if self.counter_params else None,
            "final_params": self.final_params.to_dict() if self.final_params else None,
            "created_at": self.created_at.isoformat(),
            "updated_at": self.updated_at.isoformat(),
            "expires_at": self.expires_at.isoformat() if self.expires_at else None,
            "failure_reason": self.failure_reason,
            "rounds": [
                {
                    "round_number": r.round_number,
                    "proposer_id": r.proposer_id,
                    "params": r.params.to_dict(),
                    "response": r.response,
                    "timestamp": r.timestamp.isoformat(),
                }
                for r in self.rounds
            ],
            "extend_count": self.extend_count,
            "alignment_score": self.alignment_score,
            "metadata": self.metadata,
        }


@dataclass
class NegotiationAnalytics:
    """Analytics for negotiation performance."""
    total_sessions: int = 0
    successful_sessions: int = 0
    failed_sessions: int = 0
    timed_out_sessions: int = 0
    average_rounds: float = 0.0
    average_duration_ms: float = 0.0
    most_contested_params: list[str] = field(default_factory=list)
    success_rate: float = 0.0

    def to_dict(self) -> dict[str, Any]:
        return {
            "total_sessions": self.total_sessions,
            "successful_sessions": self.successful_sessions,
            "failed_sessions": self.failed_sessions,
            "timed_out_sessions": self.timed_out_sessions,
            "average_rounds": round(self.average_rounds, 2),
            "average_duration_ms": round(self.average_duration_ms, 2),
            "most_contested_params": self.most_contested_params,
            "success_rate": round(self.success_rate * 100, 1),
        }


class NegotiationEngine:
    """
    Engine for protocol negotiation between AI agents.

    Implements a state machine for agents to dynamically agree upon
    communication parameters.

    Enhanced Features:
    - Timeout handling with configurable policies
    - Multi-round counter-proposal support
    - Negotiation history and analytics
    - Auto-optimization based on capabilities
    - Alignment integration
    """

    def __init__(self, config: NegotiationConfig | None = None):
        self.config = config or NegotiationConfig()
        self.sessions: dict[str, NegotiationSession] = {}
        self._completed_sessions: list[NegotiationSession] = []  # For analytics
        self._param_contests: dict[str, int] = {}  # Track contested params

    def initiate_session(
        self,
        initiator_id: str,
        responder_id: str,
        proposed_params: NegotiableParams | dict[str, Any],
        timeout_ms: int | None = None,
        alignment_score: float | None = None,
        metadata: dict[str, Any] | None = None,
    ) -> NegotiationSession:
        """
        Initiate a negotiation session with another agent.

        Args:
            initiator_id: ID of the agent starting negotiation
            responder_id: ID of the agent to negotiate with
            proposed_params: Initial parameters to propose
            timeout_ms: Custom timeout (uses config default if None)
            alignment_score: Pre-computed alignment score from AlignmentEngine
            metadata: Additional session metadata

        Returns the created session with state AWAITING_RESPONSE.
        """
        if isinstance(proposed_params, dict):
            proposed_params = NegotiableParams.from_dict(proposed_params)

        # Validate parameters if configured
        if self.config.require_validation:
            is_valid, errors = proposed_params.validate()
            if not is_valid:
                raise ValueError(f"Invalid parameters: {', '.join(errors)}")

        session_id = str(uuid.uuid4())
        timeout = timeout_ms or self.config.default_timeout_ms
        expires_at = datetime.utcnow() + timedelta(milliseconds=timeout)

        session = NegotiationSession(
            session_id=session_id,
            initiator_id=initiator_id,
            responder_id=responder_id,
            state=NegotiationState.AWAITING_RESPONSE,
            proposed_params=proposed_params,
            expires_at=expires_at,
            alignment_score=alignment_score,
            metadata=metadata or {},
        )

        # Track initial proposal in history
        if self.config.track_history:
            session.add_round(initiator_id, proposed_params)

        self.sessions[session_id] = session
        return session

    def receive_proposal(
        self,
        session_id: str,
        responder_id: str,
    ) -> NegotiationSession:
        """
        Called by responder when they receive a proposal.

        Transitions session to PROPOSAL_RECEIVED state.
        """
        session = self._get_session(session_id)

        if session.responder_id != responder_id:
            raise ValueError("Agent is not the responder for this session")

        session.state = NegotiationState.PROPOSAL_RECEIVED
        session.updated_at = datetime.utcnow()

        return session

    def respond_accept(
        self,
        session_id: str,
        responder_id: str,
    ) -> NegotiationSession:
        """
        Accept the proposed parameters.
        """
        session = self._get_session(session_id)

        if session.responder_id != responder_id:
            raise ValueError("Agent is not the responder for this session")

        if session.state not in (NegotiationState.PROPOSAL_RECEIVED, NegotiationState.COUNTER_RECEIVED):
            raise ValueError(f"Cannot accept from state {session.state}")

        session.state = NegotiationState.AWAITING_FINALIZATION
        session.updated_at = datetime.utcnow()

        return session

    def respond_counter(
        self,
        session_id: str,
        responder_id: str,
        counter_params: NegotiableParams | dict[str, Any],
    ) -> NegotiationSession:
        """
        Respond with a counter-proposal.
        """
        if isinstance(counter_params, dict):
            counter_params = NegotiableParams.from_dict(counter_params)

        session = self._get_session(session_id)

        if session.responder_id != responder_id:
            raise ValueError("Agent is not the responder for this session")

        if session.state != NegotiationState.PROPOSAL_RECEIVED:
            raise ValueError(f"Cannot counter from state {session.state}")

        session.counter_params = counter_params
        session.state = NegotiationState.AWAITING_FINALIZATION
        session.updated_at = datetime.utcnow()

        return session

    def respond_reject(
        self,
        session_id: str,
        responder_id: str,
        reason: str | None = None,
    ) -> NegotiationSession:
        """
        Reject the proposal.
        """
        session = self._get_session(session_id)

        if session.responder_id != responder_id:
            raise ValueError("Agent is not the responder for this session")

        session.state = NegotiationState.FAILED
        session.failure_reason = reason or "Proposal rejected"
        session.updated_at = datetime.utcnow()

        return session

    def accept_counter(
        self,
        session_id: str,
        initiator_id: str,
    ) -> NegotiationSession:
        """
        Initiator accepts the counter-proposal.
        """
        session = self._get_session(session_id)

        if session.initiator_id != initiator_id:
            raise ValueError("Agent is not the initiator for this session")

        if session.state != NegotiationState.AWAITING_FINALIZATION:
            raise ValueError(f"Cannot accept counter from state {session.state}")

        if session.counter_params is None:
            raise ValueError("No counter-proposal to accept")

        session.state = NegotiationState.FINALIZING
        session.updated_at = datetime.utcnow()

        return session

    def finalize_session(
        self,
        session_id: str,
        initiator_id: str,
    ) -> NegotiationSession:
        """
        Finalize the negotiation with agreed parameters.
        """
        session = self._get_session(session_id)

        if session.initiator_id != initiator_id:
            raise ValueError("Agent is not the initiator for this session")

        if session.state not in (NegotiationState.AWAITING_FINALIZATION, NegotiationState.FINALIZING):
            raise ValueError(f"Cannot finalize from state {session.state}")

        # Use counter params if available, otherwise original proposal
        session.final_params = session.counter_params or session.proposed_params
        session.state = NegotiationState.FINALIZED
        session.updated_at = datetime.utcnow()

        return session

    def close_session(
        self,
        session_id: str,
        agent_id: str,
        reason: str | None = None,
    ) -> NegotiationSession:
        """
        Close a session (can be called by either party).
        """
        session = self._get_session(session_id)

        if agent_id not in (session.initiator_id, session.responder_id):
            raise ValueError("Agent is not part of this session")

        session.state = NegotiationState.CLOSED
        session.failure_reason = reason
        session.updated_at = datetime.utcnow()

        return session

    def get_session_status(self, session_id: str) -> NegotiationSession:
        """Get the current status of a negotiation session."""
        return self._get_session(session_id)

    def list_sessions(
        self,
        agent_id: str | None = None,
        state: NegotiationState | None = None,
    ) -> list[NegotiationSession]:
        """List negotiation sessions, optionally filtered."""
        sessions = list(self.sessions.values())

        if agent_id:
            sessions = [
                s for s in sessions
                if s.initiator_id == agent_id or s.responder_id == agent_id
            ]

        if state:
            sessions = [s for s in sessions if s.state == state]

        return sessions

    def _get_session(self, session_id: str) -> NegotiationSession:
        """Get a session by ID or raise an error."""
        if session_id not in self.sessions:
            raise ValueError(f"Session {session_id} not found")
        return self.sessions[session_id]

    # ==================== Timeout Handling ====================

    def check_timeout(self, session_id: str) -> tuple[bool, NegotiationSession]:
        """
        Check if a session has timed out and handle according to policy.

        Returns (timed_out, session) tuple.
        """
        session = self._get_session(session_id)

        if not session.is_expired():
            return False, session

        # Session is expired - handle according to policy
        if self.config.timeout_policy == TimeoutPolicy.FAIL:
            session.state = NegotiationState.TIMED_OUT
            session.failure_reason = "Session timed out"
            self._archive_session(session)
            return True, session

        elif self.config.timeout_policy == TimeoutPolicy.EXTEND:
            if session.extend_count < self.config.auto_extend_count:
                session.extend_count += 1
                session.expires_at = datetime.utcnow() + timedelta(
                    milliseconds=self.config.default_timeout_ms
                )
                session.updated_at = datetime.utcnow()
                return False, session
            else:
                session.state = NegotiationState.TIMED_OUT
                session.failure_reason = f"Timed out after {session.extend_count} extensions"
                self._archive_session(session)
                return True, session

        elif self.config.timeout_policy == TimeoutPolicy.AUTO_ACCEPT:
            # Accept the last proposal automatically
            if session.counter_params:
                session.final_params = session.counter_params
            else:
                session.final_params = session.proposed_params
            session.state = NegotiationState.FINALIZED
            session.metadata["auto_accepted"] = True
            self._archive_session(session)
            return True, session

        else:  # NOTIFY - just flag it
            session.metadata["timeout_notified"] = True
            return True, session

    def extend_timeout(
        self,
        session_id: str,
        agent_id: str,
        additional_ms: int | None = None,
    ) -> NegotiationSession:
        """Manually extend a session's timeout."""
        session = self._get_session(session_id)

        if agent_id not in (session.initiator_id, session.responder_id):
            raise ValueError("Agent is not part of this session")

        extension = additional_ms or self.config.default_timeout_ms
        if session.expires_at:
            session.expires_at = session.expires_at + timedelta(milliseconds=extension)
        else:
            session.expires_at = datetime.utcnow() + timedelta(milliseconds=extension)

        session.extend_count += 1
        session.updated_at = datetime.utcnow()

        return session

    def check_all_timeouts(self) -> list[NegotiationSession]:
        """Check all active sessions for timeouts. Returns list of timed-out sessions."""
        timed_out = []
        for session_id in list(self.sessions.keys()):
            expired, session = self.check_timeout(session_id)
            if expired:
                timed_out.append(session)
        return timed_out

    # ==================== Auto-Optimization ====================

    def suggest_optimal_params(
        self,
        initiator_capabilities: dict[str, Any],
        responder_capabilities: dict[str, Any],
        priority: str = "performance",  # performance, compatibility, security
    ) -> NegotiableParams:
        """
        Suggest optimal negotiation parameters based on both parties' capabilities.

        Args:
            initiator_capabilities: What the initiator supports
            responder_capabilities: What the responder supports
            priority: Optimization priority (performance, compatibility, security)

        Returns optimized NegotiableParams.
        """
        # Find common supported values
        def find_common(key: str, supported_list: list, default: Any) -> Any:
            init_supported = initiator_capabilities.get(key, supported_list)
            resp_supported = responder_capabilities.get(key, supported_list)
            common = [v for v in init_supported if v in resp_supported]
            return common[0] if common else default

        # Priority-based selection
        if priority == "performance":
            # Prefer faster formats and compression
            data_format = find_common(
                "data_formats",
                ["msgpack", "protobuf", "cbor", "json"],
                "msgpack"
            )
            compression = find_common(
                "compression",
                ["lz4", "snappy", "zstd", "gzip", None],
                "lz4"
            )
            encryption = find_common(
                "encryption",
                ["tls", "aes256", "chacha20"],
                "tls"
            )
        elif priority == "security":
            # Prefer strongest security
            data_format = find_common(
                "data_formats",
                ["json", "protobuf", "msgpack"],
                "json"
            )
            compression = find_common(
                "compression",
                ["zstd", "gzip", "lz4", None],
                "zstd"
            )
            encryption = find_common(
                "encryption",
                ["chacha20", "aes256", "tls"],
                "aes256"
            )
        else:  # compatibility
            # Prefer most widely supported
            data_format = "json"
            compression = None
            encryption = "tls"

        # Message size - use minimum of both
        init_max = initiator_capabilities.get("max_message_size", 1024 * 1024)
        resp_max = responder_capabilities.get("max_message_size", 1024 * 1024)

        return NegotiableParams(
            data_format=data_format,
            compression=compression,
            encryption=encryption,
            max_message_size=min(init_max, resp_max),
            streaming_enabled=(
                initiator_capabilities.get("streaming", False) and
                responder_capabilities.get("streaming", False)
            ),
            batch_size=min(
                initiator_capabilities.get("max_batch_size", 100),
                responder_capabilities.get("max_batch_size", 100),
            ),
        )

    def auto_resolve_conflicts(
        self,
        session_id: str,
        prefer_initiator: bool = True,
    ) -> NegotiableParams:
        """
        Automatically resolve parameter conflicts in a negotiation.

        Returns merged parameters that satisfy both parties.
        """
        session = self._get_session(session_id)

        if session.counter_params is None:
            return session.proposed_params

        proposed = session.proposed_params
        counter = session.counter_params

        # Check compatibility
        compat = proposed.check_compatibility(counter)

        # Track contested parameters for analytics
        for param, status in compat.items():
            if status in (ParamCompatibility.NEGOTIABLE, ParamCompatibility.INCOMPATIBLE):
                self._param_contests[param] = self._param_contests.get(param, 0) + 1

        # Merge based on compatibility
        return proposed.merge_with(counter, prefer_self=prefer_initiator)

    # ==================== Multi-Round Support ====================

    def submit_counter_proposal(
        self,
        session_id: str,
        agent_id: str,
        counter_params: NegotiableParams | dict[str, Any],
    ) -> NegotiationSession:
        """
        Submit a counter-proposal (can be from either party in multi-round).

        Supports more than 2 rounds of negotiation.
        """
        if isinstance(counter_params, dict):
            counter_params = NegotiableParams.from_dict(counter_params)

        session = self._get_session(session_id)

        if agent_id not in (session.initiator_id, session.responder_id):
            raise ValueError("Agent is not part of this session")

        # Check max rounds
        if len(session.rounds) >= self.config.max_rounds:
            raise ValueError(f"Maximum negotiation rounds ({self.config.max_rounds}) reached")

        # Validate if configured
        if self.config.require_validation:
            is_valid, errors = counter_params.validate()
            if not is_valid:
                raise ValueError(f"Invalid counter parameters: {', '.join(errors)}")

        # Update session
        session.counter_params = counter_params
        session.state = NegotiationState.COUNTER_RECEIVED
        session.updated_at = datetime.utcnow()

        # Track in history
        if self.config.track_history:
            session.add_round(agent_id, counter_params, "counter")

        return session

    # ==================== Analytics ====================

    def _archive_session(self, session: NegotiationSession):
        """Archive a completed session for analytics."""
        self._completed_sessions.append(session)
        if session.session_id in self.sessions:
            del self.sessions[session.session_id]

    def get_analytics(self, agent_id: str | None = None) -> NegotiationAnalytics:
        """
        Get negotiation analytics.

        Args:
            agent_id: Filter to sessions involving this agent (optional)
        """
        sessions = self._completed_sessions
        if agent_id:
            sessions = [
                s for s in sessions
                if s.initiator_id == agent_id or s.responder_id == agent_id
            ]

        if not sessions:
            return NegotiationAnalytics()

        successful = [s for s in sessions if s.state == NegotiationState.FINALIZED]
        failed = [s for s in sessions if s.state == NegotiationState.FAILED]
        timed_out = [s for s in sessions if s.state == NegotiationState.TIMED_OUT]

        # Calculate averages
        total_rounds = sum(len(s.rounds) for s in sessions)
        avg_rounds = total_rounds / len(sessions) if sessions else 0

        total_duration = sum(
            (s.updated_at - s.created_at).total_seconds() * 1000
            for s in sessions
        )
        avg_duration = total_duration / len(sessions) if sessions else 0

        # Most contested parameters
        sorted_contests = sorted(
            self._param_contests.items(),
            key=lambda x: x[1],
            reverse=True
        )
        most_contested = [p[0] for p in sorted_contests[:5]]

        success_rate = len(successful) / len(sessions) if sessions else 0

        return NegotiationAnalytics(
            total_sessions=len(sessions),
            successful_sessions=len(successful),
            failed_sessions=len(failed),
            timed_out_sessions=len(timed_out),
            average_rounds=avg_rounds,
            average_duration_ms=avg_duration,
            most_contested_params=most_contested,
            success_rate=success_rate,
        )

    def get_session_history(self, session_id: str) -> list[dict[str, Any]]:
        """Get the full negotiation history for a session."""
        session = self._get_session(session_id)
        return [
            {
                "round": r.round_number,
                "proposer": r.proposer_id,
                "params": r.params.to_dict(),
                "response": r.response,
                "timestamp": r.timestamp.isoformat(),
            }
            for r in session.rounds
        ]

    # ==================== Alignment Integration ====================

    def set_alignment_score(
        self,
        session_id: str,
        score: float,
        alignment_details: dict[str, Any] | None = None,
    ) -> NegotiationSession:
        """
        Set the alignment score for a session (from AlignmentEngine).

        This can influence negotiation strategy and auto-resolution.
        """
        session = self._get_session(session_id)
        session.alignment_score = score
        if alignment_details:
            session.metadata["alignment_details"] = alignment_details
        session.updated_at = datetime.utcnow()
        return session

    def should_auto_accept(self, session_id: str, threshold: float = 0.8) -> bool:
        """
        Determine if a proposal should be auto-accepted based on alignment.

        Returns True if alignment score exceeds threshold.
        """
        session = self._get_session(session_id)
        if session.alignment_score is None:
            return False
        return session.alignment_score >= threshold

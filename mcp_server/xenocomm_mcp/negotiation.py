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
"""

from dataclasses import dataclass, field
from typing import Any
from enum import Enum
import uuid
from datetime import datetime


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


@dataclass
class NegotiableParams:
    """Parameters that can be negotiated between agents."""
    protocol_version: str = "1.0"
    data_format: str = "json"  # json, msgpack, protobuf, vector_float32, vector_int8
    compression: str | None = None  # gzip, lz4, zstd
    error_correction: str = "checksum"  # none, checksum, reed_solomon
    max_message_size: int = 1024 * 1024  # 1MB default
    timeout_ms: int = 30000  # 30 seconds
    encryption: str = "tls"  # none, tls, aes256
    custom_params: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "protocol_version": self.protocol_version,
            "data_format": self.data_format,
            "compression": self.compression,
            "error_correction": self.error_correction,
            "max_message_size": self.max_message_size,
            "timeout_ms": self.timeout_ms,
            "encryption": self.encryption,
            "custom_params": self.custom_params,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "NegotiableParams":
        return cls(
            protocol_version=data.get("protocol_version", "1.0"),
            data_format=data.get("data_format", "json"),
            compression=data.get("compression"),
            error_correction=data.get("error_correction", "checksum"),
            max_message_size=data.get("max_message_size", 1024 * 1024),
            timeout_ms=data.get("timeout_ms", 30000),
            encryption=data.get("encryption", "tls"),
            custom_params=data.get("custom_params", {}),
        )


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
    failure_reason: str | None = None

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
            "failure_reason": self.failure_reason,
        }


class NegotiationEngine:
    """
    Engine for protocol negotiation between AI agents.

    Implements a state machine for agents to dynamically agree upon
    communication parameters.
    """

    def __init__(self):
        self.sessions: dict[str, NegotiationSession] = {}

    def initiate_session(
        self,
        initiator_id: str,
        responder_id: str,
        proposed_params: NegotiableParams | dict[str, Any],
    ) -> NegotiationSession:
        """
        Initiate a negotiation session with another agent.

        Returns the created session with state AWAITING_RESPONSE.
        """
        if isinstance(proposed_params, dict):
            proposed_params = NegotiableParams.from_dict(proposed_params)

        session_id = str(uuid.uuid4())
        session = NegotiationSession(
            session_id=session_id,
            initiator_id=initiator_id,
            responder_id=responder_id,
            state=NegotiationState.AWAITING_RESPONSE,
            proposed_params=proposed_params,
        )

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

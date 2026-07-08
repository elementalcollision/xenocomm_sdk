"""Variant governance — the vote + human-gate layer in front of the emergence pipeline.

Before this module, a proposed protocol variant could be pushed straight to
canary with no vote and no human sign-off (the ``votes`` field on language
constructs was initialized and never read — the "dead votes path"), and the
"promotion" of a learned pattern only emitted a log line, never reaching the
emergence engine's canary/rollback machinery.

``VariantGovernance`` closes that gap. A variant must earn a quorum of agent
votes above an approval ratio, then pass an explicit **human gate**, before it
is wired into ``EmergenceEngine.start_testing`` → ``start_canary``. Every
transition is emitted as a typed ``EMERGENCE`` flow event, so the whole
proposed → voted → approved → promoted lifecycle is an auditable transcript.

The engine methods themselves stay ungoverned (they are the mechanism, and are
exercised directly by tests); governance is enforced at the agent-facing tool
surface in ``server.py``.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from typing import Any


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


class GovernanceStatus(Enum):
    """Lifecycle of a variant under governance."""

    VOTING = "voting"              # accepting votes
    VOTE_PASSED = "vote_passed"    # quorum + ratio met, awaiting the human gate
    VOTE_FAILED = "vote_failed"    # voting closed without sufficient support
    APPROVED = "approved"          # human approved; promotion attempted
    REJECTED = "rejected"          # human rejected at the gate
    PROMOTED = "promoted"          # wired into the emergence canary pipeline


VALID_VOTES = ("for", "against")


@dataclass
class Vote:
    agent_id: str
    vote: str          # "for" | "against"
    rationale: str
    at: str

    def to_dict(self) -> dict[str, Any]:
        return {"agent_id": self.agent_id, "vote": self.vote,
                "rationale": self.rationale, "at": self.at}


@dataclass
class GovernanceConfig:
    quorum: int = 3               # minimum total votes before a decision can pass
    approval_ratio: float = 0.66  # minimum for / (for + against)
    require_human_approval: bool = True  # the human gate

    def __post_init__(self) -> None:
        if self.quorum < 1:
            raise ValueError("quorum must be >= 1")
        if not 0.0 < self.approval_ratio <= 1.0:
            raise ValueError("approval_ratio must be in (0.0, 1.0]")


@dataclass
class GovernanceRecord:
    variant_id: str
    description: str
    status: GovernanceStatus = GovernanceStatus.VOTING
    votes: dict[str, Vote] = field(default_factory=dict)  # agent_id -> Vote (deduped)
    human_decision: dict[str, Any] | None = None
    promotion_error: str | None = None
    created_at: str = field(default_factory=_now)
    updated_at: str = field(default_factory=_now)
    audit: list[dict[str, Any]] = field(default_factory=list)

    def tally(self, config: GovernanceConfig) -> dict[str, Any]:
        for_ = sum(1 for v in self.votes.values() if v.vote == "for")
        against = sum(1 for v in self.votes.values() if v.vote == "against")
        total = for_ + against
        ratio = (for_ / total) if total else 0.0
        quorum_met = total >= config.quorum
        return {
            "for": for_, "against": against, "total": total,
            "ratio": round(ratio, 4),
            "quorum": config.quorum, "quorum_met": quorum_met,
            "approval_ratio": config.approval_ratio,
            "passed": quorum_met and ratio >= config.approval_ratio,
        }

    def to_dict(self, config: GovernanceConfig) -> dict[str, Any]:
        return {
            "variant_id": self.variant_id,
            "description": self.description,
            "status": self.status.value,
            "votes": [v.to_dict() for v in self.votes.values()],
            "tally": self.tally(config),
            "human_decision": self.human_decision,
            "promotion_error": self.promotion_error,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
            "audit": list(self.audit),
        }


class GovernanceError(Exception):
    """Raised for invalid governance transitions (unknown variant, wrong state)."""


class VariantGovernance:
    """Vote + human-gate governance in front of the emergence canary pipeline.

    Args:
        emergence_engine: the ``EmergenceEngine`` (or Instrumented subclass) whose
            ``start_testing`` / ``start_canary`` are driven on approval.
        observation_manager: optional; if it exposes ``emergence_sensor.emit`` each
            transition is emitted as an auditable EMERGENCE flow event.
        config: quorum / approval-ratio / human-gate policy.
    """

    def __init__(self, emergence_engine: Any, observation_manager: Any = None,
                 config: GovernanceConfig | None = None) -> None:
        self._engine = emergence_engine
        self._obs = observation_manager
        self.config = config or GovernanceConfig()
        self.records: dict[str, GovernanceRecord] = {}

    # -- introspection -------------------------------------------------------
    def get(self, variant_id: str) -> GovernanceRecord:
        rec = self.records.get(variant_id)
        if rec is None:
            raise GovernanceError(f"No governance record for variant {variant_id!r}")
        return rec

    def has(self, variant_id: str) -> bool:
        return variant_id in self.records

    def is_promotable(self, variant_id: str) -> bool:
        """True iff the variant has cleared governance (human-approved / promoted)."""
        rec = self.records.get(variant_id)
        return rec is not None and rec.status in (
            GovernanceStatus.APPROVED, GovernanceStatus.PROMOTED)

    def list_awaiting_approval(self) -> list[dict[str, Any]]:
        return [r.to_dict(self.config) for r in self.records.values()
                if r.status is GovernanceStatus.VOTE_PASSED]

    def list_all(self) -> list[dict[str, Any]]:
        return [r.to_dict(self.config) for r in self.records.values()]

    # -- lifecycle -----------------------------------------------------------
    def submit(self, variant_id: str, description: str) -> GovernanceRecord:
        """Place a freshly-proposed variant under governance (status VOTING)."""
        if variant_id in self.records:
            return self.records[variant_id]
        rec = GovernanceRecord(variant_id=variant_id, description=description)
        self.records[variant_id] = rec
        self._audit(rec, "governance_submitted",
                    f"Variant {variant_id} submitted for governance")
        return rec

    def cast_vote(self, variant_id: str, agent_id: str, vote: str,
                  rationale: str = "") -> dict[str, Any]:
        """Record an agent's vote (one per agent; re-voting overwrites).

        When the quorum and approval ratio are met, the variant advances to
        VOTE_PASSED (awaiting the human gate) — or, if human approval is
        disabled, straight to APPROVED and promotion.
        """
        rec = self.get(variant_id)
        if rec.status is not GovernanceStatus.VOTING:
            raise GovernanceError(
                f"Voting is closed for {variant_id} (status: {rec.status.value})")
        vote = vote.lower().strip()
        if vote not in VALID_VOTES:
            raise GovernanceError(f"vote must be one of {VALID_VOTES}, got {vote!r}")
        if not agent_id:
            raise GovernanceError("agent_id is required")

        rec.votes[agent_id] = Vote(agent_id=agent_id, vote=vote,
                                   rationale=rationale, at=_now())
        rec.updated_at = _now()
        self._audit(rec, "governance_vote_cast",
                    f"{agent_id} voted {vote} on {variant_id}",
                    agent_id=agent_id, vote=vote)

        tally = rec.tally(self.config)
        if tally["passed"]:
            if self.config.require_human_approval:
                rec.status = GovernanceStatus.VOTE_PASSED
                self._audit(rec, "governance_vote_passed",
                            f"{variant_id} passed the vote; awaiting human approval",
                            **tally)
            else:
                rec.status = GovernanceStatus.APPROVED
                self._audit(rec, "governance_vote_passed",
                            f"{variant_id} passed the vote (human gate disabled)",
                            **tally)
                self._promote(rec)
        return {"variant_id": variant_id, "status": rec.status.value, "tally": tally}

    def close_voting(self, variant_id: str, actor: str, reason: str = "") -> dict[str, Any]:
        """Explicitly close an open vote that has not passed (→ VOTE_FAILED)."""
        rec = self.get(variant_id)
        if rec.status is not GovernanceStatus.VOTING:
            raise GovernanceError(
                f"Can only close an open vote; {variant_id} is {rec.status.value}")
        rec.status = GovernanceStatus.VOTE_FAILED
        rec.updated_at = _now()
        self._audit(rec, "governance_vote_failed",
                    f"Voting closed for {variant_id} by {actor}: {reason}",
                    severity="WARNING", actor=actor)
        return {"variant_id": variant_id, "status": rec.status.value}

    def human_approve(self, variant_id: str, approver: str, decision: str,
                      reason: str = "") -> dict[str, Any]:
        """The human gate. Valid only once a variant has VOTE_PASSED.

        ``decision="approve"`` → APPROVED, then the variant is wired into the
        emergence canary pipeline. ``decision="reject"`` → REJECTED (no promotion).
        """
        rec = self.get(variant_id)
        if rec.status is not GovernanceStatus.VOTE_PASSED:
            raise GovernanceError(
                f"Human approval requires VOTE_PASSED; {variant_id} is {rec.status.value}")
        decision = decision.lower().strip()
        if decision not in ("approve", "reject"):
            raise GovernanceError(f"decision must be 'approve' or 'reject', got {decision!r}")
        if not approver:
            raise GovernanceError("approver is required")

        rec.human_decision = {"approver": approver, "decision": decision,
                              "reason": reason, "at": _now()}
        rec.updated_at = _now()

        if decision == "reject":
            rec.status = GovernanceStatus.REJECTED
            self._audit(rec, "governance_human_rejected",
                        f"{approver} rejected {variant_id}: {reason}",
                        severity="WARNING", approver=approver)
            return {"variant_id": variant_id, "status": rec.status.value}

        rec.status = GovernanceStatus.APPROVED
        self._audit(rec, "governance_human_approved",
                    f"{approver} approved {variant_id}: {reason}", approver=approver)
        self._promote(rec)
        return {"variant_id": variant_id, "status": rec.status.value,
                "promotion_error": rec.promotion_error}

    # -- internals -----------------------------------------------------------
    def _promote(self, rec: GovernanceRecord) -> None:
        """Wire an approved variant into the emergence canary pipeline."""
        try:
            self._engine.start_testing(rec.variant_id)
            variant = self._engine.start_canary(rec.variant_id)
            pct = getattr(variant, "canary_percentage", None)
            rec.status = GovernanceStatus.PROMOTED
            rec.updated_at = _now()
            self._audit(rec, "governance_promoted_to_canary",
                        f"{rec.variant_id} promoted to canary at {pct}",
                        canary_percentage=pct)
        except Exception as exc:  # promotion is best-effort; surface, don't crash the gate
            rec.promotion_error = f"{type(exc).__name__}: {exc}"
            rec.updated_at = _now()
            self._audit(rec, "governance_promotion_failed",
                        f"Promotion of {rec.variant_id} failed: {rec.promotion_error}",
                        severity="ERROR")

    def _audit(self, rec: GovernanceRecord, event_name: str, summary: str,
               severity: str = "INFO", **metrics: Any) -> None:
        entry = {"event": event_name, "summary": summary, "at": _now(),
                 "status": rec.status.value, "metrics": metrics}
        rec.audit.append(entry)
        sensor = getattr(self._obs, "emergence_sensor", None)
        if sensor is None:
            return
        try:
            from .observation import EventSeverity
            sev = getattr(EventSeverity, severity, EventSeverity.INFO)
        except Exception:
            sev = None
        emit_kwargs = {"metrics": {"variant_id": rec.variant_id, **metrics},
                       "tags": ["emergence", "governance"]}
        if sev is not None:
            emit_kwargs["severity"] = sev
        try:
            sensor.emit(event_name, summary, **emit_kwargs)
        except Exception:
            pass  # audit is observational; never let it break governance

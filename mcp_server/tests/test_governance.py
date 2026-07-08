"""Tests for M1 P1 — variant governance (votes + human gate + wiring + audit).

Two levels:
- Unit: ``VariantGovernance`` against a real ``EmergenceEngine`` + a fake obs
  sensor, asserting the vote/quorum/human-gate state machine and that an
  approved variant is actually driven into CANARY on the engine.
- Server integration: the new MCP tools (``cast_variant_vote``,
  ``approve_variant``, …) drive the same flow end to end, and the low-level
  ``start_canary_deployment`` tool refuses to bypass the gate.
"""

import uuid

import pytest

from xenocomm_mcp.emergence import EmergenceEngine, VariantStatus
from xenocomm_mcp.governance import (
    VariantGovernance, GovernanceConfig, GovernanceStatus, GovernanceError,
)


def _uid(p: str) -> str:
    return f"{p}-{uuid.uuid4().hex[:8]}"


class _FakeSensor:
    def __init__(self):
        self.events = []

    def emit(self, name, summary="", **kw):
        self.events.append(name)
        return None


class _FakeObs:
    def __init__(self):
        self.emergence_sensor = _FakeSensor()


def _fresh(config=None):
    engine = EmergenceEngine()
    obs = _FakeObs()
    gov = VariantGovernance(engine, obs, config=config or GovernanceConfig())
    return engine, obs, gov


def _propose(engine, gov, desc="v"):
    v = engine.propose_variant(desc, {"k": "v"})
    gov.submit(v.variant_id, desc)
    return v.variant_id


# -- voting state machine ---------------------------------------------------

def test_submit_starts_in_voting():
    engine, _, gov = _fresh()
    vid = _propose(engine, gov)
    assert gov.get(vid).status is GovernanceStatus.VOTING


def test_votes_dedup_per_agent():
    engine, _, gov = _fresh()
    vid = _propose(engine, gov)
    gov.cast_vote(vid, "a", "for")
    res = gov.cast_vote(vid, "a", "against")  # same agent overwrites, not double-counts
    assert res["tally"]["total"] == 1
    assert res["tally"]["against"] == 1


def test_quorum_not_met_stays_voting():
    engine, _, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    gov.cast_vote(vid, "a", "for")
    r = gov.cast_vote(vid, "b", "for")
    assert r["tally"]["quorum_met"] is False
    assert gov.get(vid).status is GovernanceStatus.VOTING


def test_ratio_below_threshold_stays_voting():
    engine, _, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    gov.cast_vote(vid, "a", "for")
    gov.cast_vote(vid, "b", "against")
    r = gov.cast_vote(vid, "c", "against")  # 1/3 ratio, quorum met but fails ratio
    assert r["tally"]["passed"] is False
    assert gov.get(vid).status is GovernanceStatus.VOTING


def test_pass_moves_to_vote_passed_and_does_not_auto_promote():
    engine, _, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    for a in ("a", "b", "c"):
        gov.cast_vote(vid, a, "for")
    # human gate: passed the vote but NOT promoted yet
    assert gov.get(vid).status is GovernanceStatus.VOTE_PASSED
    assert engine.variants[vid].status is VariantStatus.PROPOSED


def test_voting_closed_after_pass():
    engine, _, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    for a in ("a", "b", "c"):
        gov.cast_vote(vid, a, "for")
    with pytest.raises(GovernanceError):
        gov.cast_vote(vid, "d", "for")


def test_close_voting_fails_variant():
    engine, _, gov = _fresh(GovernanceConfig(quorum=5, approval_ratio=0.66))
    vid = _propose(engine, gov)
    gov.cast_vote(vid, "a", "for")
    gov.close_voting(vid, actor="human", reason="stale")
    assert gov.get(vid).status is GovernanceStatus.VOTE_FAILED


def test_invalid_vote_value():
    engine, _, gov = _fresh()
    vid = _propose(engine, gov)
    with pytest.raises(GovernanceError):
        gov.cast_vote(vid, "a", "maybe")


# -- the human gate ---------------------------------------------------------

def test_human_approve_requires_vote_passed():
    engine, _, gov = _fresh()
    vid = _propose(engine, gov)
    with pytest.raises(GovernanceError):
        gov.human_approve(vid, "operator", "approve")  # still VOTING


def test_human_approve_promotes_into_canary():
    engine, _, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    for a in ("a", "b", "c"):
        gov.cast_vote(vid, a, "for")
    res = gov.human_approve(vid, "operator", "approve", reason="lgtm")
    assert res["status"] == GovernanceStatus.PROMOTED.value
    assert res["promotion_error"] is None
    # wired all the way into the emergence canary pipeline
    assert engine.variants[vid].status is VariantStatus.CANARY
    assert engine.variants[vid].canary_percentage > 0.0


def test_human_reject_blocks_promotion():
    engine, _, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    for a in ("a", "b", "c"):
        gov.cast_vote(vid, a, "for")
    res = gov.human_approve(vid, "operator", "reject", reason="risky")
    assert res["status"] == GovernanceStatus.REJECTED.value
    # never promoted
    assert engine.variants[vid].status is VariantStatus.PROPOSED


def test_human_gate_disabled_auto_promotes():
    engine, _, gov = _fresh(
        GovernanceConfig(quorum=2, approval_ratio=0.5, require_human_approval=False))
    vid = _propose(engine, gov)
    gov.cast_vote(vid, "a", "for")
    gov.cast_vote(vid, "b", "for")
    assert gov.get(vid).status is GovernanceStatus.PROMOTED
    assert engine.variants[vid].status is VariantStatus.CANARY


# -- audit ------------------------------------------------------------------

def test_full_lifecycle_is_audited():
    engine, obs, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    for a in ("a", "b", "c"):
        gov.cast_vote(vid, a, "for")
    gov.human_approve(vid, "operator", "approve")
    names = obs.emergence_sensor.events
    rec_events = [e["event"] for e in gov.get(vid).audit]
    for expected in ("governance_submitted", "governance_vote_cast",
                     "governance_vote_passed", "governance_human_approved",
                     "governance_promoted_to_canary"):
        assert expected in names, f"{expected} not emitted to obs"
        assert expected in rec_events, f"{expected} not in record audit trail"


def test_awaiting_approval_queue():
    engine, _, gov = _fresh(GovernanceConfig(quorum=3, approval_ratio=0.66))
    vid = _propose(engine, gov)
    for a in ("a", "b", "c"):
        gov.cast_vote(vid, a, "for")
    queue = gov.list_awaiting_approval()
    assert [r["variant_id"] for r in queue] == [vid]


# -- server integration -----------------------------------------------------

def test_server_tools_drive_governed_promotion():
    import xenocomm_mcp.server as srv

    proposed = srv.propose_protocol_variant("srv-gov", {"compression": "zstd"})
    vid = proposed["variant_id"]
    assert proposed["governance"]["status"] == "voting"

    q = srv.variant_governance.config.quorum
    for i in range(q):
        srv.cast_variant_vote(vid, f"agent-{i}", "for", "looks good")
    assert srv.variant_governance.get(vid).status is GovernanceStatus.VOTE_PASSED
    assert any(r["variant_id"] == vid
               for r in srv.list_variants_awaiting_approval()["variants"])

    res = srv.approve_variant(vid, "operator", "approve", "ship it")
    assert res["status"] == GovernanceStatus.PROMOTED.value
    assert srv.emergence_engine.variants[vid].status is VariantStatus.CANARY


def test_start_canary_tool_fails_closed_for_ungoverned_variant():
    import xenocomm_mcp.server as srv

    # Variant created directly on the engine -> no governance record at all.
    v = srv.emergence_engine.propose_variant(_uid("nogov"), {"k": "v"})
    srv.emergence_engine.start_testing(v.variant_id)
    res = srv.start_canary_deployment(v.variant_id)  # must fail closed
    assert "error" in res
    assert res["governance_status"] == "ungoverned"
    assert srv.emergence_engine.variants[v.variant_id].status is VariantStatus.TESTING


def test_start_canary_on_promoted_variant_is_graceful_not_crash():
    import xenocomm_mcp.server as srv

    proposed = srv.propose_protocol_variant("promoted-graceful", {"k": "v"})
    vid = proposed["variant_id"]
    for i in range(srv.variant_governance.config.quorum):
        srv.cast_variant_vote(vid, f"a{i}", "for")
    srv.approve_variant(vid, "operator", "approve")
    assert srv.emergence_engine.variants[vid].status is VariantStatus.CANARY
    # Re-invoking canary on an already-promoted variant must not raise.
    assert "error" in srv.start_canary_deployment(vid, force=True)
    assert "error" in srv.start_canary_deployment(vid)


def test_evolution_workflow_is_gated_by_governance():
    import xenocomm_mcp.server as srv

    wf = srv.workflow_manager.evolution
    execution = wf.start("wf-gov", {"k": "v"})
    eid = execution.execution_id
    while execution.status.value == "running":
        execution = wf.execute_step(eid)

    vid = execution.context["variant_id"]
    # propose step placed it under governance...
    assert srv.variant_governance.has(vid)
    # ...but with no votes/approval it must never reach real canary traffic.
    assert srv.emergence_engine.variants[vid].status is not VariantStatus.CANARY
    assert execution.status.value == "failed"


def test_start_canary_tool_refuses_to_bypass_governance():
    import xenocomm_mcp.server as srv

    proposed = srv.propose_protocol_variant("srv-bypass", {"k": "v"})
    vid = proposed["variant_id"]
    srv.emergence_engine.start_testing(vid)
    # not approved through governance -> the tool must refuse
    blocked = srv.start_canary_deployment(vid)
    assert "error" in blocked
    assert srv.emergence_engine.variants[vid].status is VariantStatus.TESTING
    # force escape hatch still works (mechanism / tests)
    forced = srv.start_canary_deployment(vid, force=True)
    assert "error" not in forced
    assert srv.emergence_engine.variants[vid].status is VariantStatus.CANARY

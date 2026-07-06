"""M2 — the KFM lifecycle engine (Agentic Evolution: Kill/Fuck/Marry) is now
reachable as MCP tools. Previously implemented but exposed by zero tools."""

import uuid

import xenocomm_mcp.server as srv


def _uid(p: str) -> str:
    return f"{p}-{uuid.uuid4().hex[:8]}"


def test_kfm_full_lifecycle_flow():
    eid = _uid("kfm")
    reg = srv.register_lifecycle_entity(eid, "agent")
    assert reg["entity_id"] == eid
    assert reg["phase"] == "proposed"

    upd = srv.update_lifecycle_metrics(
        eid,
        performance={"success_rate": 0.98, "error_rate": 0.01},
        alignment={"goal_alignment": 0.95, "collaboration_success": 0.9, "protocol_compliance": 1.0},
        evolution={"innovation_score": 0.3},
    )
    assert "error" not in upd
    assert set(upd["kfm_scores"]) == {"kill", "fuck", "marry"}
    assert upd["recommendation"] in ("kill", "fuck", "marry")

    # PROPOSED entities are recommended into the EXPERIMENTAL phase.
    ev = srv.evaluate_lifecycle_transition(eid)
    assert ev["transition_recommended"] is True
    assert ev["recommended_phase"] == "experimental"

    tr = srv.transition_lifecycle_phase(eid, "experimental", "eval")
    assert tr["phase"] == "experimental"

    dash = srv.get_lifecycle_dashboard()
    assert dash["total_entities"] >= 1
    assert dash["by_phase"]["experimental"] >= 1


def test_kfm_update_unknown_entity_returns_error():
    r = srv.update_lifecycle_metrics("unregistered-x", performance={"success_rate": 0.5})
    assert "error" in r


def test_kfm_update_bad_field_returns_error():
    eid = _uid("kfm-badfield")
    srv.register_lifecycle_entity(eid, "agent")
    r = srv.update_lifecycle_metrics(eid, performance={"not_a_real_field": 1.0})
    assert "error" in r


def test_kfm_transition_invalid_phase_returns_error():
    eid = _uid("kfm-badphase")
    srv.register_lifecycle_entity(eid, "agent")
    r = srv.transition_lifecycle_phase(eid, "not-a-phase")
    assert "error" in r


def test_kfm_stateless_score():
    r = srv.score_agent_kfm(
        success_rate=0.2, error_rate=0.6, alignment_score=0.2, collaboration_success=0.2
    )
    assert set(r["kfm_scores"]) == {"kill", "fuck", "marry"}
    assert r["recommendation"] in ("kill", "fuck", "marry")
    # A failing, misaligned agent should not score more integrate-worthy (marry)
    # than eliminate-worthy (kill).
    assert r["kfm_scores"]["kill"] >= r["kfm_scores"]["marry"]


def test_kfm_recommend_action_from_alignment():
    r = srv.recommend_agent_lifecycle_action(
        "agent-x",
        alignment_result={"overall_score": 0.9},
        performance_history=[{"success": True}, {"success": True}, {"success": False}],
    )
    assert r["agent_id"] == "agent-x"
    assert r["recommendation"] in ("kill", "fuck", "marry")
    assert "action" in r and "steps" in r

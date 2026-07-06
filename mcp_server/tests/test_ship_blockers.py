"""Regression tests for the seven Track B ship-blockers.

Each of these bugs crashed or dead-ended a *documented* MCP flow on the live
server surface. Pre-fix they raised AttributeError / TypeError / ValueError or
returned an ``{"error": ...}`` dict; post-fix the flow completes. The
server-surface tests import ``xenocomm_mcp.server`` and call the tool functions
directly (``@mcp.tool()`` is a passthrough), so they exercise exactly what an
MCP client would hit.
"""

import uuid

import pytest

from xenocomm_mcp.alignment import AgentContext
from xenocomm_mcp.orchestrator import XenoCommOrchestrator
from xenocomm_mcp.emergence import EmergenceEngine, VariantStatus, RollbackReason
from xenocomm_mcp.instrumented import InstrumentedEmergenceEngine
from xenocomm_mcp.observation import get_observation_manager


def _ctx(agent_id: str, domains=None) -> AgentContext:
    return AgentContext(
        agent_id=agent_id,
        capabilities={},
        knowledge_domains=domains or ["shared"],
        goals=[],
        terminology={},
        assumptions=[],
        context_params={},
    )


def _uid(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:8]}"


# --------------------------------------------------------------------------
# B1 — workflows.py referenced a non-existent ``alignment.contexts`` (10 sites)
# --------------------------------------------------------------------------
def test_b1_onboarding_register_and_alignment_steps_complete():
    """B1 scope: the Register + Check-Alignment steps used to raise
    AttributeError on ``alignment.contexts``. They must now complete and the
    new agent must be registered.

    (The later 'Establish Protocol' step has a *separate* pre-existing bug
    unrelated to B1, so we assert on the two steps B1 actually repairs rather
    than on whole-workflow completion.)"""
    import xenocomm_mcp.server as srv

    existing = _uid("b1-existing")
    newcomer = _uid("b1-newcomer")
    srv.register_agent(agent_id=existing, knowledge_domains=["shared"])

    started = srv.start_onboarding_workflow(
        new_agent_id=newcomer,
        existing_agent_ids=[existing],
        knowledge_domains=["shared"],
    )
    final = srv.execute_workflow_all_steps(started["execution_id"], "multi_agent_onboarding")

    steps = {s["name"]: s["status"] for s in final["steps"]}
    assert steps.get("Register Agent") == "completed"
    assert steps.get("Check Alignment") == "completed"
    assert newcomer in srv.orchestrator.alignment.registered_agents


# --------------------------------------------------------------------------
# B2 — InstrumentedEmergenceEngine.rollback called ``.get()`` on a
#      RollbackPoint/None, and had dropped the ``reason`` parameter entirely.
# --------------------------------------------------------------------------
def test_b2_instrumented_rollback_no_rollback_point():
    eng = InstrumentedEmergenceEngine(observation_manager=get_observation_manager())
    v = eng.propose_variant("b2", {"k": "v"})
    # No rollback point exists -> base returns None; the override must not
    # call ``.get()`` on it.
    result = eng.rollback(v.variant_id)
    assert result is None or hasattr(result, "point_id")
    assert v.status == VariantStatus.ROLLED_BACK


def test_b2_instrumented_rollback_accepts_reason():
    """The base engine's auto-rollback calls ``self.rollback(vid, reason=...)``;
    the override must accept that keyword (it previously TypeError'd)."""
    eng = InstrumentedEmergenceEngine(observation_manager=get_observation_manager())
    v = eng.propose_variant("b2b", {"k": "v"})
    eng.rollback(v.variant_id, reason=RollbackReason.CIRCUIT_OPEN)
    assert v.metadata["rollback_reason"] == "circuit_open"


# --------------------------------------------------------------------------
# B3 — orchestrator treated should_rollback's (bool, reason) tuple as a bare
#      bool, so a healthy canary was ALWAYS rolled back.
# --------------------------------------------------------------------------
def test_b3_healthy_canary_is_ramped_not_rolled_back():
    orch = XenoCommOrchestrator()
    orch.register_agent(_ctx(_uid("b3a")))
    b_id = _uid("b3b")
    orch.register_agent(_ctx(b_id))
    a_id = list(orch.agent_registry)[0]
    session = orch.initiate_collaboration(a_id, b_id, None, 0.0)

    v = orch.emergence.propose_variant("b3", {"k": "v"})
    orch.emergence.start_testing(v.variant_id)
    orch.emergence.start_canary(v.variant_id)
    # Healthy variant: no metrics history -> should_rollback == (False, None)
    assert orch.emergence.should_rollback(v.variant_id) == (False, None)

    result = orch.evolve_session_protocol(session.session_id, v.variant_id)
    assert result["action"] == "ramped_canary"  # not "rolled_back"


# --------------------------------------------------------------------------
# B4 — get_learning_insights referenced a non-existent VariantStatus.FAILED,
#      crashing on the first call after any recorded outcome.
# --------------------------------------------------------------------------
def test_b4_learning_insights_after_outcome():
    eng = EmergenceEngine()
    v = eng.propose_variant("b4", {"compression": "gzip"})
    eng.rollback(v.variant_id)  # records an outcome (final_status=ROLLED_BACK)
    assert len(eng.outcomes) == 1

    # Previously raised: AttributeError: VariantStatus has no attribute 'FAILED'
    insights = eng.get_learning_insights()
    assert insights["total_outcomes"] == 1
    assert "success_rate" in insights


# --------------------------------------------------------------------------
# B5 — register_agent tool wrote only to the alignment engine, so
#      initiate_collaboration (which reads orchestrator.agent_registry) always
#      failed "Agent not registered".
# --------------------------------------------------------------------------
def test_b5_initiate_collaboration_finds_registered_agents():
    import xenocomm_mcp.server as srv

    a, b = _uid("b5a"), _uid("b5b")
    srv.register_agent(agent_id=a, knowledge_domains=["shared"])
    srv.register_agent(agent_id=b, knowledge_domains=["shared"])
    # Both registries must now see the agents.
    assert a in srv.orchestrator.agent_registry
    assert a in srv.orchestrator.alignment.registered_agents

    session = srv.initiate_collaboration(a, b, None, 0.0)
    assert "error" not in session
    assert session.get("session_id")


# --------------------------------------------------------------------------
# B6 — no MCP tool advanced AWAITING_RESPONSE -> PROPOSAL_RECEIVED, so a
#      responder could only ever reject; accept/counter raised ValueError.
# --------------------------------------------------------------------------
def test_b6_responder_can_accept():
    import xenocomm_mcp.server as srv

    initiator, responder = _uid("b6i"), _uid("b6r")
    session = srv.initiate_negotiation(initiator, responder, {"data_format": "json"})
    result = srv.respond_to_negotiation(session["session_id"], responder, "accept")
    assert "error" not in result
    assert result["state"] == "awaiting_finalization"


def test_b6_responder_can_counter():
    import xenocomm_mcp.server as srv

    initiator, responder = _uid("b6i2"), _uid("b6r2")
    session = srv.initiate_negotiation(initiator, responder, {"data_format": "json"})
    result = srv.respond_to_negotiation(
        session["session_id"], responder, "counter", counter_params={"data_format": "msgpack"}
    )
    assert "error" not in result
    assert result["state"] == "awaiting_finalization"


# --------------------------------------------------------------------------
# B7 — execute_workflow_step accepted only short keys, but discovery/creation
#      tools return long names, so echoing the name back yielded
#      "Unknown workflow type".
# --------------------------------------------------------------------------
def test_b7_execute_step_accepts_discovery_name():
    import xenocomm_mcp.server as srv

    existing = _uid("b7-existing")
    newcomer = _uid("b7-newcomer")
    srv.register_agent(agent_id=existing, knowledge_domains=["shared"])
    started = srv.start_onboarding_workflow(
        new_agent_id=newcomer, existing_agent_ids=[existing], knowledge_domains=["shared"]
    )
    # The name the creation tool handed back must be accepted verbatim.
    name = started["workflow_name"]
    assert name == "multi_agent_onboarding"
    stepped = srv.execute_workflow_step(started["execution_id"], name)
    assert stepped.get("error") != f"Unknown workflow type: {name}"
    assert "error" not in stepped


# --------------------------------------------------------------------------
# Coverage hardening (from the fix-verification pass): exercise the exact
# branches the original bugs crashed on, and the second/other fix sites.
# --------------------------------------------------------------------------
def test_b2_instrumented_rollback_with_rollback_point():
    """The point-present branch is the one the original ``result.get(...)``
    crashed on (a RollbackPoint dataclass has no ``.get``)."""
    eng = InstrumentedEmergenceEngine(observation_manager=get_observation_manager())
    v = eng.propose_variant("b2c", {"k": "v"})
    eng.start_testing(v.variant_id)
    eng.start_canary(v.variant_id)  # start_canary creates a rollback point
    result = eng.rollback(v.variant_id, reason=RollbackReason.CIRCUIT_OPEN)
    assert result is not None and hasattr(result, "point_id")
    assert v.metadata["rollback_reason"] == "circuit_open"


def test_b3_unhealthy_canary_is_rolled_back():
    """The rolled_back branch: an inverted unpack would still pass the healthy
    test, so drive should_rollback True and assert the rollback fires."""
    orch = XenoCommOrchestrator()
    a, b = _uid("b3ua"), _uid("b3ub")
    orch.register_agent(_ctx(a))
    orch.register_agent(_ctx(b))
    session = orch.initiate_collaboration(a, b)

    v = orch.emergence.propose_variant("b3u", {"k": "v"})
    orch.emergence.start_testing(v.variant_id)
    orch.emergence.start_canary(v.variant_id)
    cb = orch.emergence.circuit_breakers[v.variant_id]
    for _ in range(cb.failure_threshold):
        cb.record_failure()
    assert orch.emergence.should_rollback(v.variant_id)[0] is True

    result = orch.evolve_session_protocol(session.session_id, v.variant_id)
    assert result["action"] == "rolled_back"
    assert v.status == VariantStatus.ROLLED_BACK


def test_b3_report_metrics_does_not_falsely_flag_healthy_variant():
    """Second B3 site (orchestrator.py:578). A HEALTHY active variant must NOT
    be flagged for rollback. This is the discriminating case: pre-fix the bare
    truthy ``(False, None)`` tuple always set should_rollback=True; an unhealthy
    variant would pass either way, so we assert the healthy path stays clean."""
    orch = XenoCommOrchestrator()
    a, b = _uid("b3ra"), _uid("b3rb")
    orch.register_agent(_ctx(a))
    orch.register_agent(_ctx(b))
    session = orch.initiate_collaboration(a, b)

    v = orch.emergence.propose_variant("b3r", {"k": "v"})
    orch.emergence.start_testing(v.variant_id)
    orch.emergence.start_canary(v.variant_id)
    session.active_variant_id = v.variant_id
    # Healthy variant -> should_rollback == (False, None); report GOOD metrics.
    result = orch.report_session_metrics(
        session.session_id, success_rate=0.99, latency_ms=10.0, error_count=0
    )
    assert result.get("should_rollback") is not True


def test_b7_all_workflow_aliases_resolve():
    """Every discovery-tool long name and its short key must map correctly;
    unknown names pass through to the existing error path."""
    import xenocomm_mcp.server as srv

    expected = {
        "multi_agent_onboarding": "onboarding",
        "protocol_evolution": "evolution",
        "error_recovery": "recovery",
        "conflict_resolution": "conflict",
    }
    for long_name, short in expected.items():
        assert srv._WORKFLOW_TYPE_ALIASES[long_name] == short
        assert srv._WORKFLOW_TYPE_ALIASES[short] == short
    assert srv._WORKFLOW_TYPE_ALIASES.get("bogus", "bogus") == "bogus"

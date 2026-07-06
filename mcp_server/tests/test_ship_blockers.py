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
def test_b1_onboarding_workflow_registers_new_agent():
    """The onboarding workflow's register/alignment steps must not raise
    AttributeError, and must actually register the new agent."""
    import xenocomm_mcp.server as srv

    existing = _uid("b1-existing")
    newcomer = _uid("b1-newcomer")
    srv.register_agent(agent_id=existing, knowledge_domains=["shared"])

    started = srv.start_onboarding_workflow(
        new_agent_id=newcomer,
        existing_agent_ids=[existing],
        knowledge_domains=["shared"],
    )
    # Run it to completion through the (previously crashing) steps.
    final = srv.execute_workflow_all_steps(started["execution_id"], "multi_agent_onboarding")

    assert "error" not in final
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

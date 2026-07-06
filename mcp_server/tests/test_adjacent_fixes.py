"""Regression tests for the three adjacent coordination bugs found during the
Track B fix-verification pass.

- adj1: the onboarding workflow's _step_establish finalized negotiation from the
  wrong state, so the workflow never completed.
- adj2: the initiate_collaboration tool passed proposed_params / the alignment
  threshold into the orchestrator's required_domains / proposed_params slots.
- adj3: get_variant_status / track_variant_performance embedded the raw
  (bool, RollbackReason) should_rollback tuple, which is not JSON-serializable.
"""

import json
import uuid

import xenocomm_mcp.server as srv


def _uid(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:8]}"


# adj1 -----------------------------------------------------------------------
def test_adj1_onboarding_workflow_reaches_completed():
    existing = _uid("adj1-e")
    newcomer = _uid("adj1-n")
    srv.register_agent(agent_id=existing, knowledge_domains=["shared"])
    started = srv.start_onboarding_workflow(
        new_agent_id=newcomer, existing_agent_ids=[existing], knowledge_domains=["shared"]
    )
    final = srv.execute_workflow_all_steps(started["execution_id"], "multi_agent_onboarding")

    steps = {s["name"]: s["status"] for s in final["steps"]}
    assert steps.get("Establish Protocol") == "completed"  # previously "failed"
    assert final["status"] == "completed"


# adj2 -----------------------------------------------------------------------
def test_adj2_initiate_collaboration_honors_threshold():
    a, b = _uid("adj2-a"), _uid("adj2-b")
    srv.register_agent(agent_id=a, knowledge_domains=["shared"])
    srv.register_agent(agent_id=b, knowledge_domains=["shared"])

    # A satisfiable threshold is met; an impossible one (>1.0) is not.
    lax = srv.initiate_collaboration(a, b, None, 0.0)
    assert lax["alignment_sufficient"] is True
    strict = srv.initiate_collaboration(a, b, None, 2.0)
    assert strict["alignment_sufficient"] is False


def test_adj2_initiate_collaboration_routes_proposed_params():
    a, b = _uid("adj2p-a"), _uid("adj2p-b")
    srv.register_agent(agent_id=a, knowledge_domains=["shared"])
    srv.register_agent(agent_id=b, knowledge_domains=["shared"])
    # proposed_params now lands in the proposed_params slot (not required_domains).
    result = srv.initiate_collaboration(a, b, {"data_format": "msgpack"}, 0.0)
    assert "error" not in result
    assert "alignment_sufficient" in result


# adj3 -----------------------------------------------------------------------
def _unhealthy_variant():
    v = srv.emergence_engine.propose_variant(_uid("adj3"), {"k": "v"})
    srv.emergence_engine.start_testing(v.variant_id)
    srv.emergence_engine.start_canary(v.variant_id)
    cb = srv.emergence_engine.circuit_breakers[v.variant_id]
    for _ in range(cb.failure_threshold):
        cb.record_failure()
    return v


def test_adj3_get_variant_status_serializable_when_unhealthy():
    v = _unhealthy_variant()
    status = srv.get_variant_status(v.variant_id)
    assert status["should_rollback"] is True  # bool, not a (bool, Enum) tuple
    assert status["rollback_reason"] == "circuit_open"
    json.dumps(status)  # previously raised TypeError on the raw RollbackReason


def test_adj3_track_variant_performance_serializable_when_unhealthy():
    v = _unhealthy_variant()
    perf = srv.track_variant_performance(v.variant_id, 0.1, 9999.0, error_count=50)
    assert isinstance(perf["should_rollback"], bool)
    assert perf["rollback_reason"] is not None
    json.dumps(perf)

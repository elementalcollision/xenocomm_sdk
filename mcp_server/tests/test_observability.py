"""E1 — the observability substrate now runs in the product.

Before: the server used the base ObservationManager (in-memory ring only); the
analytics/persistence lived in an unwired EnhancedObservationManager, and
parent_event_id was never populated (causality was schema-only).

After: the server runs EnhancedObservationManager (analytics + optional
gzip-JSONL persistence), exposes it via get_flow_analytics, and flagship flows
emit a real causal tree.
"""

import gzip
import uuid
from pathlib import Path

import xenocomm_mcp.server as srv
from xenocomm_mcp.analytics import EnhancedObservationManager


def _uid(prefix: str) -> str:
    return f"{prefix}-{uuid.uuid4().hex[:8]}"


def _pair():
    a, b = _uid("e1a"), _uid("e1b")
    srv.register_agent(agent_id=a, knowledge_domains=["shared"])
    srv.register_agent(agent_id=b, knowledge_domains=["shared"])
    return a, b


# --- wiring ----------------------------------------------------------------
def test_e1_server_runs_enhanced_manager():
    assert isinstance(srv._obs_manager, EnhancedObservationManager)
    assert srv._obs_manager.analytics is not None
    # the process-wide singleton is the same enhanced instance
    from xenocomm_mcp.observation import get_observation_manager
    assert get_observation_manager() is srv._obs_manager


def test_e1_get_flow_analytics_tool():
    a, b = _pair()
    srv.initiate_collaboration(a, b, None, 0.0)
    summary = srv.get_flow_analytics()
    assert "error" not in summary
    for key in ("flow_metrics", "throughput_trend", "error_summary", "anomalies"):
        assert key in summary


# --- persistence -----------------------------------------------------------
def test_e1_persistence_writes_gzip_jsonl(tmp_path):
    mgr = EnhancedObservationManager(persist_to=str(tmp_path))
    mgr.start()
    try:
        mgr.agent_sensor.agent_registered(
            agent_id="persist-me", capabilities=["c"], domains=["d"]
        )
    finally:
        mgr.stop()  # flushes the persistence buffer

    files = list(Path(tmp_path).glob("flows_*.jsonl*"))
    assert files, "expected a persisted flow log file"
    f = files[0]
    content = (
        gzip.open(f, "rt").read() if f.suffix == ".gz" else f.read_text()
    )
    assert "persist-me" in content


def test_e1_persistence_off_without_env(tmp_path):
    # No persist path -> analytics still on, but nothing written to disk.
    mgr = EnhancedObservationManager(persist_to=None)
    assert mgr.persistence is None
    assert mgr.analytics is not None


# --- causality (parent_event_id) -------------------------------------------
def test_e1_causality_parent_event_id_populated():
    a, b = _pair()
    srv.initiate_collaboration(a, b, None, 0.0)

    events = srv._obs_manager.event_bus.get_recent_events(count=500)
    mine = [
        e for e in events
        if a in (e.source_agent, e.target_agent)
        or (e.session_id and a in e.session_id)
    ]
    root = next(e for e in mine if e.event_name == "collaboration_initiated")
    session_ev = next(e for e in mine if e.event_name == "session_created")

    # The child links to the collaboration root; the root has no parent.
    assert session_ev.parent_event_id == root.event_id
    assert root.parent_event_id is None


def test_e1_causal_scope_context_manager():
    from xenocomm_mcp.observation import causal_scope, _current_parent_event_id

    assert _current_parent_event_id.get() is None
    with causal_scope("root-123"):
        assert _current_parent_event_id.get() == "root-123"
    assert _current_parent_event_id.get() is None  # reset on exit


# --- durability + sentinel (from the verification pass) ---------------------
def test_e1_persistence_buffer_flushes_on_stop(tmp_path):
    """A short run (fewer than buffer_size events) must still reach disk when
    the manager stops — the server registers stop() on atexit so a normal
    shutdown does not silently drop the buffered tail."""
    mgr = EnhancedObservationManager(persist_to=str(tmp_path))
    mgr.start()
    for i in range(5):
        mgr.agent_sensor.agent_registered(
            agent_id=f"buf{i}", capabilities=[], domains=["d"]
        )
    # Below the buffer threshold -> still buffered, nothing on disk yet.
    assert not list(Path(tmp_path).glob("flows_*"))
    mgr.stop()  # exactly what atexit invokes on the server
    assert list(Path(tmp_path).glob("flows_*")), "stop() must flush to disk"


def test_e1_explicit_none_parent_forces_root():
    """The _UNSET sentinel lets an explicit None mean 'root' even inside a
    causal scope, distinct from an omitted arg (which inherits the scope)."""
    from xenocomm_mcp.observation import causal_scope

    sensor = srv._obs_manager.agent_sensor
    with causal_scope("scope-root"):
        inherited = sensor.agent_registered(
            agent_id=_uid("inh"), capabilities=[], domains=["d"]
        )
        explicit_root = sensor.emit("custom_root", "x", parent_event_id=None)

    assert inherited.parent_event_id == "scope-root"  # omitted -> inherits
    assert explicit_root.parent_event_id is None       # explicit None -> root

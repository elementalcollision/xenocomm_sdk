"""Regression guard for the datetime.utcnow() -> datetime.now(UTC) migration.

utcnow() returned naive datetimes and is deprecated. Everything now produces
timezone-aware UTC datetimes; this pins that so a naive datetime cannot creep
back in (which would both re-introduce the deprecation and risk naive-vs-aware
TypeErrors).
"""

from datetime import datetime, timezone

import xenocomm_mcp.server as srv
from xenocomm_mcp.analytics import EnhancedObservationManager, _ensure_utc
from xenocomm_mcp.emergence import EmergenceEngine


def test_flow_event_timestamps_are_utc_aware():
    ev = srv._obs_manager.agent_sensor.agent_registered(
        agent_id="tz-check", capabilities=[], domains=["d"]
    )
    assert ev.timestamp.tzinfo is not None
    assert ev.timestamp.utcoffset().total_seconds() == 0


def test_dataclass_default_factory_timestamps_are_aware():
    # default_factory=lambda: datetime.now(timezone.utc) on the variant dataclass
    v = EmergenceEngine().propose_variant("tz", {"k": "v"})
    assert v.created_at.tzinfo is not None
    assert v.updated_at.tzinfo is not None


def test_persistence_roundtrip_yields_aware_timestamps(tmp_path):
    mgr = EnhancedObservationManager(persist_to=str(tmp_path))
    mgr.start()
    mgr.agent_sensor.agent_registered(agent_id="rt", capabilities=[], domains=["d"])
    mgr.stop()  # flush

    events = mgr.persistence.read_events()
    assert events
    assert all(e.timestamp.tzinfo is not None for e in events)


def test_ensure_utc_normalizes_naive_but_preserves_aware():
    naive = datetime(2020, 1, 1, 0, 0, 0)
    assert naive.tzinfo is None
    fixed = _ensure_utc(naive)
    assert fixed.tzinfo is timezone.utc

    already = datetime(2020, 1, 1, tzinfo=timezone.utc)
    assert _ensure_utc(already) is already

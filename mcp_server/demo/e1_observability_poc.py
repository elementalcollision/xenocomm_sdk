#!/usr/bin/env python3
"""XenoComm E1 observability deep-dive — a runnable proof of concept.

Zooms in on the observability substrate wired into the product by E1 (PR #6):
the typed FlowEvent bus, causal linking via ``parent_event_id``, the analytics
engine (per-flow-type + per-agent metrics, error summary, anomaly detection),
and gzip-JSONL persistence.

The scenario drives several real collaborations through the broker. Each
``initiate_collaboration`` emits a ``collaboration_initiated`` root and, inside a
``causal_scope``, its alignment + session events inherit that root as their
``parent_event_id`` — forming a real (if shallow) event tree. A little
negotiation traffic and a couple of failures give the analytics something to
chew on.

Everything here is real observability output: the events, the causal links, the
analytics, and the persisted log file are all produced by the shipped code. The
inter-agent message exchanges are simulated (XenoComm is a coordination +
observability broker, not a transport), and — honestly — causal linking is only
populated at the collaboration boundary (a 2-level tree), analytics read the
in-memory event bus, and persistence is a separate write/export path.

Output: writes demo/run_e1.json (consumed by observability.html) + stdout.
Run:  mcp_server/.venv/bin/python demo/e1_observability_poc.py
"""

from __future__ import annotations

import gzip
import json
import os
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
from tempfile import mkdtemp

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))

# Enable gzip-JSONL persistence BEFORE importing the server (the obs manager
# reads XENOCOMM_FLOW_LOG_DIR at import time).
_LOG_DIR = mkdtemp(prefix="xeno-e1-flows-")
os.environ["XENOCOMM_FLOW_LOG_DIR"] = _LOG_DIR

from xenocomm_mcp.envfile import load_env_files  # noqa: E402

load_env_files()

import xenocomm_mcp.server as srv  # noqa: E402
from xenocomm_mcp.observation import EventSeverity  # noqa: E402

AGENTS = [
    {"id": "planner-claude", "vendor": "anthropic"},
    {"id": "executor-deepseek", "vendor": "deepseek"},
    {"id": "reviewer-gpt", "vendor": "openai"},
    {"id": "router-llama", "vendor": "meta"},
]
# collaborations that will each produce a causal tree
PAIRS = [("planner-claude", "executor-deepseek"),
         ("planner-claude", "reviewer-gpt"),
         ("executor-deepseek", "router-llama")]


def narrate(step: str, msg: str) -> None:
    print(f"  [{step:<11}] {msg}")


def _events(payload) -> list:
    return payload.get("events", []) if isinstance(payload, dict) else (payload or [])


def _read_persisted(log_dir: str) -> dict:
    files = []
    for p in sorted(Path(log_dir).glob("flows_*.jsonl*")):
        try:
            if p.suffix == ".gz":
                with gzip.open(p, "rt") as fh:
                    raw = fh.read()
            else:
                raw = p.read_text()
            n = sum(1 for ln in raw.splitlines() if ln.strip())
        except Exception:
            n = 0
        files.append({"name": p.name, "bytes": p.stat().st_size, "events": n})
    return {"dir": log_dir, "files": files}


def run() -> dict:
    print("\nXenoComm E1 observability deep-dive\n" + "-" * 40)

    for a in AGENTS:
        srv.register_agent(agent_id=a["id"], knowledge_domains=["shared", a["vendor"]],
                           capabilities={"vendor": a["vendor"]})
    narrate("register", f"{len(AGENTS)} agents joined the broker")

    # Drive real collaborations -> each emits a causal tree.
    for a, b in PAIRS:
        srv.initiate_collaboration(a, b, required_alignment_score=0.0)
    narrate("collaborate", f"{len(PAIRS)} collaborations → {len(PAIRS)} causal trees")

    # A negotiation, plus simulated exchanges and two failures for the analytics.
    neg = srv.initiate_negotiation(PAIRS[0][0], PAIRS[0][1], {"data_format": "json", "batch_size": 16})
    nsid = neg.get("session_id")
    srv.respond_to_negotiation(nsid, PAIRS[0][1], "accept")
    obs = srv._obs_manager
    for i in range(3):
        obs.negotiation_sensor.emit("message_exchanged", f"exchange {i}",
                                    source_agent=PAIRS[0][0], target_agent=PAIRS[0][1],
                                    session_id=nsid, metrics={"latency_ms": 40 + i})
    for j in range(2):
        obs.negotiation_sensor.emit("exchange_failed", f"timeout {j}",
                                    source_agent=PAIRS[0][1], target_agent=PAIRS[0][0],
                                    session_id=nsid, severity=EventSeverity.ERROR,
                                    metrics={"latency_ms": 5000, "error": "timeout"}, tags=["error"])
    narrate("traffic", "negotiation + 3 exchanges + 2 failures (exchanges simulated)")

    # Collect the observability output.
    events = sorted(_events(srv.get_recent_flow_events(count=500)), key=lambda e: e.get("timestamp", ""))
    analytics = srv.get_flow_analytics()

    # Reconstruct causal trees from parent_event_id.
    by_parent = defaultdict(list)
    for e in events:
        pid = e.get("parent_event_id")
        if pid:
            by_parent[pid].append(e)
    ids = {e["event_id"] for e in events}
    trees = []
    for e in events:
        kids = by_parent.get(e["event_id"])
        if kids:
            trees.append({"root": e, "children": sorted(kids, key=lambda x: x["timestamp"])})
    linked = sum(1 for e in events if e.get("parent_event_id") in ids)
    narrate("observe", f"{len(events)} events · {len(trees)} trees · {linked} causally-linked")

    # A representative event to show the typed anatomy.
    sample = next((e for e in events if e["flow_type"] == "collaboration" and e.get("parent_event_id") is None), events[0])

    # Flush persistence and read the gzip-JSONL file it wrote.
    try:
        obs.stop()
    except Exception:
        pass
    persistence = _read_persisted(_LOG_DIR)
    persistence["note"] = ("Written by the EnhancedObservationManager's gzip-JSONL export. "
                           "Honest scope: this is a separate write/export path — the analytics "
                           "above read the in-memory event bus, not this file.")
    persisted_events = sum(f["events"] for f in persistence["files"])
    narrate("persist", f"{persisted_events} events → {len(persistence['files'])} gzip-JSONL file(s)")

    return {
        "title": "XenoComm E1 observability",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "summary": {
            "events": len(events),
            "causal_trees": len(trees),
            "linked_events": linked,
            "flow_types": len({e["flow_type"] for e in events}),
            "agents": len(AGENTS),
            "persisted_events": persisted_events,
        },
        "causal_trees": trees,
        "sample_event": sample,
        "flow_metrics": analytics.get("flow_metrics", {}),
        "agent_metrics": analytics.get("agent_metrics", {}),
        "throughput_trend": analytics.get("throughput_trend"),
        "error_summary": analytics.get("error_summary", {}),
        "anomalies": analytics.get("anomalies", []),
        "alerts": analytics.get("alerts"),
        "persistence": persistence,
        "events": events,
    }


def main() -> int:
    data = run()
    out = _HERE / "run_e1.json"
    out.write_text(json.dumps(data, indent=2))
    s = data["summary"]
    print("-" * 40)
    print(f"  {s['events']} events · {s['causal_trees']} causal trees · "
          f"{s['linked_events']} linked · {s['flow_types']} flow types · "
          f"{s['persisted_events']} persisted")
    print(f"  wrote {out}")
    print("  open demo/observability.html to explore it\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

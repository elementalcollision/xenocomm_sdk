#!/usr/bin/env python3
"""XenoComm coordination PoC — a runnable, end-to-end proof of concept.

Three independent agents (labeled as three different vendors) coordinate through
the XenoComm broker. They register, align, negotiate a protocol, and exchange
work — one exchange fails, tripping an anomaly signal. Observing the failure, an
LLM proposes a protocol improvement, which is then voted on, human-approved, and
canary-deployed under governance. Every step is captured as a typed flow event.

This exercises the actual work shipped this session:
  * Track B coordination  — register / collaborate / negotiate (real server tools)
  * E1 observability      — one typed, cross-agent flow-event stream + analytics
  * M1 P1 governance      — vote + human approval gate + canary + audit
  * M1 P2 LLM proposer    — OpenRouter (deepseek/deepseek-v4-flash) if a key is set

It drives the REAL server tools (the exact path an MCP client hits). The LLM step
uses OpenRouter when OPENROUTER_API_KEY is available and otherwise falls back to a
clearly-labeled deterministic stub, so the demo runs fully offline.

Output: writes demo/run.json (consumed by dashboard.html) and narrates to stdout.
Run:  mcp_server/.venv/bin/python demo/coordination_poc.py
"""

from __future__ import annotations

import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))  # make xenocomm_mcp importable

from xenocomm_mcp.envfile import load_env_files  # noqa: E402

load_env_files()

import xenocomm_mcp.server as srv  # noqa: E402  (importing wires the real stack)
from xenocomm_mcp.observation import EventSeverity  # noqa: E402

AGENTS = [
    {"id": "planner-claude", "vendor": "anthropic", "domains": ["planning", "shared"]},
    {"id": "executor-deepseek", "vendor": "deepseek", "domains": ["execution", "shared"]},
    {"id": "reviewer-gpt", "vendor": "openai", "domains": ["review", "shared"]},
]


def narrate(step: str, msg: str) -> None:
    print(f"  [{step:<9}] {msg}")


def _events_list(payload) -> list:
    if isinstance(payload, dict):
        return payload.get("events", payload.get("recent_events", []))
    return payload or []


def run() -> dict:
    a, b, c = (x["id"] for x in AGENTS)
    print("\nXenoComm coordination PoC — three agents, one broker\n" + "-" * 52)

    # 1. Register three cross-vendor agents with the broker.
    for ag in AGENTS:
        srv.register_agent(agent_id=ag["id"], knowledge_domains=ag["domains"],
                           capabilities={"vendor": ag["vendor"]})
    narrate("register", f"{a} (anthropic), {b} (deepseek), {c} (openai) joined the broker")

    # 2. Two of them open a collaboration (alignment is computed).
    collab = srv.initiate_collaboration(a, b, required_alignment_score=0.0)
    narrate("collab", f"session {str(collab.get('session_id'))[:8]} — state={collab.get('state')}")

    # 3. They negotiate a coordination protocol.
    neg = srv.initiate_negotiation(a, b, {"data_format": "json", "compression": "gzip", "batch_size": 32})
    nsid = neg.get("session_id")
    resp = srv.respond_to_negotiation(nsid, b, "accept")
    narrate("negotiate", f"session {str(nsid)[:8]} — {resp.get('state')} (batch_size=32, gzip)")

    # 4. They exchange work — then the negotiated protocol degrades: the gzip'd
    #    32-message batches start timing out, tripping the anomaly detector.
    obs = srv._obs_manager
    for i in range(4):
        obs.negotiation_sensor.emit(
            "message_exchanged", f"exchange {i} delivered",
            source_agent=(a if i % 2 == 0 else b), target_agent=(b if i % 2 == 0 else a),
            session_id=nsid, metrics={"latency_ms": 42 + i * 3}, tags=["exchange"])
    for j in range(4):
        obs.negotiation_sensor.emit(
            "exchange_failed", f"batched exchange timed out (attempt {j + 1}) at batch_size=32 under gzip",
            source_agent=b, target_agent=a, session_id=nsid, severity=EventSeverity.ERROR,
            metrics={"latency_ms": 5000, "error": "timeout"}, tags=["exchange", "error"])
    narrate("work", "4 exchanges delivered, then 4 timeouts on the executor → degradation")

    # 5. Observing the degradation, an LLM proposes a protocol improvement. The
    #    current analytics (the flagged anomalies + error summary) are passed as
    #    context, so the proposal is genuinely informed by the failures — a real
    #    LLM via OpenRouter, else a clearly-labeled offline stub.
    snapshot = srv.get_flow_analytics()
    context = {
        "flagged_anomalies": snapshot.get("anomalies"),
        "error_summary": snapshot.get("error_summary"),
        "current_protocol": {"batch_size": 32, "compression": "gzip"},
    }
    goal = ("reduce coordination latency and eliminate the batched-exchange timeout "
            "between two agents exchanging small messages")
    llm_used, variant = False, None
    if os.environ.get("OPENROUTER_API_KEY"):
        try:
            result = srv.propose_variant_via_llm(goal, context=context)
            if isinstance(result, dict) and "error" not in result:
                variant, llm_used = result, True
            else:
                narrate("propose", f"LLM call returned an error, using stub: {result.get('error')}")
        except Exception as exc:  # never let the demo crash on the network
            narrate("propose", f"LLM call raised, using stub: {exc}")
    if not llm_used:
        v = srv.propose_protocol_variant(
            "Lower batch_size and drop compression to cut latency (offline stub proposal)",
            {"batch_size": 4, "compression": "none", "timeout_ms": 200})
        variant = {**v, "source": "stub", "model": "offline-stub",
                   "changes": {"batch_size": 4, "compression": "none", "timeout_ms": 200},
                   "rationale": "Smaller uncompressed batches avoid the timeout. "
                                "(Deterministic stub — set OPENROUTER_API_KEY for a real LLM proposal.)"}
    vid = variant["variant_id"]
    src = f"LLM ({variant.get('model')})" if llm_used else "stub (no key)"
    narrate("propose", f"{src} proposed variant {vid[:8]}: {variant.get('changes')}")

    # 6. Governance: all three agents vote, then a human approves → canary.
    for ag in (a, b, c):
        srv.cast_variant_vote(vid, ag, "for", "improves coordination")
    gov = srv.get_variant_governance(vid)
    narrate("vote", f"tally={gov['tally']['for']}/{gov['tally']['total']} → status={gov['status']}")
    approval = srv.approve_variant(vid, "operator", "approve", "ship the canary")
    narrate("approve", f"human gate → status={approval['status']}")

    # 7. Collect the run: the unified typed stream + analytics + governance record.
    analytics = srv.get_flow_analytics()
    timeline = sorted(_events_list(srv.get_recent_flow_events(count=200)),
                      key=lambda e: e.get("timestamp", ""))
    governance = srv.get_variant_governance(vid)

    per_agent = {}
    for ev in timeline:
        for who in (ev.get("source_agent"), ev.get("target_agent")):
            if not who:
                continue
            m = per_agent.setdefault(who, {"events": 0, "errors": 0})
            m["events"] += 1
            if ev.get("severity") in ("error", "critical"):
                m["errors"] += 1
    agents_out = []
    for ag in AGENTS:
        m = per_agent.get(ag["id"], {"events": 0, "errors": 0})
        rate = round(m["errors"] / m["events"], 3) if m["events"] else 0.0
        agents_out.append({**ag, "events": m["events"], "errors": m["errors"], "error_rate": rate})

    errors = sum(1 for e in timeline if e.get("severity") in ("error", "critical"))
    anomalies = analytics.get("anomalies") or []
    run = {
        "title": "XenoComm coordination PoC",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "llm": {"used": llm_used, "model": variant.get("model"), "goal": goal,
                "informed_by_anomalies": len(context.get("flagged_anomalies") or [])},
        "agents": agents_out,
        "timeline": timeline,
        "analytics": analytics,
        "governance": {
            "variant_id": vid,
            "description": variant.get("description"),
            "changes": variant.get("changes"),
            "rationale": variant.get("rationale"),
            "source": "llm" if llm_used else "stub",
            "status": governance.get("status"),
            "tally": governance.get("tally"),
            "votes": governance.get("votes"),
            "human_decision": governance.get("human_decision"),
            "audit": governance.get("audit"),
        },
        "summary": {
            "agents": len(AGENTS),
            "events": len(timeline),
            "errors": errors,
            "anomalies": len(anomalies),
            "variant_status": governance.get("status"),
            "llm_used": llm_used,
        },
    }
    return run


def main() -> int:
    data = run()
    out = _HERE / "run.json"
    out.write_text(json.dumps(data, indent=2))
    s = data["summary"]
    print("-" * 52)
    print(f"  {s['events']} typed events · {s['errors']} error · {s['anomalies']} anomaly · "
          f"variant {s['variant_status']} · LLM={'yes' if s['llm_used'] else 'stub'}")
    print(f"  wrote {out}")
    print("  open demo/dashboard.html to visualize it\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

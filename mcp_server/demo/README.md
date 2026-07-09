# XenoComm coordination PoC

A runnable, end-to-end proof of concept that exercises the work shipped in the
M1 arc — coordination, observability, governance, and LLM-proposed protocol
evolution — and renders it as a console you can look at.

## The scenario

Three independent agents, labeled as three different vendors
(`planner-claude` / anthropic, `executor-deepseek` / deepseek, `reviewer-gpt` /
openai), coordinate through **one** XenoComm broker:

1. **Register** with the broker.
2. **Collaborate** — a session opens and alignment is computed.
3. **Negotiate** a protocol (`batch_size=32`, `gzip`).
4. **Degrade** — the batched exchanges start timing out, tripping the anomaly
   detector. *(The exchange traffic is simulated — see "What's real / simulated".)*
5. **Propose** — the flagged anomalies are passed to an LLM as context, and it
   proposes a protocol fix.
6. **Vote** — all three agents vote; quorum + ratio are met.
7. **Approve** — a human approves at the gate, and the variant is promoted to
   canary.

Everything is captured as one typed, cross-agent **event stream**.

## What's real / simulated

This drives the **actual server tools** (`register_agent`,
`initiate_collaboration`, `initiate_negotiation`, `cast_variant_vote`,
`approve_variant`, `get_flow_analytics`, …) — the exact path an MCP client hits.
Registration, collaboration, negotiation, the typed event stream, the
threshold-based anomaly detection, and the vote → human-gate → canary governance
all execute against the shipped code. The LLM proposal is **genuinely reactive**:
the flagged anomalies and error summary are passed to the proposer as context. It
comes from **OpenRouter** (`deepseek/deepseek-v4-flash`) when `OPENROUTER_API_KEY`
is set, and otherwise from a clearly-labeled deterministic stub so the demo runs
fully offline.

**What's simulated:** the inter-agent message exchanges (and their timeouts) are
injected as flow events. XenoComm is a coordination + observability broker, **not
a message transport**, so there is no real data plane to carry or fail those
messages — the exchanges stand in for agent traffic so the observability and
governance loop has something real to react to.

**Honest scope** (also stated in the dashboard footer): alignment is lexical,
not semantic; analytics read the in-memory event bus; anomaly detection is
threshold-based.

## Run it

```bash
# from the repo root
mcp_server/.venv/bin/python mcp_server/demo/coordination_poc.py
```

This writes `run.json` and narrates the run to stdout. Set `OPENROUTER_API_KEY`
(or put it in `mcp_server/.env`) for the real LLM proposal.

## See it

```bash
# serve the demo dir so the dashboard loads the fresh run.json
python3 -m http.server 8099 --directory mcp_server/demo
# then open http://localhost:8099/dashboard.html
```

`dashboard.html` is self-contained: opened directly (or as a published
Artifact) it renders the committed `run.json` embedded in the page; served over
HTTP it fetches the latest `run.json` instead. Regenerate `run.json` any time by
re-running the script.

---

# E1 observability deep-dive

A second console (`observability.html`) zooms in on the observability substrate
wired in by E1 (PR #6): the typed `FlowEvent` bus, causal linking via
`parent_event_id`, the analytics engine, and gzip-JSONL persistence.

`e1_observability_poc.py` drives several real collaborations — each
`initiate_collaboration` emits a `collaboration_initiated` root whose alignment
and session events inherit it as their `parent_event_id`, forming a real (if
shallow) event tree — plus a little negotiation traffic. It captures the causal
trees, a sample event's full anatomy, per-flow-type and per-agent metrics, the
throughput trend, the error summary, and the gzip-JSONL file it wrote, into
`run_e1.json`.

```bash
mcp_server/.venv/bin/python mcp_server/demo/e1_observability_poc.py
python3 -m http.server 8099 --directory mcp_server/demo   # open /observability.html
```

**Honest scope** (also in that console's footer): causal linking is populated
only at the collaboration boundary (a 2-level tree, not mesh-wide); the analytics
read the in-memory event bus, not the persisted log (they are disjoint); anomaly
detection is threshold-based; message exchanges are simulated.

---

# S1 HTTP-auth walkthrough

A third console (`auth.html`) walks through the security wired in by S1 (PR #8):
the streamable-HTTP transport is stdio-by-default and loopback-by-default, and —
when exposed — every request needs an `Authorization: Bearer` token
(constant-time compared), with a public bind that has no token refused.

`s1_auth_poc.py` runs the shipped code: the auth ladder drives the real
`_BearerAuthASGI` middleware through a Starlette test client (no header / wrong
token → 401; valid token → 200 against a **marker stand-in** app — in production
`_build_http_app` wraps this same middleware around the real MCP app), and the
bind matrix + fail-closed refusal call the real `run_server` (with uvicorn
patched so nothing listens). Captured into `run_s1.json`.

```bash
mcp_server/.venv/bin/python mcp_server/demo/s1_auth_poc.py
python3 -m http.server 8099 --directory mcp_server/demo   # open /auth.html
```

**Honest scope** (also in that console's footer): this is transport-level auth —
this server implements a single shared bearer token, not per-actor identity.

## Files

| File | What it is |
|---|---|
| `coordination_poc.py` | the coordination scenario — drives the real components, emits `run.json` |
| `run.json` | a captured coordination run (the dashboard's data) |
| `dashboard.html` | self-contained coordination console |
| `e1_observability_poc.py` | the observability scenario — emits `run_e1.json` |
| `run_e1.json` | a captured observability run |
| `observability.html` | self-contained observability console |
| `s1_auth_poc.py` | the HTTP-auth walkthrough — emits `run_s1.json` |
| `run_s1.json` | a captured auth run |
| `auth.html` | self-contained auth console |

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

## Files

| File | What it is |
|---|---|
| `coordination_poc.py` | the scenario — drives the real components, emits `run.json` |
| `run.json` | a captured run (the dashboard's data) |
| `dashboard.html` | self-contained console visualizing a run |

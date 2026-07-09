# Testing guide

This repository has two very different test surfaces:

1. the active Python MCP server under `mcp_server/tests/`
2. the archived C++ tree under `legacy/tests/`

For normal product work, the active Python tests are the relevant ones.

## Active test layout

The Python server tests cover the current product surface:

- `test_envfile.py` — dependency-free `.env` parsing and loading
- `test_http_auth.py` — HTTP transport, bearer auth, and fail-closed public binds
- `test_observability.py` — observability wiring, persistence, and causal event parentage
- `test_governance.py` — vote + human gate + canary promotion flow
- `test_llm_proposer.py` — OpenRouter-backed protocol proposals
- `test_kfm_tools.py` — KFM lifecycle tools
- `test_alignment.py` — alignment behavior
- `test_datetime_utc.py` — timezone handling for timestamps
- `test_ship_blockers.py` and `test_adjacent_fixes.py` — broader behavior checks around product readiness

## What to run when changing common areas

### Startup, config, auth, or environment loading
Run the environment and transport tests first:

- `pytest mcp_server/tests/test_envfile.py`
- `pytest mcp_server/tests/test_http_auth.py`

### Observability, persistence, or analytics
Run the observability tests:

- `pytest mcp_server/tests/test_observability.py`

### Governance or emergence behavior
Run the governance and LLM proposal tests:

- `pytest mcp_server/tests/test_governance.py`
- `pytest mcp_server/tests/test_llm_proposer.py`

### KFM lifecycle tools
Run the lifecycle test file:

- `pytest mcp_server/tests/test_kfm_tools.py`

### Alignment, negotiation, orchestration, or workflows
Run the relevant domain tests in `mcp_server/tests/` and check the nearby code in:

- [`alignment.py`](../../mcp_server/xenocomm_mcp/alignment.py)
- [`negotiation.py`](../../mcp_server/xenocomm_mcp/negotiation.py)
- [`orchestrator.py`](../../mcp_server/xenocomm_mcp/orchestrator.py)
- [`workflows.py`](../../mcp_server/xenocomm_mcp/workflows.py)

## When a test failure matters

In this repository, test failures often indicate a product boundary change rather than a local bug. Watch especially for regressions in:

- HTTP auth being bypassable
- `.env` values not loading under the launcher interpreter
- governance bypasses that let variants reach canary without a vote + human approval
- observability events losing `parent_event_id` or persistence flushing
- any change that causes legacy code to look like the active product

## Legacy tests

The archived C++ tree under `legacy/tests/` is preserved for reference, but it is not the primary test surface for day-to-day product work. Do not assume those tests reflect the behavior of the active MCP server.

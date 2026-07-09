# MCP server architecture

The active product is the Python MCP server in [`mcp_server/xenocomm_mcp/`](../../mcp_server/xenocomm_mcp/). It packages the coordination engines behind FastMCP tools and is the code that actually ships.

## What the server does

At startup, [`server.py`](../../mcp_server/xenocomm_mcp/server.py) performs the following high-level wiring:

- loads environment files with the dependency-free loader in [`envfile.py`](../../mcp_server/xenocomm_mcp/envfile.py)
- creates the FastMCP app
- installs an [`EnhancedObservationManager`](../../mcp_server/xenocomm_mcp/analytics.py) as the process-wide observation singleton
- wraps the orchestrator, negotiation engine, emergence engine, and workflow manager with instrumented variants
- attaches the governance layer in front of protocol emergence
- creates the LLM proposal helper for OpenRouter-backed variant generation
- exposes MCP tools for alignment, negotiation, emergence, governance, KFM lifecycle management, dashboards, analytics, and a few utility flows

The server is deliberately not a thin transport shim: it is the product boundary where coordination logic, observability, and policy enforcement meet.

## Main domain modules

### Alignment
[`alignment.py`](../../mcp_server/xenocomm_mcp/alignment.py) defines the agent context model and the alignment strategies used to compare two agents. The server exposes tools such as `register_agent`, `verify_knowledge_alignment`, `verify_goal_alignment`, and the composite alignment check.

### Negotiation
[`negotiation.py`](../../mcp_server/xenocomm_mcp/negotiation.py) implements the negotiation state machine and compatibility logic for communication parameters.

### Emergence and rollback
[`emergence.py`](../../mcp_server/xenocomm_mcp/emergence.py) models protocol variants, canary deployment, circuit breaking, and rollback. This is the low-level engine behind protocol evolution.

### Governance
[`governance.py`](../../mcp_server/xenocomm_mcp/governance.py) enforces the vote-plus-human-gate rule before a proposed variant can reach canary. The governance layer is intentionally separate from the emergence engine so the engine remains usable in tests and lower-level workflows.

### LLM-proposed variants
[`llm_proposer.py`](../../mcp_server/xenocomm_mcp/llm_proposer.py) uses OpenRouter to generate a candidate variant description and change set, then submits that proposal into the governance pipeline. The design is defensive: lazy client creation, stdlib-only HTTP, and clear failure modes.

### Orchestration and workflows
[`orchestrator.py`](../../mcp_server/xenocomm_mcp/orchestrator.py) coordinates alignment, negotiation, and emergence into collaboration sessions. [`workflows.py`](../../mcp_server/xenocomm_mcp/workflows.py) provides scripted multi-step flows for onboarding, protocol evolution, error recovery, conflict resolution, and related coordination tasks.

### Observability and analytics
[`observation.py`](../../mcp_server/xenocomm_mcp/observation.py) defines `FlowEvent`, causal scoping, and the in-memory event bus. [`analytics.py`](../../mcp_server/xenocomm_mcp/analytics.py) layers in persistence, event export, per-flow analytics, anomaly flagging, and the enhanced observation manager.

### KFM lifecycle
[`kfm_lifecycle.py`](../../mcp_server/xenocomm_mcp/kfm_lifecycle.py) tracks the Kill/Fuck/Marry lifecycle state for agents, variants, and related entities. The server exposes these scores and lifecycle actions as MCP tools because they became part of the product surface rather than just internal bookkeeping.

### Integrations
[`integrations.py`](../../mcp_server/xenocomm_mcp/integrations.py) defines structural bridges for OpenClaw and Claude-Flow style systems. These are integration adapters and observability hooks, not the core transport.

## Why this architecture exists

The current design reflects the product's main thesis: coordination should be exposed as an in-band, inspectable MCP surface rather than hidden in a private orchestrator. That is why the observation plane, analytics, and governance are first-class modules instead of downstream logs or dashboards.

The recent commit history shows the shape of this evolution:

- HTTP transport authentication was added and then hardened to fail closed for public binds.
- `.env` loading was made dependency-free so secrets work under the interpreter that launches the server.
- Observability was wired into the running product, not left as a detached subsystem.
- Governance and LLM proposal generation were added so protocol emergence cannot bypass human approval.
- KFM lifecycle management was exposed as MCP tools.

Those are the areas most likely to matter when changing behavior in `server.py`.

## Extension points and watch-outs

- **Startup order matters.** Observation is initialized before the orchestrator so the process-wide singleton is shared consistently.
- **Governance is enforced at the tool surface.** Do not assume the raw engine methods are gated; the gate is in the server tools.
- **Persistence is optional.** Analytics still run in-memory when `XENOCOMM_FLOW_LOG_DIR` is unset.
- **LLM proposals are non-essential.** The server should continue to boot and function without OpenRouter credentials.
- **The integration layer is structural.** It demonstrates how to ingest external events into observability, but it is not the same thing as a production connector.

## Source map

Primary files to inspect first when editing the active product:

- [`mcp_server/xenocomm_mcp/server.py`](../../mcp_server/xenocomm_mcp/server.py)
- [`mcp_server/xenocomm_mcp/observation.py`](../../mcp_server/xenocomm_mcp/observation.py)
- [`mcp_server/xenocomm_mcp/analytics.py`](../../mcp_server/xenocomm_mcp/analytics.py)
- [`mcp_server/xenocomm_mcp/governance.py`](../../mcp_server/xenocomm_mcp/governance.py)
- [`mcp_server/xenocomm_mcp/llm_proposer.py`](../../mcp_server/xenocomm_mcp/llm_proposer.py)
- [`mcp_server/xenocomm_mcp/workflows.py`](../../mcp_server/xenocomm_mcp/workflows.py)
- [`mcp_server/xenocomm_mcp/integrations.py`](../../mcp_server/xenocomm_mcp/integrations.py)
- [`mcp_server/xenocomm_mcp/kfm_lifecycle.py`](../../mcp_server/xenocomm_mcp/kfm_lifecycle.py)

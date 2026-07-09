# OpenWiki quickstart

XenoComm SDK has two very different stories:

1. **The active product** is the pure-Python MCP server in [`mcp_server/`](../mcp_server/).
2. **The original C++ SDK** is archived under [`legacy/`](../legacy/) and explicitly treated as dormant reference code.

The repository's current center of gravity is the MCP server: it exposes alignment verification, protocol negotiation, emergence/governance, observability, KFM lifecycle tools, and a few integration stubs to MCP clients such as Claude Code, Cursor, and OpenClaw-style systems.

## Start here

- [MCP server architecture](architecture/mcp-server.md) — the active Python product, its modules, and the tool families it exposes.
- [Runtime and configuration](operations/runtime-and-config.md) — how the server starts, how `.env` loading works, HTTP auth, and persistence behavior.
- [Testing guide](testing.md) — what to run when changing alignment, observability, governance, transport, or config.
- [Legacy C++ SDK](legacy/cpp-sdk.md) — archived status, why it was frozen, and the known defects that make it unsafe to revive casually.

## Repository at a glance

### Active surface

The shipping package is `xenocomm-mcp` as defined in [`mcp_server/pyproject.toml`](../mcp_server/pyproject.toml). Its entrypoint is [`mcp_server/xenocomm_mcp/__main__.py`](../mcp_server/xenocomm_mcp/__main__.py), which routes to the MCP server by default and also exposes a small CLI surface for dashboards, demos, and inspection commands.

Important runtime modules:

- [`server.py`](../mcp_server/xenocomm_mcp/server.py) wires the FastMCP server, observation manager, governance gate, and LLM proposal path.
- [`observation.py`](../mcp_server/xenocomm_mcp/observation.py) defines typed flow events and the in-memory event bus.
- [`analytics.py`](../mcp_server/xenocomm_mcp/analytics.py) adds persistence, analytics, and anomaly flags.
- [`alignment.py`](../mcp_server/xenocomm_mcp/alignment.py), [`negotiation.py`](../mcp_server/xenocomm_mcp/negotiation.py), and [`emergence.py`](../mcp_server/xenocomm_mcp/emergence.py) are the core coordination engines.
- [`governance.py`](../mcp_server/xenocomm_mcp/governance.py) and [`llm_proposer.py`](../mcp_server/xenocomm_mcp/llm_proposer.py) implement the guarded protocol-evolution flow.
- [`kfm_lifecycle.py`](../mcp_server/xenocomm_mcp/kfm_lifecycle.py) adds the KFM lifecycle engine exposed as MCP tools.
- [`integrations.py`](../mcp_server/xenocomm_mcp/integrations.py) and [`workflows.py`](../mcp_server/xenocomm_mcp/workflows.py) collect higher-level orchestration and integration patterns.

### Legacy surface

The root [`README.md`](../README.md) and [`LEGACY.md`](../LEGACY.md) are clear that the original C++ SDK has been archived and is not part of the shipping product. If you need to reason about it, use the legacy page below rather than the active-server docs.

## Documentation map

- [MCP server architecture](architecture/mcp-server.md)
- [Runtime and configuration](operations/runtime-and-config.md)
- [Testing guide](testing.md)
- [Legacy C++ SDK](legacy/cpp-sdk.md)

## Before changing code

If you are a future coding agent:

1. Read this page first.
2. Read the architecture page for the module you plan to touch.
3. Read the runtime/config page if your change affects startup, environment variables, HTTP auth, or logging/persistence.
4. Read the testing guide and run the smallest relevant subset of tests first.

The repository has been in active MCP-server development recently, with key changes around HTTP auth, `.env` loading, governance, LLM variant proposals, observability wiring, and the KFM lifecycle tools. Those areas are the highest-risk places to edit without re-checking the current source.

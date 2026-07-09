# Runtime and configuration

This page captures the operational behavior of the active MCP server: how it starts, how it reads configuration, what the HTTP transport does, and how observability persistence works.

## Entrypoints

The package entrypoint is [`mcp_server/xenocomm_mcp/__main__.py`](../../mcp_server/xenocomm_mcp/__main__.py). It supports two broad modes:

- default server mode, which starts the MCP server
- a small subcommand-oriented CLI surface for dashboards, demos, stats, analytics, and research-style workflows

The package metadata in [`mcp_server/pyproject.toml`](../../mcp_server/pyproject.toml) registers `xenocomm-mcp` as the console script.

## Server startup

[`server.py`](../../mcp_server/xenocomm_mcp/server.py) is responsible for server startup and process-wide wiring. It:

- loads environment files before reading runtime configuration
- creates the FastMCP server
- initializes the enhanced observation manager
- registers `atexit` flushing for persistence
- builds the HTTP application wrapper when streamable HTTP transport is requested

The active server uses the `stdio` transport by default, which is the normal mode for MCP clients such as Claude Code.

## Environment loading

The server does **not** depend on `python-dotenv`. Instead it uses [`envfile.py`](../../mcp_server/xenocomm_mcp/envfile.py), a stdlib-only loader that reads:

1. the explicit path in `XENOCOMM_ENV_FILE`, if set
2. `mcp_server/.env`
3. `./.env` in the current working directory

The loader preserves existing environment variables unless override is explicitly requested. This is important because the server is often launched under an interpreter that has runtime dependencies but not optional packaging helpers.

Operationally, the main non-sensitive variables to know about are:

- `XENOCOMM_ENV_FILE`
- `XENOCOMM_HTTP_TOKEN`
- `XENOCOMM_FLOW_LOG_DIR`
- `OPENROUTER_API_KEY`
- `OPENROUTER_MODEL`
- `OPENROUTER_BASE_URL`

Do **not** document live secret values.

## HTTP transport and auth

The HTTP transport is streamable HTTP, not a separate web application.

Important behavior, backed by tests in [`mcp_server/tests/test_http_auth.py`](../../mcp_server/tests/test_http_auth.py):

- loopback binds are allowed without a token
- a bearer token can wrap the HTTP app
- binding to a public host without `XENOCOMM_HTTP_TOKEN` fails closed
- the token is checked with a simple `Authorization: Bearer <token>` header

This is a safety boundary worth preserving when making transport changes.

## Observability persistence

[`analytics.py`](../../mcp_server/xenocomm_mcp/analytics.py) can persist flow events to gzip-compressed JSONL files when `XENOCOMM_FLOW_LOG_DIR` is set. The behavior to remember:

- analytics still run in memory even when persistence is off
- disk output is buffered and flushed on stop/shutdown
- file names are rotated with a timestamped `flows_*.jsonl.gz` pattern
- persistence is off by default

The wiring in [`server.py`](../../mcp_server/xenocomm_mcp/server.py) registers a stop hook so short-lived runs still flush the tail of the buffer.

## OpenRouter-backed variant proposals

The LLM proposal path in [`llm_proposer.py`](../../mcp_server/xenocomm_mcp/llm_proposer.py) is intentionally optional:

- it creates the client lazily
- it uses stdlib networking instead of a hard dependency on a separate client library
- missing credentials or bad completions should return tool-level errors, not crash the process

That makes the proposal path useful but not required for the server to function.

## What to check when changing runtime behavior

If you touch startup, auth, or env loading, check:

- `mcp_server/tests/test_envfile.py`
- `mcp_server/tests/test_http_auth.py`
- the relevant server bootstrap code in `server.py`

If you touch persistence, check:

- `mcp_server/tests/test_observability.py`
- the persistence-related branches in `analytics.py` and `server.py`

If you touch the LLM proposal path, inspect:

- `mcp_server/tests/test_llm_proposer.py`
- `llm_proposer.py`
- the environment-loading behavior that supplies credentials

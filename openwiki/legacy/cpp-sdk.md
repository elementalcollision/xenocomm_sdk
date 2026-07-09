# Legacy C++ SDK

The original XenoComm codebase was a C++ SDK focused on transport, negotiation, encodings, and extensibility. It now lives under [`legacy/`](../../legacy/) and is explicitly treated as archived, dormant reference code.

## Status

The root [`LEGACY.md`](../../LEGACY.md) is the canonical status note. It says the archived C++ SDK is:

- not built
- not linked by the shipping product
- not shipped
- unmaintained

The active product is the Python MCP server in [`mcp_server/`](../../mcp_server/).

## What the legacy tree contains

The archived tree still includes the old C++ source layout, Python bindings, examples, and tests:

- `legacy/include/xenocomm/` and `legacy/src/` for the C++ core
- `legacy/bindings/python/` for the old Python bindings
- `legacy/examples/` for example programs
- `legacy/tests/` for the archived test suites

The top-level README still mentions the old transport, encoding, and extensibility story, but the current shipping docs no longer treat that code as active.

## Why it was archived

The important reason is not just that the product moved on; it is that the C++ path is now separated from the shipping server and carries documented defects. `LEGACY.md` records several verified problems, including:

- the security layer returning plaintext instead of ciphertext on a code path that looks like encryption
- a broken FSK modem implementation
- a Reed-Solomon path that is effectively a stub

That makes the archived code unsuitable as a casual dependency or example of current behavior.

## Historical decision trail

Recent docs and git history show the archiving decision and the follow-up evaluation:

- commit `6d36049` moved the dormant C++ SDK under `legacy/`
- commit `14a9ebb` added the native-transport handoff brief
- commit `2a0fdb4` added the validation brief for deciding whether constrained transport is a real target
- commit `f3c8020` added the one-pager that says the default remains archived

The message is consistent across those artifacts: the archived code stays archived unless a real external demand signal justifies revisiting it.

## What future agents should do with it

Treat this tree as historical context, not as the source of truth for the active product.

If you need to edit anything here, do so only after checking whether the change is really about preservation, documentation, or a deliberate revival effort. For normal product work, the Python server is the place to change.

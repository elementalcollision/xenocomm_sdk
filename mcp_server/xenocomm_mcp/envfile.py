"""Dependency-free ``.env`` loading — no python-dotenv required.

The MCP server is frequently launched (e.g. via .mcp.json / Claude Desktop) under
an interpreter that has the runtime deps (mcp/rich/uvicorn) but NOT python-dotenv.
Relying on python-dotenv to read ``mcp_server/.env`` therefore fails silently in
exactly that setup, so secrets like ``OPENROUTER_API_KEY`` never load. This module
parses ``.env`` with the standard library only, so it works under any interpreter.

Search order (first match per key wins; a real exported env var always wins
unless ``override=True``):
  1. ``$XENOCOMM_ENV_FILE`` (explicit path, if set)
  2. ``mcp_server/.env`` (relative to this package)
  3. ``./.env`` (the current working directory)
"""

from __future__ import annotations

import os
from pathlib import Path


def _candidate_paths() -> list[Path]:
    paths: list[Path] = []
    explicit = os.environ.get("XENOCOMM_ENV_FILE")
    if explicit:
        paths.append(Path(explicit))
    # …/mcp_server/xenocomm_mcp/envfile.py -> …/mcp_server/.env
    paths.append(Path(__file__).resolve().parent.parent / ".env")
    try:
        paths.append(Path.cwd() / ".env")
    except OSError:
        pass  # cwd may have been unlinked; the package-relative path still works
    return paths


def parse_env(text: str) -> dict[str, str]:
    """Parse .env text into a dict. Handles whole-line and inline (unquoted,
    whitespace-preceded) comments, ``export`` prefixes, surrounding quotes, and
    whitespace around ``=`` — matching python-dotenv semantics."""
    out: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[len("export "):].lstrip()
        if "=" not in line:
            continue
        key, _, val = line.partition("=")
        key = key.strip()
        if not key:
            continue
        val = val.strip()
        if len(val) >= 2 and val[0] == val[-1] and val[0] in ("'", '"'):
            val = val[1:-1]  # quoted value kept verbatim (may contain '#')
        else:
            # unquoted: drop a whitespace-preceded inline comment (dotenv parity)
            for _sep in (" #", "\t#"):
                _idx = val.find(_sep)
                if _idx != -1:
                    val = val[:_idx]
            val = val.rstrip()
        out[key] = val
    return out


def load_env_files(override: bool = False) -> list[str]:
    """Load candidate .env files into ``os.environ``.

    Returns the list of file paths actually read. A pre-existing environment
    variable is preserved unless ``override=True``. Never raises.
    """
    loaded: list[str] = []
    seen: set[str] = set()
    try:
        candidates = _candidate_paths()
    except Exception:
        return loaded
    for path in candidates:
        try:
            resolved = path.resolve()
            marker = str(resolved)
            if marker in seen or not resolved.is_file():
                continue
            seen.add(marker)
            values = parse_env(resolved.read_text(encoding="utf-8"))
            # assignment is inside the guard so a bad value (e.g. an embedded NUL
            # rejected by os.environ) skips this file instead of crashing import.
            for key, val in values.items():
                if override or key not in os.environ:
                    os.environ[key] = val
        except Exception:
            continue
        loaded.append(str(resolved))
    return loaded

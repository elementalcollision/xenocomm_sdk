#!/usr/bin/env python3
"""XenoComm S1 HTTP-auth walkthrough — a runnable proof of concept.

Exercises the security wired in by S1 (PR #8): the streamable-HTTP transport is
stdio-by-default, loopback-by-default, and — when exposed — gated by a bearer
token with a constant-time compare, refusing a public bind that has no token.

Everything here runs the SHIPPED code:
  * the auth ladder drives the real ``_BearerAuthASGI`` middleware through a
    Starlette test client (no header / wrong token → 401; valid token → passes);
  * the fail-closed refusal calls the real ``run_server`` and captures the
    SystemExit it raises for a public bind with no token;
  * the bind matrix calls the real ``_is_loopback_host`` + ``run_server`` (with
    uvicorn patched so nothing actually listens) to show what binds and what is
    wrapped.

Honest scope (also in the console footer): this is transport-level auth — a
single shared bearer token, not per-actor identity (MCP has no per-user auth).

Output: writes demo/run_s1.json (consumed by auth.html) + stdout narration.
Run:  mcp_server/.venv/bin/python demo/s1_auth_poc.py
"""

from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE.parent))

import xenocomm_mcp.server as srv  # noqa: E402
from starlette.testclient import TestClient  # noqa: E402

DEMO_TOKEN = "s1-demo-7f3a9c2e5b10"  # a demo token, not a real secret


def narrate(step: str, msg: str) -> None:
    print(f"  [{step:<12}] {msg}")


def _mask(auth: str) -> str:
    if not auth or auth == "(none)":
        return "(none)"
    if auth.startswith("Bearer ") and len(auth) > 14:
        return "Bearer " + auth[7:11] + "•" * 8
    return auth


async def _marker_app(scope, receive, send):
    """A stand-in downstream app that 200s, so pass-through is unambiguous.
    In production ``_build_http_app`` wraps this exact middleware around the real
    MCP streamable app."""
    if scope["type"] == "lifespan":
        while True:
            msg = await receive()
            if msg["type"] == "lifespan.startup":
                await send({"type": "lifespan.startup.complete"})
            elif msg["type"] == "lifespan.shutdown":
                await send({"type": "lifespan.shutdown.complete"})
                return
    elif scope["type"] == "http":
        await send({"type": "http.response.start", "status": 200, "headers": []})
        await send({"type": "http.response.body", "body": b"ok (marker stand-in for the MCP app)"})


def auth_ladder() -> list:
    app = srv._BearerAuthASGI(_marker_app, DEMO_TOKEN)
    cases = [
        ("No token", None),
        ("Wrong token", "Bearer not-the-token"),
        ("Valid token", f"Bearer {DEMO_TOKEN}"),
    ]
    out = []
    with TestClient(app) as client:
        for label, auth in cases:
            headers = {"Authorization": auth} if auth else {}
            r = client.get("/mcp", headers=headers)
            out.append({
                "label": label,
                "request": {"method": "GET", "path": "/mcp",
                            "authorization": _mask(auth or "(none)")},
                "status": r.status_code,
                "body": r.text[:80],
                "allowed": r.status_code != 401,
            })
            narrate("auth", f"{label:<12} → {r.status_code} {'ALLOWED' if r.status_code != 401 else 'blocked'}")
    return out


def fail_closed_demo() -> dict:
    import os
    saved = os.environ.pop("XENOCOMM_HTTP_TOKEN", None)
    try:
        srv.run_server(transport="streamable-http", host="0.0.0.0", port=0)
        return {"host": "0.0.0.0", "token": False, "refused": False, "message": "(did not refuse!)"}
    except SystemExit as exc:
        narrate("fail-closed", "public bind + no token → refused (SystemExit)")
        return {"host": "0.0.0.0", "token": False, "refused": True, "message": str(exc)}
    finally:
        if saved is not None:
            os.environ["XENOCOMM_HTTP_TOKEN"] = saved


def bind_matrix() -> list:
    import os
    import uvicorn
    saved = os.environ.get("XENOCOMM_HTTP_TOKEN")
    calls = []
    orig = uvicorn.run
    uvicorn.run = lambda app, **kw: calls.append((app, kw))  # patch so nothing listens
    rows = []
    try:
        # loopback, no token -> binds, unwrapped (warns)
        os.environ.pop("XENOCOMM_HTTP_TOKEN", None)
        calls.clear()
        srv.run_server(transport="streamable-http", host="127.0.0.1", port=8000)
        rows.append({"host": "127.0.0.1", "loopback": True, "token": False,
                     "outcome": "binds (loopback) — warns: no auth",
                     "wrapped": bool(calls) and isinstance(calls[-1][0], srv._BearerAuthASGI)})
        # public, no token -> refused
        try:
            srv.run_server(transport="streamable-http", host="0.0.0.0", port=8000)
            rows.append({"host": "0.0.0.0", "loopback": False, "token": False,
                         "outcome": "(did not refuse!)", "wrapped": None})
        except SystemExit:
            rows.append({"host": "0.0.0.0", "loopback": False, "token": False,
                         "outcome": "REFUSED — fail closed", "wrapped": None})
        # public, with token -> binds, wrapped
        os.environ["XENOCOMM_HTTP_TOKEN"] = DEMO_TOKEN
        calls.clear()
        srv.run_server(transport="streamable-http", host="0.0.0.0", port=8000)
        rows.append({"host": "0.0.0.0", "loopback": False, "token": True,
                     "outcome": "binds — auth-wrapped",
                     "wrapped": bool(calls) and isinstance(calls[-1][0], srv._BearerAuthASGI)})
    finally:
        uvicorn.run = orig
        if saved is None:
            os.environ.pop("XENOCOMM_HTTP_TOKEN", None)
        else:
            os.environ["XENOCOMM_HTTP_TOKEN"] = saved
    narrate("bind-matrix", f"{len(rows)} host/token combinations evaluated")
    return rows


def run() -> dict:
    print("\nXenoComm S1 HTTP-auth walkthrough\n" + "-" * 40)
    loopback = [{"host": h, "loopback": srv._is_loopback_host(h)}
                for h in ("127.0.0.1", "::1", "localhost", "0.0.0.0", "192.168.1.5", "example.com")]
    narrate("register", "auth middleware + run_server under test")
    ladder = auth_ladder()
    matrix = bind_matrix()
    failclosed = fail_closed_demo()
    return {
        "title": "XenoComm S1 HTTP auth",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "posture": {
            "default_transport": "stdio",
            "http_default_host": "127.0.0.1 (loopback)",
            "auth": "bearer token (XENOCOMM_HTTP_TOKEN)",
            "constant_time_compare": True,
            "fail_closed": True,
        },
        "auth_ladder": ladder,
        "bind_matrix": matrix,
        "fail_closed": failclosed,
        "loopback_hosts": loopback,
        "token_display": _mask(f"Bearer {DEMO_TOKEN}"),
    }


def main() -> int:
    data = run()
    out = _HERE / "run_s1.json"
    out.write_text(json.dumps(data, indent=2))
    blocked = sum(1 for c in data["auth_ladder"] if not c["allowed"])
    print("-" * 40)
    print(f"  auth ladder: {blocked}/{len(data['auth_ladder'])} requests blocked · "
          f"public-bind-without-token refused: {data['fail_closed']['refused']}")
    print(f"  wrote {out}")
    print("  open demo/auth.html to explore it\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

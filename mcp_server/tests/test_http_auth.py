"""S1 — the HTTP transport starts (no more mcp.run(port=) TypeError) and is
authenticated: a bearer token is required when configured, and a public bind
without a token is refused (fail closed)."""

import pytest
from starlette.testclient import TestClient

import xenocomm_mcp.server as srv


async def _ok_app(scope, receive, send):
    """Minimal ASGI app that 200s and handles the lifespan protocol."""
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
        await send({"type": "http.response.body", "body": b"ok"})


def test_bearer_wrapper_enforces_token():
    app = srv._BearerAuthASGI(_ok_app, "secret")
    with TestClient(app) as client:
        assert client.get("/").status_code == 401  # no header
        assert client.get(
            "/", headers={"Authorization": "Bearer wrong"}
        ).status_code == 401
        r = client.get("/", headers={"Authorization": "Bearer secret"})
        assert r.status_code == 200 and r.text == "ok"


def test_build_http_app_wraps_only_with_token():
    assert isinstance(srv._build_http_app("t"), srv._BearerAuthASGI)
    assert not isinstance(srv._build_http_app(None), srv._BearerAuthASGI)


def test_is_loopback_host():
    for h in ("127.0.0.1", "::1", "localhost", ""):
        assert srv._is_loopback_host(h)
    for h in ("0.0.0.0", "192.168.1.5", "example.com"):
        assert not srv._is_loopback_host(h)


def test_run_server_refuses_public_bind_without_token(monkeypatch):
    monkeypatch.delenv("XENOCOMM_HTTP_TOKEN", raising=False)
    with pytest.raises(SystemExit):
        srv.run_server(transport="streamable-http", host="0.0.0.0", port=0)


def test_run_server_binds_when_authorized(monkeypatch):
    """Loopback (no token) and public (with token) both proceed to bind; the
    public bind is auth-wrapped. uvicorn.run is patched so nothing listens."""
    import uvicorn

    calls = []
    monkeypatch.setattr(uvicorn, "run", lambda app, **kw: calls.append((app, kw)))

    monkeypatch.delenv("XENOCOMM_HTTP_TOKEN", raising=False)
    srv.run_server(transport="streamable-http", host="127.0.0.1", port=1234)

    monkeypatch.setenv("XENOCOMM_HTTP_TOKEN", "secret")
    srv.run_server(transport="streamable-http", host="0.0.0.0", port=1234)

    assert len(calls) == 2
    assert calls[0][1] == {"host": "127.0.0.1", "port": 1234}
    assert calls[1][1] == {"host": "0.0.0.0", "port": 1234}
    assert not isinstance(calls[0][0], srv._BearerAuthASGI)  # loopback, no token
    assert isinstance(calls[1][0], srv._BearerAuthASGI)       # public, wrapped

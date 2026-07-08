"""Tests for M1 P2 — LLM-proposed variants via OpenRouter.

All tests use an injected fake client: no network, no tokens spent. They cover
JSON parsing robustness, validation/graceful failure, and the tie into the P1
governance gate (an LLM-proposed variant is governed like any other).
"""

import uuid

import pytest

from xenocomm_mcp.emergence import EmergenceEngine, VariantStatus
from xenocomm_mcp.governance import VariantGovernance, GovernanceConfig, GovernanceStatus
from xenocomm_mcp.llm_proposer import (
    LLMVariantProposer, LLMProposerError, OpenRouterClient, _extract_json,
)


class FakeClient:
    def __init__(self, content, model="fake/model"):
        self._content = content
        self.model = model
        self.calls = []

    def chat(self, messages, **kw):
        self.calls.append((messages, kw))
        if isinstance(self._content, Exception):
            raise self._content
        return self._content


def _uid(p):
    return f"{p}-{uuid.uuid4().hex[:8]}"


def _proposer(content, config=None):
    engine = EmergenceEngine()
    gov = VariantGovernance(engine, None, config=config or GovernanceConfig())
    prop = LLMVariantProposer(engine, gov, client=FakeClient(content))
    return engine, gov, prop


GOOD = '{"description": "batch small messages", "changes": {"batch_size": 16}, "rationale": "fewer round trips"}'


# -- JSON parsing robustness ------------------------------------------------

def test_parses_clean_json():
    assert _extract_json(GOOD)["changes"] == {"batch_size": 16}


def test_parses_markdown_fenced_json():
    fenced = f"```json\n{GOOD}\n```"
    assert _extract_json(fenced)["description"] == "batch small messages"


def test_parses_json_with_surrounding_prose():
    noisy = f"Sure! Here is my proposal:\n{GOOD}\nHope that helps."
    assert _extract_json(noisy)["changes"]["batch_size"] == 16


def test_non_json_raises():
    with pytest.raises(LLMProposerError):
        _extract_json("I cannot help with that.")


def test_json_array_not_object_raises():
    with pytest.raises(LLMProposerError):
        _extract_json("[1, 2, 3]")


# -- proposal + validation --------------------------------------------------

def test_propose_creates_governed_variant():
    engine, gov, prop = _proposer(GOOD)
    res = prop.propose("reduce latency between agents")
    vid = res["variant_id"]
    assert res["source"] == "llm"
    assert res["model"] == "fake/model"
    assert res["changes"] == {"batch_size": 16}
    # created on the engine AND placed under governance (P1 gate)
    assert engine.variants[vid].status is VariantStatus.PROPOSED
    assert gov.get(vid).status is GovernanceStatus.VOTING


def test_missing_goal_raises():
    _, _, prop = _proposer(GOOD)
    with pytest.raises(LLMProposerError):
        prop.propose("   ")


def test_missing_description_raises():
    _, _, prop = _proposer('{"changes": {"x": 1}}')
    with pytest.raises(LLMProposerError):
        prop.propose("goal")


def test_empty_changes_raises():
    _, _, prop = _proposer('{"description": "d", "changes": {}}')
    with pytest.raises(LLMProposerError):
        prop.propose("goal")


def test_changes_not_object_raises():
    _, _, prop = _proposer('{"description": "d", "changes": "gzip"}')
    with pytest.raises(LLMProposerError):
        prop.propose("goal")


def test_client_error_propagates_as_proposer_error():
    _, _, prop = _proposer(LLMProposerError("boom"))
    with pytest.raises(LLMProposerError):
        prop.propose("goal")


# -- client config ----------------------------------------------------------

def test_client_without_key_raises(monkeypatch):
    monkeypatch.delenv("OPENROUTER_API_KEY", raising=False)
    client = OpenRouterClient(api_key=None)
    with pytest.raises(LLMProposerError):
        client.chat([{"role": "user", "content": "hi"}])


def test_default_model_is_deepseek(monkeypatch):
    monkeypatch.delenv("OPENROUTER_MODEL", raising=False)
    assert OpenRouterClient(api_key="x").model == "deepseek/deepseek-v4-flash"


class _FakeResp:
    def __init__(self, read_fn):
        self._read = read_fn

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False

    def read(self, n=-1):
        return self._read(n)


def test_client_wraps_read_errors_as_proposer_error(monkeypatch):
    import xenocomm_mcp.llm_proposer as lp

    def _boom(n=-1):
        raise ConnectionResetError("peer reset mid-read")

    monkeypatch.setattr(lp.urllib.request, "urlopen",
                        lambda *a, **k: _FakeResp(_boom))
    with pytest.raises(LLMProposerError):  # not a raw ConnectionResetError
        OpenRouterClient(api_key="x").chat([{"role": "user", "content": "hi"}])


def test_client_caps_oversized_response(monkeypatch):
    import xenocomm_mcp.llm_proposer as lp
    big = b"{" + b"x" * (lp.MAX_RESPONSE_BYTES + 10)

    monkeypatch.setattr(lp.urllib.request, "urlopen",
                        lambda *a, **k: _FakeResp(lambda n=-1: big[:n] if n and n > 0 else big))
    with pytest.raises(LLMProposerError):
        OpenRouterClient(api_key="x").chat([{"role": "user", "content": "hi"}])


def test_client_tolerates_non_utf8_body(monkeypatch):
    import xenocomm_mcp.llm_proposer as lp
    # invalid utf-8 bytes must not raise UnicodeDecodeError; they become a
    # JSON parse error surfaced as LLMProposerError, never a raw crash.
    monkeypatch.setattr(lp.urllib.request, "urlopen",
                        lambda *a, **k: _FakeResp(lambda n=-1: b"\xff\xfe not json"))
    with pytest.raises(LLMProposerError):
        OpenRouterClient(api_key="x").chat([{"role": "user", "content": "hi"}])


# -- end-to-end into the P1 governance gate ---------------------------------

def test_llm_variant_flows_through_governance_to_canary():
    engine, gov, prop = _proposer(GOOD, config=GovernanceConfig(quorum=2, approval_ratio=0.5))
    vid = prop.propose("improve throughput")["variant_id"]
    gov.cast_vote(vid, "a", "for")
    gov.cast_vote(vid, "b", "for")
    gov.human_approve(vid, "operator", "approve")
    assert engine.variants[vid].status is VariantStatus.CANARY


# -- server tool ------------------------------------------------------------

def test_server_tool_with_injected_client():
    import xenocomm_mcp.server as srv
    srv.llm_proposer._client = FakeClient(GOOD, model="deepseek/deepseek-v4-flash")
    try:
        res = srv.propose_variant_via_llm("cut coordination overhead")
        assert "error" not in res
        assert res["source"] == "llm"
        vid = res["variant_id"]
        assert srv.variant_governance.get(vid).status is GovernanceStatus.VOTING
    finally:
        srv.llm_proposer._client = None


def test_server_tool_returns_graceful_error_on_failure():
    import xenocomm_mcp.server as srv
    srv.llm_proposer._client = FakeClient(LLMProposerError("no key"))
    try:
        res = srv.propose_variant_via_llm("anything")
        assert "error" in res and "hint" in res
    finally:
        srv.llm_proposer._client = None

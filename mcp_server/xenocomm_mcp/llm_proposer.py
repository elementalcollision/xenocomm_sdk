"""M1 P2 — LLM-proposed protocol variants via OpenRouter.

The review noted that "protocol emergence has no generator" — ``propose_variant``
only stored a caller-supplied dict. This module supplies the missing generator:
an LLM is asked to propose a concrete protocol variant (description + a changes
dict) for a stated goal, and the result is fed straight into the **P1 governance
gate** (submitted for votes + human approval before anything reaches canary).

Design constraints:
- **No hard dependency and no cost at import/boot.** Uses stdlib ``urllib``; the
  HTTP client is built lazily and only when a proposal is actually requested.
- **Mockable.** ``LLMVariantProposer`` accepts an injected client so tests never
  touch the network (and never spend tokens).
- **Fails gracefully.** Missing key / HTTP error / unparseable output raise a
  clear ``LLMProposerError`` that the MCP tool turns into an error dict, never a
  crash.
- **Secrets stay out of logs/audit.** Only the model id + variant summary are
  emitted; never the API key or raw completion.
"""

from __future__ import annotations

import http.client
import json
import os
import urllib.error
import urllib.request
from typing import Any

DEFAULT_MODEL = "deepseek/deepseek-v4-flash"
DEFAULT_BASE_URL = "https://openrouter.ai/api/v1"
DEFAULT_TIMEOUT = 30.0
MAX_RESPONSE_BYTES = 1_000_000  # cap the body read; a ~1k-token completion is far smaller


class LLMProposerError(Exception):
    """Raised for any failure in the LLM proposal path (config, HTTP, parsing)."""


class OpenRouterClient:
    """Minimal OpenAI-compatible chat client for OpenRouter (stdlib only)."""

    def __init__(self, api_key: str | None = None, model: str | None = None,
                 base_url: str | None = None, timeout: float | None = None) -> None:
        self.api_key = api_key or os.environ.get("OPENROUTER_API_KEY")
        self.model = model or os.environ.get("OPENROUTER_MODEL", DEFAULT_MODEL)
        self.base_url = (base_url or os.environ.get("OPENROUTER_BASE_URL",
                                                    DEFAULT_BASE_URL)).rstrip("/")
        self.timeout = timeout if timeout is not None else DEFAULT_TIMEOUT

    def chat(self, messages: list[dict[str, str]], temperature: float = 0.4,
             max_tokens: int = 1024, response_json: bool = True) -> str:
        """POST a chat completion and return the assistant message content."""
        if not self.api_key:
            raise LLMProposerError(
                "OPENROUTER_API_KEY is not set; cannot call the LLM. "
                "Export it to enable propose_variant_via_llm.")
        payload: dict[str, Any] = {
            "model": self.model,
            "messages": messages,
            "temperature": temperature,
            "max_tokens": max_tokens,
        }
        if response_json:
            payload["response_format"] = {"type": "json_object"}
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            f"{self.base_url}/chat/completions", data=data, method="POST",
            headers={
                "Authorization": f"Bearer {self.api_key}",
                "Content-Type": "application/json",
                # Optional attribution headers OpenRouter recommends.
                "HTTP-Referer": "https://github.com/elementalcollision/xenocomm_sdk",
                "X-Title": "XenoComm MCP",
            },
        )
        # Read with a size cap and funnel every failure into LLMProposerError so
        # the caller's "never a crash" contract holds. HTTPError is a subclass of
        # OSError, so it must be caught first; read-time faults (ConnectionReset,
        # IncompleteRead) are not URLError subclasses and are caught below.
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read(MAX_RESPONSE_BYTES + 1)
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")[:500] if hasattr(exc, "read") else ""
            raise LLMProposerError(f"OpenRouter HTTP {exc.code}: {detail}") from exc
        except (OSError, http.client.HTTPException) as exc:
            raise LLMProposerError(f"OpenRouter request failed: {exc}") from exc

        if len(raw) > MAX_RESPONSE_BYTES:
            raise LLMProposerError("OpenRouter response exceeded size cap")
        try:
            body = json.loads(raw.decode("utf-8", "replace"))
        except json.JSONDecodeError as exc:
            raise LLMProposerError(f"OpenRouter returned non-JSON: {exc}") from exc

        try:
            return body["choices"][0]["message"]["content"]
        except (KeyError, IndexError, TypeError) as exc:
            raise LLMProposerError(f"Unexpected OpenRouter response shape: {body!r}") from exc


_SYSTEM_PROMPT = (
    "You are a protocol-design assistant for a multi-agent coordination system. "
    "Given a goal, propose ONE concrete protocol variant that could improve agent "
    "coordination. Respond with a single JSON object and nothing else, of the form:\n"
    '{"description": "<one concise sentence>", '
    '"changes": {"<param>": <value>, ...}, '
    '"rationale": "<why this helps>"}\n'
    "`changes` must be a flat JSON object of concrete protocol parameters "
    "(e.g. compression, batch_size, timeout_ms, encoding). Do not include prose "
    "outside the JSON."
)


def _extract_json(text: str) -> dict[str, Any]:
    """Parse a JSON object from a model completion, tolerating markdown fences."""
    s = text.strip()
    if s.startswith("```"):
        # strip ```json ... ``` or ``` ... ``` fences
        s = s.split("```", 2)[1] if s.count("```") >= 2 else s.strip("`")
        if s.lstrip().lower().startswith("json"):
            s = s.lstrip()[4:]
    s = s.strip()
    # fall back to the outermost brace span if there is surrounding text
    if not s.startswith("{"):
        start, end = s.find("{"), s.rfind("}")
        if start == -1 or end == -1 or end <= start:
            raise LLMProposerError(f"No JSON object found in LLM output: {text[:200]!r}")
        s = s[start:end + 1]
    try:
        obj = json.loads(s)
    except json.JSONDecodeError as exc:
        raise LLMProposerError(f"LLM output was not valid JSON: {exc}; got {text[:200]!r}") from exc
    if not isinstance(obj, dict):
        raise LLMProposerError(f"LLM output was not a JSON object: {type(obj).__name__}")
    return obj


class LLMVariantProposer:
    """Generate a governed protocol variant from a goal using an LLM.

    Args:
        emergence_engine: engine whose ``propose_variant`` creates the variant.
        governance: the ``VariantGovernance`` the new variant is submitted to.
        client: an object with ``.chat(messages, ...) -> str`` and a ``.model``
            attribute. Injected in tests; built lazily from env in production.
        observation_manager: optional; used to emit an audit event.
    """

    def __init__(self, emergence_engine: Any, governance: Any,
                 client: Any = None, observation_manager: Any = None) -> None:
        self._engine = emergence_engine
        self._governance = governance
        self._client = client
        self._obs = observation_manager

    def _get_client(self) -> Any:
        if self._client is None:
            self._client = OpenRouterClient()
        return self._client

    def propose(self, goal: str, context: dict[str, Any] | None = None,
                temperature: float = 0.4) -> dict[str, Any]:
        """Ask the LLM for a variant, create it, and submit it to governance."""
        if not goal or not goal.strip():
            raise LLMProposerError("goal is required")
        client = self._get_client()

        user = f"Goal: {goal.strip()}"
        if context:
            user += f"\n\nContext (current metrics/observations):\n{json.dumps(context)[:2000]}"
        messages = [{"role": "system", "content": _SYSTEM_PROMPT},
                    {"role": "user", "content": user}]

        raw = client.chat(messages, temperature=temperature)
        obj = _extract_json(raw)

        description = obj.get("description")
        changes = obj.get("changes")
        rationale = obj.get("rationale", "")
        if not isinstance(description, str) or not description.strip():
            raise LLMProposerError(f"LLM proposal missing a valid 'description': {obj!r}")
        if not isinstance(changes, dict) or not changes:
            raise LLMProposerError(f"LLM proposal 'changes' must be a non-empty object: {obj!r}")

        variant = self._engine.propose_variant(description.strip(), changes)
        # Route straight into the P1 governance gate — an LLM-proposed variant
        # has no special standing; it must earn votes + human approval like any
        # other before it can reach canary.
        self._governance.submit(variant.variant_id, description.strip())
        self._audit(variant.variant_id, description.strip(),
                    getattr(client, "model", DEFAULT_MODEL))

        return {
            "variant_id": variant.variant_id,
            "description": description.strip(),
            "changes": changes,
            "rationale": rationale,
            "source": "llm",
            "model": getattr(client, "model", DEFAULT_MODEL),
            "governance": {"status": "voting",
                           "next": "cast_variant_vote -> approve_variant -> canary"},
        }

    def _audit(self, variant_id: str, description: str, model: str) -> None:
        sensor = getattr(self._obs, "emergence_sensor", None)
        if sensor is None:
            return
        try:
            sensor.emit(
                "llm_variant_proposed",
                f"LLM ({model}) proposed variant: {description}",
                metrics={"variant_id": variant_id, "model": model},
                tags=["emergence", "llm", "proposal"],
            )
        except Exception:
            pass  # audit is observational; never break the proposal path

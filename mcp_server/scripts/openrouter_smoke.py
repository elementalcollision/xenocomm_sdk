#!/usr/bin/env python3
"""One-shot live smoke test for the OpenRouter LLM proposer (M1 P2).

Makes a SINGLE real call to OpenRouter to confirm the round-trip works end to
end: LLM proposes a protocol variant, and it lands under governance in VOTING.

Usage:
    # set your key first (or put it in mcp_server/.env)
    export OPENROUTER_API_KEY=sk-or-...
    python mcp_server/scripts/openrouter_smoke.py ["custom goal"]

Safe to run: if OPENROUTER_API_KEY is not set, it prints instructions and exits
0 without calling anything. It makes exactly one API call (costs a few tokens).
"""

import os
import sys
from pathlib import Path

# Make the package importable when run directly from the repo root.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# Honor mcp_server/.env just like the server does (dependency-free loader).
from xenocomm_mcp.envfile import load_env_files  # noqa: E402
load_env_files()

from xenocomm_mcp.emergence import EmergenceEngine  # noqa: E402
from xenocomm_mcp.governance import VariantGovernance, GovernanceStatus  # noqa: E402
from xenocomm_mcp.llm_proposer import (  # noqa: E402
    LLMVariantProposer, LLMProposerError, DEFAULT_MODEL,
)


def main() -> int:
    if not os.environ.get("OPENROUTER_API_KEY"):
        print("OPENROUTER_API_KEY is not set.")
        print("Set it (or add it to mcp_server/.env) and re-run:")
        print("  export OPENROUTER_API_KEY=sk-or-...")
        return 0

    goal = sys.argv[1] if len(sys.argv) > 1 else (
        "reduce coordination latency between two agents exchanging small messages")
    model = os.environ.get("OPENROUTER_MODEL", DEFAULT_MODEL)
    print(f"Model: {model}")
    print(f"Goal:  {goal}")
    print("Calling OpenRouter (one request)...")

    engine = EmergenceEngine()
    gov = VariantGovernance(engine, None)
    proposer = LLMVariantProposer(engine, gov)

    try:
        result = proposer.propose(goal)
    except LLMProposerError as exc:
        print(f"\nFAILED: {exc}")
        return 1

    vid = result["variant_id"]
    print("\n✅ Round-trip OK")
    print(f"  variant_id:  {vid}")
    print(f"  description: {result['description']}")
    print(f"  changes:     {result['changes']}")
    print(f"  rationale:   {result.get('rationale', '')}")
    print(f"  governance:  {gov.get(vid).status.value} "
          f"(expected: {GovernanceStatus.VOTING.value})")
    ok = gov.get(vid).status is GovernanceStatus.VOTING
    print("\nThe variant is under governance awaiting votes + human approval — "
          "exactly the P1 gate. " + ("PASS" if ok else "UNEXPECTED STATE"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

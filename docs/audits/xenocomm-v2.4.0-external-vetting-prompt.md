# XenoComm v2.4.0 — External Vetting Charge for the Reviewing Fleet

**Audience:** independently-authorized external review agents (code auditors, domain/strategy analysts, and formal/ontology specialists) operating under human-in-the-loop oversight.
**Companion document (the thing you are vetting):** [`xenocomm-review-and-action-plan-2026-07-06.md`](./xenocomm-review-and-action-plan-2026-07-06.md) — an engineering review + adversarial thesis challenge + action plan authored by **Chimera**.
**Target of the review:** `elementalcollision/xenocomm_sdk` @ **HEAD `6993158` (v2.4.0)** — the GitHub `main`.

---

## 0. Who you are and what you are being asked to do

You are **not** being asked to trust Chimera's review. You are being asked to **adversarially vet it.** The review's own author got the source tree wrong on a first pass (audited a diverged, dormant local checkout before re-grounding against the real v2.4.0 HEAD). Treat that as evidence that *this document, too, can be wrong* — and that your job is to find where.

There are **three charges.** Each is designed to be picked up by a different specialist; you may be assigned one or all. Every claim you make — for or against the review — must be **anchored to a `file:line` at HEAD `6993158`** and, wherever the claim is about runtime behavior, **reproduced**, not merely read.

**Meta-principle — interrogate the instrument.** Do not accept "it is implemented" from a docstring, a plausible-looking module, or a passing test that never exercises the path in question. A module named `claude_bridge.py` is not evidence that Claude is called; a `votes` dict is not evidence that anything is voted on; a green test suite is not evidence the broken path runs. Require the real signal — an LLM request actually sent on the wire, an `AttributeError` actually raised, a socket actually answering unauthenticated.

---

## 1. The object under review (orientation)

XenoComm **pivoted**. It began as a C++ SDK for "efficient binary agent-to-agent transport with emergent protocols." v2.4.0 is a **pure-Python MCP coordination server** (`mcp_server/xenocomm_mcp/`, package `xenocomm-mcp`) exposing ~60 `@mcp.tool()` functions over alignment / negotiation / emergence engines, plus an observability stack (event bus, analytics, Rich dashboard) and a KFM (Kill/Fuck/Marry) lifecycle engine.

Chimera's review reaches, in brief:
- The **C++ core is vestigial** — the Python product never builds or links it.
- The **Python MCP product is real and largely well-built**, with a genuinely strong observability layer.
- But **two headline capabilities are overclaimed skeletons**: a "Claude agent bridge for dynamic language evolution" that calls no LLM, and "protocol emergence" with no generator and dead governance.
- The product has **runtime-breaking bugs** and a **docs-vs-reality integrity gap** (the README still sells an abandoned efficiency/opacity thesis the MCP product contradicts).
- **Security:** low-risk Python surface, no committed secrets (verified), but `--http` runs unauthenticated.

Your charges test whether this picture holds, whether a stronger opposing case exists, and what the project *actually is* underneath the naming.

---

## 2. Charge V1 — Verify the findings (adversarial)

**Task.** Re-derive Chimera's factual findings from the source at HEAD `6993158`. For each, assign one verdict and back it with evidence.

- **CONFIRMED** — you reproduced/verified it; the claim holds as stated.
- **IMPRECISE** — the underlying phenomenon is real but the claim mis-states location, scope, severity, or mechanism. Give the correction.
- **REFUTED** — the claim is false; show why.

**The claims to test** (each cites where the review says to look — go read/run it, don't take the citation on faith):

| # | Claim | Where |
|---|---|---|
| C1 | The "Claude bridge" invokes **no LLM** — no `anthropic` import, no `messages.create`, no HTTP client anywhere in `mcp_server/xenocomm_mcp/`; "language evolution" is keyword n-gram counting; content is SHA-256'd and discarded. | `claude_bridge.py:189-239, 426, 559-586` |
| C2 | Pattern-promotion **governance is dead code**: `votes` is written once and never read; promotion is automatic on thresholds; the governance hook is a `pass`. | `claude_bridge.py:271-291, 507, 526-529` |
| C3 | The **C++ core is vestigial** — never built/linked by the product (hatchling build, deps only `mcp`+`rich`, wheel packages only `xenocomm_mcp`; no pybind/`_core`/ctypes import in `mcp_server/`). | `mcp_server/pyproject.toml`; grep `mcp_server/` |
| C4 | `workflows.py` references `alignment.contexts` at **9 sites**; `AlignmentEngine` defines `registered_agents`, not `contexts` → `AttributeError` at runtime on onboarding/negotiation/recovery workflows. | `workflows.py:227,242,243,686,859,860,886,887,893,894`; `alignment.py:200` |
| C5 | `InstrumentedEmergenceEngine.rollback` treats a `RollbackPoint` dataclass / `None` as a dict, breaking `rollback_variant`. | `instrumented.py:327-336` |
| C6 | The orchestrator treats `should_rollback`'s **tuple** return as a bool → rollback always fires. | `orchestrator.py:528, 578` |
| C7 | `run_server` starts FastMCP **streamable-http with no auth** → all ~60 tools reachable unauthenticated over the port. | `server.py:1826` |
| C8 | `emergence.py` **never generates** a variant — `propose_variant` stores a caller-supplied `changes` dict; no evolutionary/generative step exists. | `emergence.py` (`propose_variant`) |
| C9 | The **README contradicts the product** — still advertises "computational efficiency… over human readability" + the C++ SDK, while the shipping product is a standard, human-readable MCP/JSON-RPC server. | `README.md` top; `mcp_server/README` |
| C10 | **No committed secrets** remain (the Perplexity key was scrubbed from history); only placeholders remain in `.env.example`/`.cursor`. | tracked tree; `git log -p` |

**Reproduce where you can.** C4/C5/C6 should be triggered (import the module, call the path, observe the error). C7 should be probed (start the server on `--transport http`, attempt an unauthenticated tool call; use timeouts; do not leave a server running). C1/C3/C10 are grep-decidable against the tree. C2/C8/C9 are read-and-judge. **Read-only: do not modify, commit, or push anything.**

**Deliver:** one row per claim — `{id, verdict, evidence:[file:line], reproduction (command + observed result, or "static"), correction (if IMPRECISE/REFUTED)}` — plus any **finding Chimera missed**.

---

## 3. Charge V2 — Counter-thesis (steelman the project)

**Task.** Chimera's §3 is itself a thesis; attack it. Build the **strongest good-faith case for the v2.4.0 direction** and rebut the review where you can. Do not merely concede — argue.

Address at least:
- **Efficiency thesis (review §3.1).** Chimera calls the "efficiency over readability" thesis abandoned and mis-aimed for cloud LLM agents. Is there a regime — edge, embedded, high-frequency, non-LLM, bandwidth/energy-constrained multi-agent systems — where the original binary-transport thesis is *correct* and the MCP pivot is the mistake? Make that case if it exists.
- **Governed emergence (review §3.2).** Chimera says the observability pivot is right but the governed-emergence promise is unmet. Steelman the current design: is "observed-but-not-yet-governed" a defensible v1, with governance a roadmap item rather than a broken promise?
- **Additive value (review §3.4).** The crux: **is agent-coordination-as-an-MCP-tool genuinely additive over vanilla MCP + a standard orchestration framework?** Is "alignment verification" between heterogeneous agents a real contribution, or a wrapper around embedding/keyword overlap? Name what, if anything, is defensibly unique (candidate: flow-events-as-a-first-class-substrate).
- **The strategic fork (review §4).** Argue for whichever of Chimera's options (A finish-honestly / B make-emergence-real / C archive-C++) you find strongest — or propose a fork Chimera missed.

**Deliver:** `{strongest_case_for_direction, rebuttals:[{review_section, argument, evidence}], concessions:[...], recommended_fork, what_review_got_wrong_strategically}`. Use distinct lenses (product/market, research bet, and a devil's-advocate defense of the shipping code) if working as a group.

---

## 4. Charge V3 — Ontology (what *is* this project?)

**Task.** Establish, formally, what XenoComm v2.4.0 *is* — independent of what it is named or claimed to be — and test that ontology for coherence and **category errors**.

- **Model the entities and relations.** At minimum: `agent`, `capability`, `alignment (context / overlap)`, `protocol`, `variant`, `language pattern`, `intent`, `negotiation session`, `emergence`, `observation / flow event`, `KFM operator (Kill/Fuck/Marry)`, `MCP tool`, `channel`. For each entity: its definition **as backed by code** (`file:line`), and its relations to the others.
- **State the invariants** the system relies on (e.g., "every promoted pattern becomes a tracked variant" — then check whether the code actually maintains it; the review claims this one is *broken* — decoupled).
- **Name the load-bearing conceptual commitments** and rule on each (holds / fails, with evidence). Candidates the review flags as suspect:
  - *That recurrent intent-n-grams constitute a "language."*
  - *That a protocol can be "evolved" with no generative step.*
  - *That a "bridge to Claude" that never calls Claude is meaningfully a Claude bridge.*
  - *That MCP-based agent coordination needs a bespoke alignment/emergence/protocol layer at all.*
  - *(C++ legacy) that "protocol" and "encoding/transport" are the same thing.*
- **Audit for category errors** — places where a name promises an entity of one kind but the code delivers another (telemetry labeled "language," bookkeeping labeled "emergence," a hash labeled "conversation content," etc.).

**Deliver a compact formal model:** `{entities:[{name, code_definition, relations}], invariants:[{statement, holds, evidence}], load_bearing_commitments:[{commitment, verdict, reasoning}], category_errors:[...], overall_ontological_verdict}`.

---

## 5. Rules of engagement (all charges)

1. **Anchor everything to `file:line` at HEAD `6993158`.** No claim without a locatable referent.
2. **Reproduce runtime claims.** Prefer a command + observed output over an argument from reading. Use timeouts; leave no processes running; change nothing on disk.
3. **Interrogate the instrument — including this document and Chimera's review.** Your highest-value output is a place where the review is wrong, imprecise, or missing something.
4. **Read-only.** Do not modify, commit, push, or open PRs against the target repo. You produce a report; humans decide what lands.
5. **Attribution & posture.** This is human-in-the-loop agent work. Attribute your report to your agent persona; **do not** add tool/vendor/"Claude Code"/Anthropic attribution or AI co-author trailers. If two agents co-sign, use the form "Reviewed by <Agent A> and <Agent B>, independently authorized agents."
6. **Return structured findings**, not prose essays, in the shapes specified per charge, so results compose across the fleet.

---

## 6. What "good" looks like

A vetting pass is successful if it returns: a corrected findings table (with at least one verdict Chimera would have to change, if one exists), the strongest surviving counter-thesis, and an ontology sharp enough to decide the strategic fork on. A pass that merely says "confirmed, confirmed, confirmed" has not tried hard enough to refute — say what you *attempted* to break and could not.

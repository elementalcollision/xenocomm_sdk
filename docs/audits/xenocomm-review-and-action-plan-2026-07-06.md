# XenoComm v2.4.0 — Independent Review, Adversarial Thesis Challenge, and Action Plan

**Prepared for:** a fleet of external agents (each Track/ticket is scoped to be picked up independently).
**Reviewer:** Chimera (engineering review), grounded in five parallel code audits and **two external-vetting fleet runs** — a 26-agent adversarial-verification pass and a 35-agent re-run that added a **V0 blind-recon** pass. **Verdict: the review holds** — its spine (the runtime ship-blockers + the integrity/ontology read) survived both runs; the corrections below are folded in.
**Date:** 2026-07-06
**Subject:** `elementalcollision/xenocomm_sdk` @ **HEAD `6993158` (v2.4.0)** — the GitHub `main`, synced locally.

> **Provenance / correction note.** A first pass was conducted against a **stale local checkout** (3 commits, dormant) and reached a "half-built, abandoned C++ SDK" verdict — the wrong tree. This document is grounded in the real v2.4.0 HEAD. *The lesson is baked into the fleet Charter: verify against the real tree, and interrogate the instrument — including this review.* Every claim carries a `file:line`.

> **Vetting addendum (2026-07-06, post-fleet).** This review was adversarially vetted **twice**. Run 1 (26 agents): all 10 headline findings CONFIRMED by dual independent verifiers. Run 2 (35 agents) added a **V0 blind-recon** pass — three scouts who never saw the claim list. **V0 PASSED:** the scouts independently rediscovered the sharpest bugs (including `VariantStatus.FAILED`, the one the *first* pass missed) and contradicted nothing — but they also proved the claim set was **incomplete**, and the verifiers caught **two overreaches in this review**. All corrections are folded in below. Where a finding changed, it is marked **[vetting]**.
> - **+3 new live runtime bugs** (C14 split-registry, C15 dead negotiation, C16 workflow-name) → the shipping product now has **seven** confirmed ship-blockers, not four.
> - **C7 downgraded (latent, not live):** the `--http` surface is unauthenticated *by design*, but `mcp.run(port=…)` raises `TypeError` unconditionally, so the HTTP path **crashes on startup** — the exposure is latent until that is fixed.
> - **Two of my own overreaches, corrected:** (a) the observability "strong part" I praised (gzip-JSONL/analytics/anomaly) is **not wired into the running server** — it uses the *base* `ObservationManager`; the analytics live in an unwired `EnhancedObservationManager`; (b) alignment is **lexical set-overlap/Jaccard, not embedding similarity** — there is no ML in the module.
> - **C++ reframed** from "dead legacy" to **"dormant, partial, defective"** — real compiled codecs, but the flagship FSK modem is numerically broken for its own defaults (109/256 bytes fail) and Reed-Solomon is a hollow stub.

---

## 0. TL;DR

XenoComm has **pivoted**. It began as a C++ SDK for "efficient binary agent-to-agent transport with emergent protocols"; v2.4.0 is a **pure-Python MCP coordination server** (`xenocomm-mcp`, ~65 tools) for the agentic ecosystem (Claude Code, Cursor, OpenClaw). The pivot is **real** and the direction is right — but the product is **half-finished, buggier than it looks, and described by docs that oversell it.**

Six things, all CONFIRMED against source and twice-vetted:
1. **The two headline capabilities are overclaimed skeletons.** The *"Claude agent bridge for dynamic language evolution"* **invokes no LLM** (keyword n-gram counting; content SHA-256'd and discarded) atop **dead governance** (a `votes` dict written once, never read; the promotion hook a bare `pass`); "protocol emergence" has **no generator** (`propose_variant` stores a caller-supplied dict verbatim).
2. **The shipping product has SEVEN runtime ship-blockers** on its live MCP surface — the flows it's *named for* fail on first real use: workflow tools (`.contexts`, 10 sites), `rollback_variant`, always-rollback, `get_emergence_learning_insights` (`VariantStatus.FAILED`), **`initiate_collaboration` (split agent registries — 100% unreachable)**, the **negotiation state machine (responder can only ever reject)**, and `execute_workflow_step` (name-contract mismatch). See §2 / Track B.
3. **The docs oversell.** The `README` still leads with "maximum computational efficiency… **over human readability**" and ships **C++ examples that don't compile**; two module *names* lie (see #1). **[vetting]** The accurate charge is "two modules are overclaimed + a stale README headline," **not** "the whole product is vapor" — most of the ~65 tools are backed by real code.
4. **The genuinely good part is real code that isn't running. [vetting]** The observability substrate (typed `FlowEvent` bus, gzip-JSONL analytics, anomaly detection) is well-built — but the server instantiates the **base** `ObservationManager`; the analytics/persistence live in an `EnhancedObservationManager` that is **never wired in**, and `parent_event_id` causality is never populated. So the moat is *code-only* today.
5. **The C++ is dormant, partial, and defective — not dead. [vetting]** Never linked by the shipping wheel, unmaintained (frozen 2025-05, and its CI was removed 2026-07-06), yet it *does* contain real compiled Goertzel-FSK/CRC/RLE codecs. But the FSK modem is **numerically broken for its own defaults** (109/256 bytes fail) and Reed-Solomon is a hollow stub. Its crypto is theater (plaintext `encrypt()`), but on a path the product never runs.
6. **Security:** no committed secrets (verified over the full tree + history). The one notable exposure — `--http` with zero auth — is currently **latent** behind a startup `TypeError` (C7). The C++ security cluster is severe-in-principle but dormant.

**Recommended posture:** *finish the pivot honestly, around the observability moat.* Fix the seven ship-blockers; **wire the real analytics substrate into the running server** (highest leverage — converts the moat from "code exists" to "runs"); fix + authenticate the HTTP path; retire the two overclaims and the stale thesis; **archive the C++** (done — moved to [`legacy/`](../../legacy/), PR #13) and gate any native-transport revival behind cheap external validation, building the *cheapest sufficient* transport with a full Rust rewrite only as a last resort (§4 / Track L).

> **Update 2026-07-06 (post-decision):** Track **L1** is **resolved → archived** and Track **S2** is **flagged/closed** (both PR #13). The original §4a recommendation to "rewrite in Rust if kept" was **superseded** by an independent verification re-run — see the corrected framing in §4a and the amended handoff [`L1-native-transport-rust-vs-cpp-handoff.md`](./L1-native-transport-rust-vs-cpp-handoff.md). The sole open L1 item is now the **external-validation gate** (a product/market call for the operator), not an engineering task.

---

## Charter for the reviewing fleet — read this first

**This is a set of findings to be adversarially reviewed, not a brief to execute on faith.** It has now been vetted twice (see the addendum) and validated by a blind-recon pass — but the discipline stands for the *next* reader too. Before direction-dependent work (Tracks B/M/L/E), the fleet owes three things — **Track V**. (Security **S** and truth-in-docs **D** proceed regardless.)

1. **Adversarially verify the findings (§2).** Every claim carries a `file:line`; re-open the files. Assign CONFIRMED / IMPRECISE / REFUTED. **Do a V0 blind pass first** — derive your own top-5 from source before reading the claim list; it is where the *missed* findings came from (C13, C14–C16 were all found blind).
2. **Propose counter-theses — steelman XenoComm (§3).** Attack §3. The strongest surviving case (both runs converge): the defensible moat is a **vendor-neutral, out-of-process coordination broker + typed observability plane** that in-process frameworks (LangGraph/CrewAI) structurally cannot produce — *if* the analytics get wired in. Is that real and additive?
3. **Address the ontology (§3 close).** Both runs converge, and sharpen it: v2.4.0 is *"a well-instrumented in-process coordinator of **metadata about a communication that never occurs**"* — a "communication framework" **with no channel**. Model the entities; name the load-bearing commitments and category errors; judge which hold.

**Meta-principle:** *interrogate the instrument, including this document.* This review's own two overreaches (§0.4, §3.3) were caught exactly this way.

---

## 1. The thesis (what v2.4.0 claims)

- **Product (`mcp_server/README`):** "modernized… to bring [XenoComm's] unique capabilities to the MCP ecosystem." Differentiators: **Alignment Verification**, **Protocol Negotiation**, **Protocol Emergence** ("safe evolution… canary + rollback").
- **Claude bridge (`claude_bridge.py`):** Claude agents "register… negotiate protocols, **contribute to dynamic language emergence**, and **be observed via the flow dashboard**."
- **KFM lifecycle (`kfm_lifecycle.py`):** the operator's *Agentic Evolution* Kill/Fuck/Marry selection applied to protocol/agent lifecycle.
- **Residual old thesis (`README` top, `:3`/`:9`/`:327`):** "maximum computational efficiency… machine-optimized interaction **over human readability**."

The last bullet is **in direct tension** with the product (an MCP/JSON-RPC server is standard, human-readable, interoperable — §3.1).

---

## 2. Reality assessment (twice-vetted; **[vetting]** = corrected by the fleet)

**Repo shape:** HEAD `6993158`; ~34K LOC C++ (dormant) + ~15.5K LOC Python (`mcp_server/xenocomm_mcp/`, the product) + docs. Package `xenocomm-mcp` v2.4.0.

| Component | Verdict | Evidence (`file:line`) |
|---|---|---|
| **C++ core (whole SDK)** | **DORMANT / PARTIAL / DEFECTIVE [vetting]** (was "dead legacy") | Never built/linked by the wheel (`pyproject.toml` hatchling; deps only `mcp`+`rich`; bindings commented out `CMakeLists.txt:85`). Frozen 2025-05; CI removed 2026-07-06. *But* real compiled codecs exist: CRC32 `error_correction.cpp:19`, RLE `compression_algorithms.cpp:57`, a Goertzel-FSK modem `ggwave_fsk_adapter.cpp:199`. **Defects:** FSK is numerically broken for its own defaults — symbols >211 alias past Nyquist, **109/256 bytes fail to round-trip**; `ReedSolomonCorrection` emits zero parity (stub). |
| **MCP server** `server.py` (~1833 LOC) | **REAL (~65 tools)** | Working handlers over real engines. But see the seven ship-blockers below — the *coordination* flows fail. |
| **Observation / analytics / dashboard** | **REAL CODE — but the analytics are NOT wired into the server [vetting]** | The server uses the **base** `ObservationManager` (`server.py:48,58` → `observation.py:722` — an in-memory ring). The gzip-JSONL persistence + `FlowAnalytics` + anomaly detection live in `EnhancedObservationManager` (`analytics.py:715`), which is **never instantiated**; `parent_event_id` causality is never populated (runtime `parent=None`). *The strong part exists but does not run.* |
| **Alignment engine** `alignment.py` | **REAL — lexical, not ML [vetting]** | Terminology/goal/assumption overlap via **set-overlap + Jaccard + a hand-authored related-terms bonus** (imports only `hashlib/json/re/math/Counter`). **Not** "embedding similarity" (my §3.3 overreach). Cheap, deterministic, explainable — but only ever exercised on `random.choice` demo agents. |
| **Negotiation / Emergence engines** | **REAL plumbing, no generator** | `negotiation.py` state machine; `emergence.py` real canary/rollback/trend/Z-score stats. But `propose_variant` (`emergence.py:379`) stores a caller-supplied `changes` dict verbatim — **no generative/mutation operator**. Class is `EmergenceEngine` (`:350`); "EmergenceManager" is a stale docstring only. |
| **"Claude bridge / language evolution"** `claude_bridge.py` | **STUB / mislabeled** | **No LLM anywhere** (no `anthropic`/`openai`/`messages.create`/HTTP client, package-wide). Content SHA-256'd + **discarded** (`:426`); "evolution" = intent n-gram counting (`:189-239`) over a 10-key hardcoded map (`:559-586`). **Governance dead**: `votes` (`:507`) never read; promotion automatic (`:271-291`); hook `pass` (`:526-529`). Decoupled from `emergence.py`. *(It is, however, a functioning session registry + message ledger via live tools — the names lie, the module isn't empty.)* |
| **Ship-blocker: workflows `.contexts`** | **BROKEN — 10 sites [vetting]** | `workflows.py` refs `alignment.contexts` at **10** sites (227,242,243,686,859,860,886,887,893,894); `AlignmentEngine` has `registered_agents`, not `contexts` (`alignment.py:200`) → **FAILED step** via try/except (`:208-211`) on Onboarding/ConflictResolution (happy-path) + ErrorRecovery (conditional). |
| **Ship-blocker: `rollback_variant`** | **BROKEN [vetting]** | `InstrumentedEmergenceEngine.rollback` calls `result.get()` at **`instrumented.py:333`** on a `RollbackPoint`/`None` return (`emergence.py:576-613`) → `AttributeError` on **every** call (both branches). |
| **Ship-blocker: always-rollback** | **BROKEN [vetting]** | `should_rollback` returns a `tuple`; `bool((False,None))` is `True` → `orchestrator.py:528` rolls back **healthy** canaries every time. (`:578` only mis-flags a warning.) |
| **Ship-blocker: `get_emergence_learning_insights`** | **BROKEN [vetting]** (missed by pass 1) | `emergence.py:1008` refs `VariantStatus.FAILED`, not a member (`:36-44`) → `AttributeError` on first call after any recorded outcome; empty-guard (`:1004`) doesn't cover it. Live MCP tool (`server.py:979`). |
| **Ship-blocker: `initiate_collaboration`** | **BROKEN — new [vetting]** | Split registries: `register_agent` writes only `alignment_engine.registered_agents` (`server.py:117`); `initiate_collaboration` reads `orchestrator.agent_registry` (`orchestrator.py:161,247-253`), never populated by the public tool → **100% unreachable** via the documented register-then-collaborate flow. Live-reproduced. |
| **Ship-blocker: negotiation state machine** | **BROKEN — new [vetting]** | `initiate_negotiation` creates `AWAITING_RESPONSE`; accept/counter/finalize require `PROPOSAL_RECEIVED`; the only transition into it (`receive_proposal`) is exposed by **no MCP tool** (`negotiation.py:378,392-408`) → via the tool surface a responder can **only ever reject**. |
| **Ship-blocker: `execute_workflow_step` names** | **BROKEN — new [vetting]** | Step-execution accepts only short keys; discovery/creation tools advertise long names → echoing back the given `workflow_name` yields "Unknown workflow type" (`server.py:1651-1663`). |
| **KFM lifecycle** `kfm_lifecycle.py` (614) | **REAL but UNWIRED** | Full weighted KFM engine, exposed by **zero** MCP tools (`grep kfm server.py` = empty). Easy wiring win. |
| **Security — Python surface** | **LOW RISK; C7 latent [vetting]** | No exec/file/shell tools; no LLM data-flow. **C7:** `--http` builds FastMCP with no auth (`server.py:52-55`) → all 65 tools would be open — **but `mcp.run(…,port=port)` raises `TypeError` unconditionally** (`:1827`; `run()` takes no `port` in any `mcp` version) so the HTTP path **crashes on startup**: the exposure is **latent, undocumented**, not live. Pin is `mcp>=1.20.0` (not `>=1.28`). |
| **Security — C++ + bindings (dormant cluster)** | **Severe-in-principle, dormant [vetting]** | `encrypt()` returns **plaintext** with the SSL object never wired to a socket (`security_manager.cpp:156-166`); dead cert-hostname verification (`secure_transport_wrapper.cpp:99-129`); **fail-open** `getNegotiatedCipherSuite` returns AES-256-GCM on every unknown path (`:203-219`); `EncryptedCredentialStore` uses **unauthenticated AES-256-CBC (no MAC)** (`bindings/python/xenocomm/credential_store.py:44-70`, not in the wheel). All in unlinked/dormant code — **ship-blockers *iff* the native transport is revived.** |
| **Dependencies** | **Unpinned + inconsistent [vetting]** | Floor-only ranges, no lockfile/hashes. Manifest inconsistency: `pyproject` (`mcp>=1.20.0`, no `fastmcp`) vs `mcp_server/requirements.txt` (`mcp>=1.0.0` + `fastmcp>=0.1.0`). **Phantom dep**: standalone `fastmcp` is declared but never imported (code uses `mcp.server.fastmcp`). *(No typo-squats; no pinned-vulnerable version.)* |
| **Committed secrets** | **CLEAN ✓** | Full secret-regex battery over tracked tree + `git log --all` = 0 non-placeholder hits. Stated as a clean **end-state** (the historical scrub isn't reproducible from current history). |

**Net:** the coordination flows the product is *named for* — collaboration, negotiation, workflows, rollback, learning-insights — fail on first real use. The genuinely valuable substrate (observability) is real code that isn't wired in.

---

## 3. Adversarial challenge to the thesis (re-grounded + twice-vetted)

### 3.1 The stated efficiency/opacity thesis contradicts the product *(material — integrity)*
The `README` sells "efficiency… over human readability" + emergent binary protocols; the product is a standard, human-readable, interoperable **MCP/JSON-RPC** server — the philosophical opposite. Abandoned in code, left standing in docs (`README.md:3,9,327`), and the C++ code samples (`:122-162`) reference an API that **doesn't exist in the shipped headers**. (For cloud LLM agents the efficiency premise was always mis-aimed — transport bytes ≪ inference — so abandoning it was correct; the problem is not *saying so*.)

### 3.2 The pivot moved toward observability — the right bet, but unbuilt where it counts *(sharpest)*
"Observed via a flow dashboard" is the right instinct. **But:** (a) "language evolution" invokes **no model**; (b) **governance is dead code**; (c) **[vetting]** the analytics that would make it auditable (`EnhancedObservationManager`) are **not wired into the server** — what runs is an in-memory ring with `parent_event_id` never populated. So the observability is *live but shallow* and the "governed emergence" promise is unmet. The valuable version (persisted + causal + LLM-proposed + human-adopted) is exactly what's missing — but most of it is a **wiring** job, not a from-scratch build.

### 3.3 "Alignment verification" is real — and it is lexical, not ML *(corrected)*
**[vetting]** My first pass called it "a wrapper around embedding similarity." **There is no embedding and no ML** — `alignment.py` imports only `hashlib/json/re/math/Counter`; it is set-overlap + Jaccard + a hand-authored related-terms bonus. That over-credited its sophistication while under-crediting its real virtue (cheap, deterministic, explainable, returns recommendations). It remains **unvalidated** — exercised only on `random.choice` agents.

### 3.4 The reinvention question: what is additive over vanilla MCP? *(the strategic crux)*
Both fleet runs converge: the defensible kernel is **not** alignment/emergence but XenoComm's **position** — a vendor-neutral, *out-of-process* coordination broker plus a typed `FlowEvent` observability plane exposed as first-class MCP tools. In-process frameworks (LangGraph/CrewAI) structurally cannot produce this because they trace only their own graph, not an exchange that straddles the inter-agent / inter-vendor / inter-process boundary. **Caveat [vetting]:** today that moat is "a typed *in-memory* flow substrate," not "a causal, persisted stack" — the persistence/causality is code-only. Honest one-liner: *"OpenTelemetry-for-agent-meshes + a governed change-management pipeline, delivered as MCP tools" — ~80% built, the last 20% (wire the analytics, populate causality) is the whole game.*

### 3.5 Integrity: two modules oversell — but not the whole product *(narrowed)*
**[vetting]** "The docs describe a product that doesn't exist" over-generalized. Precisely: **two module *names* lie** (a "Claude bridge" with no Claude; "language evolution" that's n-gram telemetry), the README carries a stale headline thesis + non-compiling C++ examples, and "emergence" has no generator. But the load-bearing README claims (MCP bridge, canary/rollback, observability) *are* present in code, and the overclaims live in **names + two README passages**, not the tool docstrings (grep of tool source = zero stale marketing). The fix is an S-sized rename + doc pass, not a rewrite.

### 3.6 Ontology: a communication framework with no channel *(both runs converge; understated)*
v2.4.0 is *"a well-instrumented in-process coordinator of **metadata about a communication that never occurs**."* Its one coherent load-bearing entity is the FlowEvent substrate; every higher noun is inflated one category: telemetry→"language," bookkeeping→"emergence," a preference dict→"protocol," a hash→"conversation content," an un-invoked integration→"Claude bridge." **The deepest error: there is no channel** — agents never open one to each other; they call server tools routed through an in-process `_message_handlers` dict, and the negotiated `NegotiableParams` "protocol" is never bound to any socket. And there are **three disjoint agent registries** (`AlignmentEngine.registered_agents`, `bridge.sessions`, `KFM.entities`) never reconciled — the ontological root of the C14 collaboration failure.

---

## 4. The strategic fork

> Decided **after Track V**. Both fleet runs recommend **A**, reframed as positive strategy, not damage control.

| Option | What it means | Verdict |
|---|---|---|
| **A — Finish the pivot honestly, around the observability moat** | Fix the 7 ship-blockers; **wire `EnhancedObservationManager` into the server + populate `parent_event_id`** (converts the moat from code→product); fix + authenticate the HTTP path; retire the thesis + rename the 2 overclaims; wire in KFM. | **Recommended.** Realizes the one defensible moat; most of it is wiring, not new build. |
| **B — Make emergence *real*** | On top of A: an LLM *proposes* variants; real governance (vote/adopt + human gate); language-patterns → `emergence.py` → canary/rollback with full-transcript audit. | **Premature.** Spends the hard budget on the module with the least product pull, before the finished substrate is even persisted. Later milestone, unadvertised until real. |
| **C — The native transport: archive / maintain-C++ / rewrite-in-Rust** | The C++ is dormant, partial, defective. Decide its fate. | ✅ **RESOLVED → archived** (PR #13, under `legacy/`). Any revival gated behind external validation — see §4a. |

### 4a. The C++/native-transport trilemma — and the Rust question

> ✅ **RESOLVED (2026-07-06, PR #13) → ARCHIVED.** The C++ SDK was moved intact under [`legacy/`](../../legacy/) (for posterity), Track S2 was flagged in-source + in [`/LEGACY.md`](../../LEGACY.md), and the decision brief was widened after an independent verification re-run. **The "rewrite in Rust if kept" recommendation below is superseded** — a from-scratch Rust rewrite is the *last resort*, not the default; the cheapest-sufficient ladder (pure-Python/NumPy → FFI-vendored maintained DSP → surgical Rust hot-path) comes first, and the whole thing is gated behind external validation. The corrected reasoning is the source of truth: [`L1-native-transport-rust-vs-cpp-handoff.md`](./L1-native-transport-rust-vs-cpp-handoff.md). The original text is retained below as the audit trail.

The genuinely-real part of the C++ is the acoustic/RF-denied codec (FSK/CRC/RLE) — a real niche the MCP layer can't serve. But it is **numerically broken for its own defaults** (109/256 bytes fail), Reed-Solomon is a stub, and the crypto is plaintext. So:
- **Archive** — if constrained-transport is not a real target, delete the dead weight (and the misleading thesis/security theater it carries).
- **Maintain as C++** — weakest option: the codec, error-correction, and crypto **all need rebuilding anyway**, so you'd be doing a rewrite *in the harder language*.
- **Rewrite the live kernel in Rust — recommended *if* the native plane is kept.** Since a rewrite is unavoidable, Rust is the future-oriented/SOTA choice, bounded to the real kernel (~a few files, not the 34K of stubs): (1) **memory safety** on the untrusted-bytes-off-a-wire / hard-to-patch embedded surface — the exact place the removed Valgrind CI was guarding; (2) the crypto is a rebuild regardless — do it on RustCrypto/`rustls`/`ring` instead of the plaintext C++; (3) **PyO3 + maturin** gives cleaner Python-extension interop than the abandoned pybind11 path; (4) **Rust→WASM** opens the edge/portable agent-mesh future; (5) it aligns with the operator's existing Rust toolchain. Incremental path: Rust wrapper + real crypto/transport first, FFI to a C `ggwave` core short-term, port the DSP last. **Caveat:** only if acoustic/RF-denied swarm signaling is a real target — moot under MCP-only.

**Do SECURITY (S1) and TRUTH-IN-DOCS (D1) regardless of the fork.**

---

## 5. Action plan — delegatable tickets

### Track V — VERIFY (V0 blind + V1) · COUNTER-THESIS · ONTOLOGY (gates §4)
- **V0 — Blind recon. [M]** Derive your own top-5 from source before the claim list. *(This pass found C13–C16.)*
- **V1 — Verify §2. [M]** CONFIRMED/IMPRECISE/REFUTED per finding. **V2 — Counter-thesis** (§3.4 moat). **V3 — Ontology** (§3.6). *Route: V1→auditor · V2→strategist · V3→formal/ontology specialist.*

### Track B — BUGS (seven ship-blockers on the live MCP surface)
- **B1** — `workflows.py` `.contexts` (10 sites → `registered_agents`/property). **B2** — `rollback_variant` (`instrumented.py:333` dataclass-vs-dict). **B3** — `should_rollback` truthy-tuple (`orchestrator.py:528`). **B4** — `VariantStatus.FAILED` (`emergence.py:1008`; drop the dead member or define it). **B5** — split agent registries → `initiate_collaboration` unreachable (reconcile `register_agent` with `orchestrator.agent_registry`). **B6** — negotiation state machine (expose a `receive_proposal` tool / fix the transition). **B7** — `execute_workflow_step` name-contract (`server.py:1651-1663`). *Each [S], with a test.*

### Track S — SECURITY (regardless of fork)
- **S1 — Fix + authenticate the HTTP transport. [S]** First repair the `mcp.run(port=…)` `TypeError` so the path even starts; then add auth + bind-host restriction **before** it goes live. Document stdio-as-default. *(Today: latent behind the crash.)*
- **S2 — Neutralize the dormant C++/bindings security cluster. [S]** ✅ **DONE (PR #13):** flagged in-source — a `SECURITY WARNING` file banner + an inline plaintext-return flag in `legacy/src/core/security_manager.cpp` — and documented in [`/LEGACY.md`](../../LEGACY.md); the whole cluster is archived under `legacy/` so nothing claims security it lacks. *(Original: plaintext `encrypt()`, dead cert verify, fail-open cipher-suite, CBC-no-MAC credential store. Would become ship-blockers iff the native transport is revived — Track L.)*

### Track D — TRUTH-IN-DOCS (highest integrity-per-effort)
- **D1 — Retire the thesis + rename the overclaims + fix the fake examples. [S]** Remove `README.md:3,9,327`; rename `claude_bridge` "language evolution"→"intent-pattern telemetry" and "Claude bridge"→"agent session registry"; delete/replace the non-compiling C++ examples (`:122-162`); add a per-component status matrix (use §2).
- **D2 — Fix the dependency manifest. [S]** Reconcile `pyproject` vs `requirements.txt`; drop the phantom `fastmcp` dep; add upper bounds (`mcp>=1.20,<2`) + a hash-pinned lockfile.

### Track E — REALIZE THE MOAT (highest leverage)
- **E1 — Wire the observability substrate into the product. [M]** Instantiate `EnhancedObservationManager` in `server.py` (persisted gzip-JSONL analytics + anomaly detection) and **populate `parent_event_id`** so causality is real. This converts the strongest counter-thesis from "code exists" to "runs in product."
- **E2 — Position vs vanilla MCP. [M · deps V2]** ✅ **DONE:** evidence-backed positioning brief in [`E2-positioning-vs-vanilla-mcp.md`](./E2-positioning-vs-vanilla-mcp.md) — the structural differentiator is the out-of-process, vendor-neutral broker vantage (one typed event stream across independent agent processes); every claim is anchored to *wired* code and bounded by an honest limitations section. **Note:** this supersedes the §3.4 "the moat is code-only today" caveat — E1 (PR #6) wired `EnhancedObservationManager` into the running server, live-verified against `main` (server boots, 73 tools register, `get_flow_analytics` returns populated telemetry).

### Track M — MAKE EMERGENCE REAL (only under Option B, after A/E)
- **M1 — LLM-proposed + governed evolution. [L]** Real LLM proposes variants; real governance (vote/adopt + human gate — the dead `votes` path); wire promoted patterns → `emergence.py` → canary/rollback; full-transcript audit.
- **M2 — Wire in KFM. [S]** Surface `kfm_lifecycle.py` as MCP tools.

### Track L — NATIVE TRANSPORT (§4a trilemma)
- **L1 — Decide archive / maintain-C++ / rewrite-in-Rust. [S decision]** ✅ **RESOLVED → ARCHIVED (PR #13).** The C++ is dormant/partial/defective; a revival is a rewrite regardless, so it was archived under `legacy/` rather than maintained. **The sole remaining item is the external-validation gate — a product/market call for the operator, not an engineering task:** a one-page use-case memo → 3–5 discovery calls seeking a *named partner / LOI / funded pilot* → a weekend de-risking spike on an *existing* DSP library (not a rewrite), with a pre-committed kill criterion ("no co-funded pilot in 60 days → stay archived"). **Only on a validated "yes"**, build the *cheapest sufficient* transport — pure-Python/NumPy → FFI-vendored maintained DSP → surgical Rust hot-path — with a full Rust+PyO3 rewrite reserved as the evidence-gated **last resort**, gated behind a bit-exact conformance corpus. Playbook + corrected reasoning: [`L1-native-transport-rust-vs-cpp-handoff.md`](./L1-native-transport-rust-vs-cpp-handoff.md).

---

## 6. How to run this with a fleet

- **Ordering:** S1 + D1/D2 immediately (independent). **Track V (with a V0 blind pass) gates §4 and B/M/L/E.** B1–B7 are quick and unblock the live product; E1 is the highest-leverage single move. M/L per the chosen fork.
- **Each ticket → one agent**, every claim traced to a `file:line`.
- **Verification discipline (earned three times here):** don't trust "it's implemented," *or* a stale checkout, *or* your own praise. This review audited the wrong tree once, missed a live-tool crash once, and over-credited an observability stack that isn't wired in — each caught only by reproducing against the real HEAD and by a **blind** pass that ignored the claim list. **Require real evidence** — a raised `AttributeError`, an answered socket, an LLM request actually sent — not a plausible module.
- **Non-negotiables independent of the fork:** S1 (fix + auth the HTTP path), D1 (truthful docs). Committed secrets are clean.

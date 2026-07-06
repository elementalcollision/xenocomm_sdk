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

### 1a. The review's adversarial arguments — inlined so this prompt is self-contained (for Charge V2)

> If you were handed only this prompt: the full companion review is committed at **`docs/audits/xenocomm-review-and-action-plan-2026-07-06.md`** in the same repo — read it. But you should not have to, to argue against it. The arguments Charge V2 must engage (review §3–§4) are summarized here:
> - **§3.1 — Efficiency thesis abandoned.** The README's "efficiency over human readability" thesis is contradicted by an MCP/JSON-RPC product, and was mis-aimed for cloud LLM agents (transport bytes ≪ inference cost).
> - **§3.2 — Governed-emergence promise unmet.** The observability pivot is the right instinct, but "language evolution" calls no model, governance is dead code, and logged hashes are telemetry, not an auditable transcript.
> - **§3.3 — Alignment on toy inputs.** `alignment.py` is a genuine attempt at heterogeneous-agent mutual understanding, but is only exercised on `random.choice` agents.
> - **§3.4 — Additive value.** What is agent-coordination-as-an-MCP-tool additive over vanilla MCP + a standard orchestration framework?
> - **§3.5 — Docs-integrity gap.** A "Claude bridge" with no Claude; "emergence" with no generator.
> - **§4 — Strategic fork.** (A) finish honestly · (B) make emergence real · (C) archive the C++. The review recommends A.

---

## Charge V0 — Independent blind recon (do this FIRST, before you read C1–C10)

**Anchoring is the single biggest threat to this vetting.** Charge V1 hands you Chimera's exact map of the territory (C1–C10). If you read it first, you will tend to color inside its lines and become a confirmation-checker. So **before** you open §2:

1. From the **source tree + READMEs only**, derive your **own top-5 findings** about XenoComm v2.4.0 — the things you would flag if Chimera's review did not exist. Anchor each to `file:line`.
2. **Seal them** in your report as "V0 blind findings." You may not edit them after reading §2.
3. *Then* read §2, and mark each blind finding as **matches a Chimera claim (which one)**, **new / missed by Chimera**, or **contradicts Chimera**.

The empirical case for this pass: in the first run of this fleet, the anchored verifiers (V1) merely confirmed all 10 claims, while the **un-anchored ontology pass surfaced the one ship-blocker Chimera had missed** (`VariantStatus.FAILED`, `emergence.py:1008`, live-reachable via `get_emergence_learning_insights`). Blind recon is where missed findings come from. **Deliver:** your sealed V0 top-5 + the match/new/contradicts mapping.

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
| C7 | `run_server` starts FastMCP **streamable-http with no auth** → all ~65 tools reachable unauthenticated over the port (flag is `--http`). **Also judge:** is this an *intentional* deferral to an infra auth layer (reverse proxy / `mcp-proxy` / Unix socket) that is **documented**, or an undocumented oversight? The verdict on severity turns on that. | `server.py:1826`; READMEs |
| C8 | `emergence.py` **never generates** a variant — `propose_variant` stores a caller-supplied `changes` dict; no evolutionary/generative step exists. | `emergence.py` (`propose_variant`) |
| C9 | The **README contradicts the product** — still advertises "computational efficiency… over human readability" + the C++ SDK, while the shipping product is a standard, human-readable MCP/JSON-RPC server. | `README.md` top; `mcp_server/README` |
| C10 | **No committed secrets remain** in the tracked tree; only placeholders remain in `.env.example`/`.cursor`. *(Report the clean end-state; do not assert "the scrub is confirmed" — greps over current history cannot prove a key once existed and was removed.)* | tracked tree; `git log -p` |
| C11 | **Repo / C++ vitality.** Is the C++ genuinely inert, or maintained on a branch/fork? "Vestigial" is a claim about the **shipping wheel**, not the whole codebase — verify that distinction. Check for recent C++ commits, CI that builds/tests it, and any branch/tag that links it into a product. | `git log --all --graph --oneline --decorate`; `.github/workflows/`; `CMakeLists.txt` |
| C12 | **Dependency supply-chain.** Audit the deps (`mcp`, `rich`, transitives) for typo-squatting, unpinned/floor-only ranges, and known-vulnerable versions; note the absence of hashes/lockfile. | `mcp_server/pyproject.toml`; `requirements.txt` |

**Static-first, live-second, sandbox-always.** Static / `ast` / grep evidence is the **floor** for every claim — e.g. for C7, trace the auth-middleware path in source *before* binding any listener. Live reproduction (triggering the C4/C5/C6 runtime errors; probing C7) is *higher-value confirmation* but is **opt-in and sandbox-gated**: run only in a disposable/loopback environment, bind a high random port, wrap in a hard timeout, and then **verify** the process is gone (`pgrep`/`ps`) — do not merely hope. C1/C3/C10/C12 are grep-decidable; C2/C8/C9 are read-and-judge. If your environment cannot safely run the code, say so and fall back to static — a static verdict that shows its work beats an unsafe live one. (Environment note in §5.) **Read-only: do not modify, commit, or push anything.**

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
2. **Static first; reproduce only in a sandbox.** Static / `ast` / grep is the floor for every claim. Live reproduction is opt-in, loopback-only, high-random-port, hard-timeout, and you must *verify* no process survives afterward. Change nothing on disk. A well-shown static verdict beats an unsafe live one.
3. **Interrogate the instrument — including this document and Chimera's review.** Your highest-value output is a place where the review is wrong, imprecise, or missing something.
4. **Read-only.** Do not modify, commit, push, or open PRs against the target repo. You produce a report; humans decide what lands.
5. **Provenance & posture — transparent, not opaque.** This is autonomous-agent work under human-in-the-loop review, and your report must **say so**. Head it with: `Reviewed by <Agent Persona> — autonomous agent, human-in-the-loop`. Attribute to the accountable **agent persona** — the entity that can be re-queried and held to its findings — and omit vendor/tool co-author trailers ("Claude Code" / Anthropic / model name): those name the substrate, not the reviewer, and add no accountability. Provenance-as-agent is *disclosed*; only the tool-brand trailer is dropped. Two co-signers: `Reviewed by <A> and <B> — independently authorized agents, human-in-the-loop`.
6. **Return structured findings**, not prose essays, in the shapes specified per charge, so results compose across the fleet.
7. **Environment note.** The reference run reproduced C4–C7 under Python 3.14 with `mcp` 1.28.1 (floor `>=1.20`); the `mcp_server` wheel builds with `hatchling` (no C++ toolchain needed). If your environment diverges, report an `ImportError` / build failure as an *environment* result, kept distinct from the finding's verdict.

---

## 6. What "good" looks like

A vetting pass is successful if it returns: your sealed V0 blind findings, a corrected findings table, the strongest surviving counter-thesis, and an ontology sharp enough to decide the strategic fork on. **Honest confirmation is worth exactly as much as refutation** — the standard is *rigor*, not manufactured dissent. If a claim survives your best attempt to break it, CONFIRM it and **show the work**: what you tried, what would have falsified it, why it held. Do **not** downgrade to IMPRECISE to look diligent — a 10/10-CONFIRMED pass that documents its refutation attempts is a strong result; a pass that invents nitpicks to avoid unanimity is a weak one. The highest-value single output remains a place where the review is genuinely wrong, imprecise, or incomplete — and if you found none after trying hard, say *that*, plainly.

---

## Revision log

- **v2 (2026-07-06)** — Revised after an external model review of v1. Added **Charge V0 (blind recon)** to counter anchoring bias — empirically justified: v1's first fleet run confirmed all 10 anchored claims but found the one *missed* bug only via the un-anchored ontology pass. Inlined the review's §3–§4 arguments (§1a) so the counter-thesis charge is fulfillable from this prompt alone. Made reproduction **static-first / sandbox-gated** with an environment note. Reframed §6 so honest confirmation counts equal to refutation (removing the incentive to manufacture dissent). Replaced the attribution rule with a **transparent provenance header** (`Reviewed by <persona> — autonomous agent, human-in-the-loop`): agent authorship is disclosed; only the vendor/tool trailer is dropped. Added **C11 (repo/C++ vitality)** and **C12 (dependency supply-chain)**, and sharpened **C7** to ask whether the no-auth HTTP is documented design or oversight.

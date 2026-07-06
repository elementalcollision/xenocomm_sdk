# Handoff — L1: the native-transport decision (archive, or build the cheapest sufficient transport — full Rust rewrite only as a last resort)

**For:** the next Chimera agent to pick up **Track L1** of the XenoComm v2.4.0 review.
**Prepared by:** Chimera — autonomous agent, human-in-the-loop.
**Date:** 2026-07-06
**Revised:** 2026-07-06 — after an independent verification re-run (a 10-agent adversarial workflow plus manual reproduction against the real PR base `origin/main` `e9284d1`). What changed and why is in §7; the short version: the *evidence of brokenness* re-confirmed clean, but the *decision framing* was widened — the original steered toward a Rust rewrite that is neither the cheapest nor the safest first move.
**Companion:** [`xenocomm-review-and-action-plan-2026-07-06.md`](./xenocomm-review-and-action-plan-2026-07-06.md) (§4a, Track L1).

> **Status (2026-07-06): ARCHIVE EXECUTED (PR #13).** The C++ SDK was moved under [`legacy/`](../../legacy/), Track S2 was flagged in-source + in [`/LEGACY.md`](../../LEGACY.md), and the README/review-doc were reconciled. **One item remains open, and it is not an engineering task:** the §4 external-validation gate — the product/market call the operator must make before any native-transport revival. Everything below §4 is the ready-to-run playbook for that gate; nothing further should be *built* until it returns an external signal.

---

## 0. TL;DR — what this handoff is

Track L1 is **a decision, not a coding task.** The XenoComm C++ core (`src/`, `include/`) is dormant legacy — never built or linked by the shipping Python MCP server. Its one arguably-valuable asset is an acoustic/RF-denied **codec/FSK transport** concept that the MCP/JSON-RPC layer structurally cannot serve. But the concept is only a concept: the implementation is broken, unlinked, and untouched since the initial commit.

The decision is **not** "which language do we rewrite in." It is, in order:

> **1. Is a native constrained-transport a real product target at all? → if not, archive.**
> **2. If it is, what is the *cheapest sufficient* way to build it? → almost never a from-scratch Rust rewrite.**

Recommendation:

- **Default: archive the C++ now.** It is reversible (it's git), near-zero cost, and removes dead weight, the misleading examples, and the S2 security theater in one move. The Python MCP product is the only asset with users and a live market; nothing depends on the C++.
- **Do not answer the product question as a bare yes/no.** Replace it with a cheap staged validation that any single step can kill (§4).
- **Only on a *validated* yes, build the cheapest sufficient rung** of the ladder in §3 — pure-Python/NumPy, then FFI-vendored maintained DSP, then a surgical Rust hot-path — with a **full Rust + PyO3 rewrite reserved as the evidence-gated last resort, not the default.**
- **"Maintain as C++" stays rejected** (§3, Option B).

**Do not begin any build before the §4 validation returns an external signal (a named design partner / LOI / funded pilot).**

---

## 1. Why this is open (context)

The v2.4.0 review pivoted XenoComm's story: the shipping product is a pure-Python MCP coordination server; the C++ SDK is legacy. Two adversarial fleet runs (26 agents, then 35 with a blind-recon pass) converged on the C++ picture, and the tree now reflects it:

- **The C++ CI/CD is gone.** It was removed early (`4c0f518`, PR #2, "remove the CI/CD backend during active development") and never restored; on the current base `origin/main` the workflow files contain zero cmake/ctest/valgrind steps. The CI only ever built the legacy C++ + the old pybind11 bindings — never the shipping wheel.
- **The docs demarcate the C++ as legacy/dormant.** The recent D1 truth-in-docs pass (`e9284d1`, PR #10) relabels the SDK "legacy and dormant — not built or linked by the MCP server; unmaintained."

So the C++ is explicitly deprioritized. L1 is the remaining question: **do we ever revive a native transport, and if so, how?**

> ⚠️ **Verify against a fresh HEAD, not a stale checkout.** This review chain has audited a stale tree before, and it nearly happened again: a working copy "synced to main" was found **24 commits / ~5 months behind** the real `origin/main` (`e9284d1`, 2026-07-06). The deprioritization above is present on the real base but was *absent* from that stale snapshot. `git fetch && git log origin/main` before trusting anything here.

---

## 2. Verified state of the C++ (the evidence)

All findings below were reproduced against the real tree (`origin/main` `e9284d1`; the three audited files are byte-identical between it and the stale local snapshot, so the code findings transfer). Re-verify before acting — *interrogate the instrument.*

| Fact | Evidence | Verdict |
|---|---|---|
| **Never built/linked by the product** | `mcp_server/pyproject.toml` (hatchling, runtime deps only `mcp`+`rich`); no `_core`/pybind/ctypes/dlopen import anywhere in `mcp_server/`. The pybind11 bindings subdir is commented out (`CMakeLists.txt:85`). Dylib `xenocomm_core` versioned `0.1.0` vs wheel `2.4.0`. | ✅ Confirmed |
| **Dormant / unmaintained** | `src/` frozen ~2025-05; C++ CI removed (see §1). | ✅ Confirmed |
| **Real codec layer exists** | CRC32 `src/core/error_correction.cpp:19` (real, table-driven, poly `0xEDB88320`); RLE `src/core/compression_algorithms.cpp:57`; a Goertzel-FSK modem `src/core/ggwave_fsk_adapter.cpp`. ~700 LOC of real transcoder adapters in `src/core/*_adapter.cpp`. | ✅ Confirmed |
| **…but the FSK modem is broken for its own defaults** | Defaults (`ggwave_fsk_adapter.h:21-26`): sr 44100, base 1000 Hz, spacing 100 Hz, 256 samples/symbol. Symbol *s* → `1000 + 100·s` Hz; symbols **≥ 211** exceed Nyquist (22050 Hz) and alias. A faithful replica measures **109/256 byte values fail to round-trip**. | ✅ Confirmed — **with two refinements** (below) |
| **…and Reed-Solomon is a hollow stub** | `error_correction.cpp` `ReedSolomonCorrection` (the doc previously misspelled it "ResolomonCorrection") — library `#include` (`:7`) and every `rs.encode`/`rs.decode` call (`:190,213-217,244,262-265`) are commented out; parity shards are zero-filled (`:208-211`). | ✅ Confirmed — **worse than "empty"** (below) |
| **Crypto is theater (Track S2)** | `security_manager.cpp:150-167` `encrypt()` returns `pseudo_encrypted_data = data` — a copy of the **plaintext**, never the ciphertext. Fail-open cipher reporting: `getNegotiatedCipherSuite()` returns `AES_256_GCM_SHA384` even when `ssl_` is null (`:204,207,210`). Dead cert-hostname verification (`secure_transport_wrapper.cpp:99-129`). | ✅ Confirmed — **with a fail-closed nuance** (below) |
| **README C++ examples don't compile** | Walkthroughs call `AsyncConfig` and `EmergenceManager` methods that don't match the real headers. | ✅ Confirmed — precisely, **signature drift** (the symbols exist, wrong shape), not wholly nonexistent |

**Evidence refinements found in the re-run (these make the code *more* broken, not less — but for reasons the original headline compressed):**

1. **FSK — aliasing is only ~40% of the story, and the detector doesn't even terminate.**
   - Of the 109 failing byte values, only ~43 come from the 45 above-Nyquist symbols (211–255). The other **~66 fail *below* Nyquist**, because the Goertzel bin width is `sr/samples_per_symbol = 44100/256 ≈ 172 Hz`, wider than the 100 Hz symbol spacing — adjacent tones are physically unresolvable and the arg-max picks the wrong bin. So "symbols > 211 alias" is true but under-describes the breakage.
   - Separately, `detectSymbol()` at `ggwave_fsk_adapter.cpp:221` loops `for (uint8_t symbol = 0; symbol < 256; ++symbol)`. `uint8_t` wraps at 255, so `symbol < 256` is **always true** — the detector is an **infinite loop**; the modem as written never returns. (The 109/256 figure is the *intended* algorithm, measured with the loop widened to `int`.) Any revival must treat the modem as non-functional, not merely lossy.

2. **Reed-Solomon advertises correction it cannot perform.** Beyond emitting zero parity, `maxCorrectableErrors()` still returns `parity_shards/2` — so callers are told the channel can correct errors it silently cannot. That is a latent data-integrity trap, not just a no-op.

3. **`encrypt()` fails *closed* if the SSL is never wired.** It is gated on `SSL_is_init_finished(ssl_)`; if the handshake never happens (the likely state, since nothing wires the BIO to a socket) it returns an error, `"SSL not ready"`, rather than silently shipping plaintext. The plaintext return is real but reachable only on the handshake-complete path. Either way the function **provides no encryption** — the danger is "it looks like crypto and isn't," and the severity is conditional on wiring.

**Net:** the C++ is *dormant, partial, and defective* — the codec/FSK/RS/crypto do not work as-is and the modem does not even run. The transport *concept* is real; the *implementation* is not an asset, it is a liability with a working CRC32 attached.

---

## 3. The option space (was framed as a trilemma — it is wider)

The original brief collapsed this to "which compiled language" (maintain-C++ / rewrite-Rust / archive). That framing deletes the options that dominate a from-scratch Rust rewrite in almost every realistic case. The real choice is a ladder, cheapest first.

### Option A — Archive (**the default**)
Move `src/`/`include/` under a clearly-marked path (or split to an archived repo) and stop shipping it in the tree. Correct **whenever constrained-transport is not a *validated* target** — which is the state today. Cheapest, reversible (it's git), and it closes Track S2 in the same move by removing the plaintext-`encrypt()` / dead-cert-verify theater so nothing claims security it lacks.

### Option B — Maintain as C++ (**reject**)
Reed-Solomon is genuinely dead code, so keeping it means rebuilding it — and nobody invokes the C++ at all, so paying to keep a toolchain and a memory-unsafe DSP/parse surface that has no callers is indefensible. B is dominated by "archive" (if you don't need it) and by every rung of C (if you do). Reject.

### Option C — Build a real transport (**only on a validated §4 yes; pick the cheapest sufficient rung**)
The original's core argument was *"we're rebuilding all of it anyway, so the language choice is free — so choose Rust."* **That domination argument is two-thirds false.** Of the three "rebuild-regardless" premises, only one holds:
- **Reed-Solomon** — genuinely greenfield (commented out). ✅ rebuild-regardless is true.
- **Crypto** — *not* greenfield. `secure_transport_wrapper.cpp` is ~900 LOC of real OpenSSL BIO/SSL integration. `encrypt()` is a real bug on top of real plumbing; a rewrite *throws working integration away and risks reintroducing* cert/handshake defects. Rewriting the crypto "for safety" can be net-negative.
- **Codec/adapters** — *not* greenfield. ~700 LOC of real transcoder adapters exist; the empty `data_transcoder.cpp` base class is empty *by design*.

Once two of three premises fall, a rewrite is not free at the margin. So if you build, climb the ladder and stop at the first rung that meets the *proven* requirement:

- **C1 — Pure-Python + NumPy.** The modem is trivial narrowband FSK; a *correct* implementation (fixing the Nyquist/bin-width/RS defects) is a few hundred lines and **preserves the product's actual deployment model** — zero native deps, `mcp`+`rich`, no wheel-matrix. This is the right first choice unless a *measured* throughput/latency requirement rules it out.
- **C2 — FFI-vendor a maintained DSP library** (real `ggwave`, `liquid-dsp`, or GNU Radio blocks) *if* a genuine acoustic/interop target appears. Avoids re-deriving modem DSP — the single riskiest task in the whole endeavor — and inherits years of hardening.
- **C3 — Surgical Rust-via-FFI for the untrusted-parse hot-path only** (RS decode + FSK header framing), keeping the rest Python. Captures the memory-safety win **exactly where it is decisive** — e.g. the `reinterpret_cast` over an attacker-controlled-length `FskHeader` buffer at `ggwave_fsk_adapter.cpp:91` is genuine UB — without paying the full-rewrite / multi-platform-wheel tax. Justified only if an untrusted-input surface *actually exists* in the deployment (an in-process, trusted-peer agent mesh has none; a mic/radio picking up attacker-shaped bytes does).
- **C4 — Full Rust + PyO3 rewrite (the last resort).** Justified only when **all** of: (a) constrained-transport is a validated target; (b) throughput is a *proven* hard requirement pure-Python cannot meet; (c) no upstream C library or Rust crate fits; and (d) the team accepts re-incurring the native multi-platform build burden it shed by killing the C++ CI. **Nothing in evidence establishes any of (a)–(d) today.**

**Honest cost note (correcting "a few files").** A real C4 kernel is not "a few files": it is re-porting the working crypto and adapters, a from-scratch RS + FSK DSP, **plus** a PyO3/maturin/`cibuildwheel` multi-platform wheel pipeline that does not exist in this repo, **plus** a bit-exact wire-format conformance corpus that also does not exist. That is weeks-to-months and dozens of files, and the wheel matrix and conformance corpus are the top estimate-blowing risks — ahead of the code itself.

**Two engineering claims from the original that do not hold (removed):**
- ~~"Rust → WASM opens an edge future for free."~~ PyO3 links the CPython C-API and does **not** target browser WASM; a WASM build is a *separate, cfg-gated, PyO3-free* path plus solving `ring`/`getrandom` wasm constraints — a separately-funded port, not a byproduct.
- ~~"FFI to a C `ggwave` core short-term."~~ Self-defeating and false-premised: there is **no `ggwave` library in the tree** (the FSK is ~265 lines of hand-rolled DSP), and bolting a C FFI onto the untrusted decode path *reintroduces the exact memory-unsafe surface* a Rust move exists to remove. If you want a maintained C core, that's C2 (vendor a real library behind a validated need), not an interim hack.

**What the original got right (kept):** rejecting maintain-C++; keeping archive as the default; gating on a *product/market* question rather than a technical one; and the Reed-Solomon "rebuild in any language" observation. The instinct — *don't rewrite unless constrained-transport is real* — is directionally correct; only its packaging into a Rust-leaning trilemma was wrong.

---

## 4. The gate — replace the bare yes/no with cheap external validation

The load-bearing question is still **"Is a constrained-transport regime (acoustic / embedded / RF-denied / bandwidth-or-energy-constrained multi-agent signaling) a real XenoComm target?"** But a bare yes/no is the wrong instrument: it has no evidence bar, and it invites a motivated "yes" from the same operator whose orphaned stub created the question — textbook sunk-cost reasoning. Demand an *external* signal, cheaply, before any build:

1. **A one-page use-case memo** — who has this problem, what they use today, and why software-defined acoustic/RF beats it. Name the *specific* adjacency (acoustic air-gap vs lossy/low-bandwidth vs RF-denied); different targets favor different rungs in §3. This alone may return a decisive "no."
2. **3–5 customer-discovery conversations** seeking a **named design partner, an LOI, or a funded pilot.** Internal conviction does not count.
3. **A weekend de-risking spike on an existing library** (real `ggwave` / GNU Radio / `liquid-dsp) — **not a rewrite** — to prove the team can move real bits over a real constrained channel *at all.* This tests *capability*, which a broken, never-run stub leaves unproven.

**Pre-commit the kill criterion** (a gate with no disconfirming test just limps forward on hope): e.g. *"if no partner co-funds a pilot within 60 days, we stay archived."*

- **Default / no external signal → Option A (archive)** and keep investing in the Python product. Archive is the reversible, near-zero-cost, correct outcome — not a sad one.
- **Validated external signal → build the cheapest sufficient rung of §3** (C1 → C2 → C3, with C4 the last resort).

This is exactly the kind of hard-to-reverse investment to confirm with a human — and with the *market* — before building.

---

## 5. What the next Chimera agent should do

1. **`git pull` / re-verify §2 against a fresh `origin/main` HEAD** (this audit was against `e9284d1`; a stale local copy was 24 commits behind — do not repeat that). Don't trust this doc's `file:line`s without reopening them.
2. **Archive the C++ now, and document the traps at the point of archival** — in the same PR, record that Reed-Solomon advertises correction it cannot perform (`maxCorrectableErrors`), that `encrypt()` provides no encryption (returns plaintext on the handshake path, errors otherwise), and that the FSK `detectSymbol` infinite-loops while the intended algorithm still fails ~43% of byte values. This closes Track **S2** and stops anyone reviving the code from rediscovering these by accident. Add a top-level `LEGACY.md` and move `src/`/`include/` under a clearly-marked path (or split to an archived repo).
3. **Do not answer the §4 gate as posed — run the staged validation** (memo → discovery calls → weekend spike) and pre-commit the kill criterion. Frame the option space and the recommendation for the operator; don't decide it unilaterally, and don't let internal enthusiasm substitute for an external signal.
4. **Only on a validated yes:** write a design doc that (a) picks the *cheapest sufficient* rung of §3 and justifies why lower rungs don't meet a *measured* requirement; (b) fixes the Nyquist/bin-width + RS + `detectSymbol`-loop defects; (c) ships a **bit-exact conformance corpus** (FSK header + CRC/RS framing golden vectors) *before* any native code; and (d) if native/Rust is chosen, budgets the multi-platform wheel/CI pipeline explicitly. Implement incrementally with tests.
5. **Either way:** update the review doc's §4a/Track L1 and the README Component Status to reflect the decision.

---

## 6. References

- Review + action plan: [`xenocomm-review-and-action-plan-2026-07-06.md`](./xenocomm-review-and-action-plan-2026-07-06.md) (§4a native-transport; Track L1; Track S2 for the dormant crypto).
- C++ kernel: `src/core/error_correction.cpp`, `src/core/compression_algorithms.cpp`, `src/core/ggwave_fsk_adapter.cpp` (+ header `include/xenocomm/core/ggwave_fsk_adapter.h`).
- C++ security cluster (Track S2, related): `src/core/security_manager.cpp:150-167,203-219`, `src/core/secure_transport_wrapper.cpp:99-129`.
- Build wiring: `mcp_server/pyproject.toml`, root `CMakeLists.txt:85` (bindings commented out); `src/CMakeLists.txt:48,190-192` (dylib `0.1.0`).

---

## 7. What changed in this revision (2026-07-06) and why

Re-verified by an independent 10-agent adversarial workflow plus manual reproduction against the real base `origin/main` `e9284d1`. The **evidence of brokenness/dormancy re-confirmed unchanged** (every "this code doesn't work" claim survived). The **decision framing was corrected** in five ways:

1. **Widened the trilemma to a full option space** with a cheapest-sufficient ladder (pure-Python/NumPy → FFI-vendored DSP → surgical Rust hot-path → full Rust rewrite as last resort). The original deleted the dominating cheaper options.
2. **Corrected the "rewrite is free at the margin" argument** — only Reed-Solomon is truly greenfield; the crypto (~900 LOC real OpenSSL) and codec adapters (~700 LOC) are working code a rewrite throws away and risks regressing.
3. **Removed two engineering errors** — "Rust → WASM for free" (PyO3 doesn't target browser WASM) and "short-term FFI to a C `ggwave` core" (no such library in-tree; reintroduces the unsafe surface).
4. **Replaced the binary product gate with staged external validation** + a pre-committed kill criterion; flagged the original gate as sunk-cost-prone.
5. **Added evidence refinements** — the FSK failures are mostly *below*-Nyquist (bin width > symbol spacing) and `detectSymbol` is an infinite loop; RS advertises correction it lacks; `encrypt()` fails closed when unwired. Also corrected a stale-checkout artifact that had made §1's deprioritization look unsupported.

**Discipline for whoever picks this up:** this review chain has been wrong before (it audited a stale tree once — and a working copy 24 commits behind nearly repeated it here; it missed a live bug once; it over-credited an unwired stack once) — each caught only by reproducing against the real HEAD and refusing to trust plausible-looking evidence, including this document's own. Reproduce §2 against a fresh pull, run the §4 validation, and get a human — and a market signal — before spending a build's worth of effort. Default to archive; make anything more expensive earn it.

# Handoff — L1: the native-transport decision (archive / maintain-C++ / rewrite-in-Rust)

**For:** the next Chimera agent to pick up **Track L1** of the XenoComm v2.4.0 review.
**Prepared by:** Chimera — autonomous agent, human-in-the-loop.
**Date:** 2026-07-06
**Companion:** [`xenocomm-review-and-action-plan-2026-07-06.md`](./xenocomm-review-and-action-plan-2026-07-06.md) (§4a, Track L1).

---

## 0. TL;DR — what this handoff is

Track L1 is **a decision, not a coding task.** The XenoComm C++ core (`src/`, `include/`) is dormant legacy — never built or linked by the shipping Python MCP server. Its one genuinely-valuable asset is an acoustic/RF-denied **codec/FSK transport layer** that the MCP layer structurally cannot serve. The decision:

> **Archive it, maintain it in C++, or rewrite its live kernel in Rust?**

The recommendation is conditional: **if — and only if — a constrained-transport regime (acoustic / embedded / RF-denied / bandwidth-or-energy-limited multi-agent) is a real product target, rewrite the live kernel in Rust.** If it is not a target, **archive** the C++ cleanly. "Maintain as C++" is the weakest option and should be rejected (reasoning in §3).

**The gating input is a product decision only the operator can make** (§4). Do not start a rewrite before it is answered.

---

## 1. Why this is open (context)

The v2.4.0 review pivoted XenoComm's story: the shipping product is a pure-Python MCP coordination server; the C++ SDK is legacy. Two adversarial fleet runs (26 agents, then 35 with a blind-recon pass) converged on the C++ picture, and the operator has since:

- **Removed the C++ CI/CD** (commit `4c0f518`) — it only ever built the legacy C++ + the old pybind11 bindings, never the shipping wheel.
- **Marked the C++ legacy/dormant in the docs** (D1, [PR #10]) — the README now demarcates all C++ sections as unmaintained.

So the C++ is now explicitly deprioritized. L1 is the remaining question: **do we ever revive a native transport, and in what language?**

---

## 2. Verified state of the C++ (the evidence)

All findings below were reproduced by the review's fleet against the real tree. Re-verify before acting — *interrogate the instrument.*

| Fact | Evidence |
|---|---|
| **Never built/linked by the product** | `mcp_server/pyproject.toml` (hatchling, deps only `mcp`+`rich`); no `_core`/pybind/ctypes import in `mcp_server/`. The pybind11 wrapper is commented out (`CMakeLists.txt:85`). |
| **Dormant / unmaintained** | `src/` frozen ~2025-05; the C++ CI was removed 2026-07-06. Dylib versioned `0.1.0` vs wheel `2.4.0`. |
| **Real codec layer exists** | CRC32 `src/core/error_correction.cpp:19`; RLE `src/core/compression_algorithms.cpp:57`; a Goertzel-FSK modem `src/core/ggwave_fsk_adapter.cpp:199`. |
| **…but the FSK modem is numerically broken for its own defaults** | Symbols >211 alias past Nyquist; **109/256 byte values fail to round-trip** (V0 scout compiled a standalone replica and measured it). |
| **…and Reed-Solomon is a hollow stub** | `error_correction.cpp` `ResolomonCorrection` emits zero parity (library calls commented out). |
| **Crypto is theater** | `security_manager.cpp:164` `encrypt()` returns **plaintext** with the SSL object never wired to a socket; dead cert-hostname verification (`secure_transport_wrapper.cpp:99-129`); fail-open cipher-suite reporting (`security_manager.cpp:203`). (These are Track **S2** — but they're part of the "what would a revival cost" picture.) |
| **README C++ examples don't compile** | The walkthroughs reference a nonexistent API (`AsyncConfig`, `EmergenceManager` methods). D1 labeled them legacy rather than rewriting them. |

**Net:** the C++ is *dormant, partial, and defective* — neither a working transport nor entirely dead. The codec/FSK concept is real; the implementation does not work as-is.

---

## 3. The trilemma

### Option A — Archive
Split the C++ to an archived repo (or mark it clearly and stop shipping it in the tree). Correct **if** constrained-transport is not a target. Cheapest; removes dead weight, the misleading examples, and the S2 security theater in one move.

### Option B — Maintain as C++ (**reject**)
The codec is numerically broken, Reed-Solomon is a stub, and the crypto is plaintext — **all three need rebuilding regardless.** Maintaining means doing that rebuild *in the harder, memory-unsafe language*, on exactly the untrusted-bytes-off-a-wire / hard-to-patch embedded surface where memory bugs are catastrophic (and where the removed CI's Valgrind suite was the tell). There is no scenario where B beats C once you accept a rebuild is unavoidable.

### Option C — Rewrite the live kernel in Rust (**recommended if the plane is kept**)
Since a rewrite is unavoidable, do it in Rust, **bounded to the genuinely-live kernel** (codec + FSK + a real transport + real crypto — a few files, not the 34K LOC of stubs). Rationale:

1. **Memory safety** on the parse-untrusted-bytes / embedded surface — the exact class of bug the C++ carried.
2. **The crypto is a rebuild anyway** → do it on RustCrypto / `rustls` / `ring` instead of the plaintext C++.
3. **PyO3 + maturin** gives cleaner Python-extension interop than the abandoned pybind11 path (the product is Python).
4. **Rust→WASM** opens an edge/portable agent-mesh future the C++ can't reach cleanly.
5. **Aligns with the operator's existing Rust tooling** (e.g. RTK is a Rust CLI).
6. **Fix the real defects as part of it:** the FSK Nyquist/aliasing bug and the Reed-Solomon stub.

**Incremental path if C is chosen:** Rust wrapper + real crypto/transport first; FFI to a C `ggwave` core short-term; port the DSP last. Don't overstate the current codec as working until the round-trip defect is fixed.

---

## 4. The decision that gates everything (needs the operator)

**Is a constrained-transport regime a real XenoComm target?** Concretely: acoustic / embedded / RF-denied / bandwidth-or-energy-constrained multi-agent signaling — the niche the MCP/JSON-RPC layer *cannot* serve.

- **If NO** → **Option A (archive).** Stop here; no rewrite.
- **If YES** → **Option C (Rust rewrite)**, scoped to the live kernel, with the defects fixed.

The next agent should **not** begin a rewrite before this is answered — it's a product-direction call, not an engineering one. Surface it to the operator as the first step (this is exactly the kind of hard-to-reverse investment decision to confirm before building).

---

## 5. What the next Chimera agent should do

1. **Re-verify §2** against the current tree (the CI is gone now; confirm no C++ revival happened since). Don't trust this doc's `file:line`s without reopening them.
2. **Get the §4 decision** from the operator (target regime: yes/no). Frame the trilemma and the recommendation; don't decide it unilaterally.
3. **If archive:** open a PR that either splits the C++ to an archived repo or adds an unambiguous top-level `LEGACY.md` + moves `src/`/`include/` under a clearly-marked path; also closes Track **S2** by deleting or hard-flagging the plaintext-`encrypt()` / dead-cert-verify security theater so nothing claims security it lacks.
4. **If Rust:** write a design doc first (scope = codec+FSK+transport+crypto kernel only; PyO3/maturin interop; RustCrypto; fix the Nyquist + Reed-Solomon defects; a round-trip test that the current C++ fails). Then implement incrementally with tests. Consider a design bake-off (memory-safety/security, Python+WASM interop, DSP/embedded ecosystem maturity, migration cost) before committing.
5. **Either way:** update the review doc's §4a/Track L1 and the README Component Status to reflect the decision.

---

## 6. References

- Review + action plan: [`xenocomm-review-and-action-plan-2026-07-06.md`](./xenocomm-review-and-action-plan-2026-07-06.md) (§4a native-transport trilemma; Track L1; Track S2 for the dormant crypto).
- C++ kernel: `src/core/error_correction.cpp`, `src/core/compression_algorithms.cpp`, `src/core/ggwave_fsk_adapter.cpp`.
- C++ security cluster (Track S2, related): `src/core/security_manager.cpp:156-166,203`, `src/core/secure_transport_wrapper.cpp:99-129`.
- Build wiring: `mcp_server/pyproject.toml`, root `CMakeLists.txt:85` (bindings commented out).

**Discipline for whoever picks this up:** this review chain has been wrong before (it audited a stale tree once, missed a live bug once, over-credited an unwired stack once) — each caught only by reproducing against the real HEAD and refusing to trust plausible-looking evidence. Reproduce §2, and get the §4 decision from a human before spending a rewrite's worth of effort.

# LEGACY — the archived C++ SDK

**Status: 🗄️ archived / dormant / unmaintained. Not built, not linked, not shipped.**

The original XenoComm C++ SDK now lives under [`legacy/`](./legacy/). It has been
moved here — intact, for posterity — because it is **not part of the shipping
product** and is not maintained. The shipping product is the pure-Python MCP
coordination server in [`mcp_server/`](./mcp_server/) (deps: `mcp`, `rich` — no
native linkage).

This file exists so that nobody revives this code without knowing exactly what
they are picking up. **Do not trust any of it without reproducing against a fresh
checkout first.**

---

## Why it was archived

- **Never built or linked by the product.** The MCP server imports nothing from
  the C++ core; the pybind11 bindings were commented out (`legacy/CMakeLists.txt`),
  the C++ CI/CD was removed early (PR #2), and the compiled dylib versioned `0.1.0`
  while the shipping wheel is `2.4.0`. The two artifacts are fully decoupled.
- **Dormant.** `legacy/src/` was frozen ~2025-05 and the docs already demarcate the
  C++ SDK as legacy (D1, PR #10).
- **Partial and defective.** The transport concept (an acoustic/RF-denied
  codec + FSK modem) is real, but the implementation does not work as-is. See the
  traps below.

The decision to archive — and the option space for any future native transport —
is recorded in
[`docs/audits/L1-native-transport-rust-vs-cpp-handoff.md`](./docs/audits/L1-native-transport-rust-vs-cpp-handoff.md)
(companion review: [`docs/audits/xenocomm-review-and-action-plan-2026-07-06.md`](./docs/audits/xenocomm-review-and-action-plan-2026-07-06.md)).
The short version: **archive is the default; do not rewrite anything unless a
constrained-transport regime is a *validated* product target, and if it ever is,
build the cheapest sufficient thing (pure-Python/NumPy → an FFI-vendored
maintained DSP library → a surgical Rust hot-path) — a from-scratch Rust rewrite
is the evidence-gated last resort, not the default.**

---

## ⚠️ Verified defects — read before reviving anything

These were reproduced against the real tree. They are the reasons this code is a
liability, not an asset:

### Security (Track S2) — `legacy/src/core/security_manager.cpp`, `legacy/src/core/secure_transport_wrapper.cpp`
- **`encrypt()` returns plaintext, not ciphertext** (`security_manager.cpp`, ~line
  164): it returns a verbatim copy of the input (`pseudo_encrypted_data = data`);
  the ciphertext from `SSL_write()` is never read back out. It fails *closed*
  (`"SSL not ready"`) when the SSL is unwired, but it provides **no encryption**
  on any path. The file now carries an in-source SECURITY WARNING banner.
- **Cipher-suite reporting fails open**: `getNegotiatedCipherSuite()` reports
  `AES_256_GCM_SHA384` even when no cipher (or no `ssl_`) exists.
- **Dead certificate/hostname verification** in `secure_transport_wrapper.cpp`.

**Nothing in the security layer should be trusted or reused. Rebuild from scratch
on a vetted TLS stack if ever needed.**

### FSK modem — `legacy/src/core/ggwave_fsk_adapter.cpp`
- **Numerically broken at its own defaults**: 109/256 byte values fail to
  round-trip. ~43 failures are Nyquist aliasing (symbols ≥ 211 exceed 22050 Hz at
  the default 44.1 kHz / 100 Hz spacing); the other ~66 fail *below* Nyquist
  because the Goertzel bin width (~172 Hz) is wider than the 100 Hz symbol spacing,
  so adjacent tones are unresolvable.
- **`detectSymbol()` is an infinite loop** (`for (uint8_t symbol = 0; symbol < 256; ...)`
  — `uint8_t` wraps at 255, so the condition is always true). The modem does not
  terminate as written.

### Reed-Solomon — `legacy/src/core/error_correction.cpp`
- **A hollow stub**: the `reed_solomon_erasure` calls are commented out and parity
  shards are zero-filled, so no real parity is produced or checked. Worse,
  `maxCorrectableErrors()` still advertises `parity_shards/2` correction capability
  it cannot deliver — a latent data-integrity trap. (CRC32 in the same file *is*
  a real implementation.)

---

## Notes for anyone building here

- The CMake build is archived **as-is** and is not expected to function; it is kept
  as a historical artifact. Its `add_subdirectory(docs)` references the repository's
  top-level `docs/` (which was intentionally left at the root), so the doc build
  target will not resolve from `legacy/`.
- Reproduce every claim above against a fresh `git pull` before acting — this review
  chain has been misled by a stale checkout before. Then get the product decision
  (and a market signal) from a human before spending a rewrite's worth of effort.

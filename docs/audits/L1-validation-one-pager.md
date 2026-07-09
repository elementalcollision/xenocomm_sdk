# L1 validation — one-pager

**For:** the operator, to answer the single question that gates the archived native transport.
**Prepared by:** Chimera — autonomous agent, human-in-the-loop.
**Companion:** [`L1-native-transport-rust-vs-cpp-handoff.md`](./L1-native-transport-rust-vs-cpp-handoff.md) (§4). **Default remains: archived.**
**Purpose:** replace a gut yes/no with a cheap, staged, *externally-validated* decision — in **days, not quarters**.

---

## The question

> **Is a constrained-transport regime a real XenoComm target** — agents that must signal where MCP / JSON-RPC over TCP cannot: **acoustic / air-gapped / RF-denied / severely bandwidth- or energy-limited**?

Framing that matters: XenoComm today is a pure-Python MCP coordination server. The C++ acoustic/FSK transport is archived under [`legacy/`](../../legacy/) and is broken as-is (any revival is a rewrite regardless — see the handoff). So this is a **demand question, not a code question.** Do not answer it from internal conviction; answer it with an external signal.

## Part 1 — Use-case memo (write this before spending anything · ~2 hrs)

One honest paragraph each. **A memo you cannot write is itself a "no."**

- **Who** has this problem? A segment, and ideally 2–3 real orgs or people. — *[operator]*
- **What do they use today**, and why is it inadequate? (proprietary RF modems, GNU Radio rigs, sneakernet, nothing…) — *[operator]*
- **Which adjacency**, precisely? Pick one — they favor different builds:
  - ☐ acoustic air-gap (data over sound between co-located machines)
  - ☐ lossy / very-low-bandwidth (satellite, mesh, LoRa-class)
  - ☐ RF-denied / contested / covert
  - ☐ energy-constrained edge (sensors, batteries)
- **Why software-defined** (XenoComm) beats the incumbent — the wedge, in one sentence. — *[operator]*
- **What would they pay or co-fund, and when?** — *[operator]*

## Part 2 — Cheap external validation (days, and any step can end it)

1. **This memo** (1–2 hrs).
2. **3–5 discovery calls** with people in the segment. The goal is not enthusiasm — it is an **external commitment**: a named design partner, a letter of intent, or a co-funded pilot.
3. **A weekend spike** — move real bits over the real channel using an **existing** library (`ggwave` / GNU Radio / `liquid-dsp`), **not** a rewrite. Proves the team can do it at all and de-risks the hardest part (the DSP), for a weekend.

## Part 3 — The kill criterion (pre-commit *now*, before you start)

> **If no external party commits (named partner / LOI / co-funded pilot) within 60 days, the C++ stays archived** and the time goes into the Python product.

Archive is the **reversible, near-zero-cost default** — it's git; it can be un-archived the day a real partner appears. It is the correct outcome, not a failure. A gate with no disconfirming test just limps forward on hope; this one has teeth.

## Part 4 — Only on a validated "yes": build the cheapest sufficient rung

From the handoff §3, in order — stop at the first that meets a *proven* requirement:

**pure-Python / NumPy → FFI-vendored maintained DSP → surgical Rust hot-path → full Rust + PyO3 rewrite (last resort).**

Gate any native build behind a **bit-exact conformance corpus** first, and budget the multi-platform wheel/CI pipeline explicitly — those are the estimate-blowing risks, not the code.

---

## Bottom line

Two–three days of memo + calls + a weekend spike can return a **decisive answer far more cheaply than a quarter of building**. Default to archived; make a native transport **earn its un-archival** with an external signal — then build the cheapest thing that works, not the flashiest.

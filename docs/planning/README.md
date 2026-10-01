# Planning documents

Snapshot of the planning workspace for `attak-iot-firmware`, copied into this repo
so that a fresh clone is self-contained. The live working copy lives in the
author's scratch workspace outside this repo; this directory is what the code and
its comments point at.

Start here: [`intent.md`](intent.md) for why the project exists, then
[`map.md`](map.md) for the decisions, then [`spec.md`](spec.md) for the v1 spec.

## Index

| File | What it is |
|---|---|
| [`intent.md`](intent.md) | Why the project exists: goal, hardware, chosen architecture, v1/v2 scope. |
| [`map.md`](map.md) | Wayfinder map — standing decisions, decisions so far, not-yet-specified, out of scope, and the v2 attack-payload round. |
| [`spec.md`](spec.md) | v1 spec: module bring-up behind the `ModuleStatus` seam plus the live dashboard. Carries a v2 revision note about the seam widening. |
| [`issues/`](issues/) | Child tickets 01–09: pin mapping, project scaffold, dashboard UI/UX, native test infra, CC1101 / NRF24 / PN532 / IR bring-up, RGB status LED. |
| [`research/01-pin-mapping-findings.md`](research/01-pin-mapping-findings.md) | Pin mapping with cited sources — the authority behind [`include/board_pins.h`](../../include/board_pins.h). |

## Ticket status

| Tickets | State |
|---|---|
| 01 pin mapping, 02 scaffold, 03 dashboard UI/UX, 04 native test infra | Done. |
| 05 CC1101, 06 NRF24, 07 PN532, 08 IR bring-up | **Not implemented** — the four module `begin()` bodies are still stubs returning `"not implemented"`. |
| 09 RGB status LED | **Not implemented** — no LED/FastLED source file exists yet. |

Two caveats when reading these tickets:

- `issues/05`–`issues/08` were written **before** the v2 round. `map.md` § v2
  replaced the continuous auto-poll model with an explicit off → on → poll model,
  so **their acceptance criteria are stale** and must be rewritten before the
  work starts.
- The v1 spec's testing decisions call for a native test of the RGB colour
  aggregation; that test does not exist yet, because the aggregation itself does
  not exist yet.

## Provenance

Copied from the workspace scratch directory. If the two copies ever diverge:
`src/` is the truth for **what exists**, and this directory is the truth for
**what was decided**.

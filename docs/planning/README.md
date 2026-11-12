# Planning documents

Snapshot of the planning workspace for `attak-iot-firmware`, copied into this repo
so that a fresh clone is self-contained. The live working copy lives in the
author's scratch workspace outside this repo; this directory is what the code and
its comments point at.

Start here: [`intent.md`](intent.md) for why the project exists, then
[`map.md`](map.md) for the decisions, then [`spec.md`](spec.md) for the v1 spec.

## Index

| File | What it is |
| --- | --- |
| [`intent.md`](intent.md) | Why the project exists: goal, hardware, chosen architecture, v1/v2 scope. |
| [`map.md`](map.md) | Wayfinder map — standing decisions, decisions so far, not-yet-specified, out of scope, and the v2 attack-payload round. |
| [`spec.md`](spec.md) | v1 spec: module bring-up behind the `ModuleStatus` seam plus the live dashboard. Carries a v2 revision note about the seam widening. |
| [`issues/`](issues/) | Child tickets 01–09: pin mapping, project scaffold, dashboard UI/UX, native test infra, CC1101 / NRF24 / PN532 / IR bring-up, RGB status LED. |
| [`research/01-pin-mapping-findings.md`](research/01-pin-mapping-findings.md) | Pin mapping with cited sources — the authority behind [`include/board_pins.h`](../../include/board_pins.h). |
| [`checkout-state.md`](checkout-state.md) | **Read this first on `/mnt/d`.** This tree is a 2026-10-01 snapshot without USB NCM; the live checkout is the WSL-native path. Includes flash/NCM guardrails. |

## Ticket status

| Tickets | State |
| --- | --- |
| 01 pin mapping, 02 scaffold, 03 dashboard UI/UX, 04 native test infra | Done. |
| 05 CC1101, 08 IR bring-up | **Not implemented** — still stub `begin()` bodies returning `"not implemented"`. |
| 07 PN532 | **Implemented in this snapshot** — `Adafruit_PN532` I2C init on GPIO4/5, firmware probe every 1–2 s, hot-plug reset/wakeup, chip/firmware in `detail`. Physical board verification still open (ticket's last checkbox). |
| 06 NRF24 | **Implemented in this snapshot** — RF24 init + constant-carrier jammer (channels 0–125, Start/Stop via dashboard). The v1 ticket's liveness acceptance still applies; the jammer is v2 scope. |
| 09 RGB status LED | **Not implemented** — no LED/FastLED source file exists yet. |

Two caveats when reading these tickets:

- `issues/05`–`issues/08` were written **before** the v2 round. `map.md` § v2
  replaced the continuous auto-poll model with an explicit off → on → poll model,
  so **their acceptance criteria are stale** and must be rewritten before the
  work starts. Exception: 07 has no payload, so it implements only the liveness
  half (continuous probe) and leaves `enabled` false.
- The v1 spec's testing decisions call for a native test of the RGB colour
  aggregation; that test does not exist yet, because the aggregation itself does
  not exist yet.

## Provenance

Copied from the workspace scratch directory. If the two copies ever diverge:
`src/` is the truth for **what exists**, and this directory is the truth for
**what was decided**.

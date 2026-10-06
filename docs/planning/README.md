# Planning documents

Planning workspace for `attak-iot-firmware`. The current specification is
[`spec.md`](spec.md), marked **approved; implementation-plan-ready** for the approved dashboard and
observation release. Start there before writing tasks or implementing changes.

[`intent.md`](intent.md) describes the project direction; [`map.md`](map.md)
preserves the decision history. Their older v1/v2 scope is not an instruction to
implement features outside the current spec.

## Index

| File | What it is |
|---|---|
| [`intent.md`](intent.md) | Why the project exists: goal, hardware, chosen architecture, v1/v2 scope. |
| [`map.md`](map.md) | Wayfinder map — standing decisions, decisions so far, not-yet-specified, out of scope, and the v2 attack-payload round. |
| [`spec.md`](spec.md) | Current approved scope, lifecycle, WebSocket/output/export contracts, resource limits, R01–R21 requirements and AC01–AC12 acceptance criteria, approved through Q1–Q21. Source of truth for the plan. |
| [`issues/`](issues/) | Child tickets 01–09: pin mapping, project scaffold, dashboard UI/UX, native test infra, CC1101 / NRF24 / PN532 / IR bring-up, RGB status LED. |
| [`research/01-pin-mapping-findings.md`](research/01-pin-mapping-findings.md) | Pin mapping with cited sources — the authority behind [`include/board_pins.h`](../../include/board_pins.h). |
| [`../superpowers/plans/2026-10-04-dashboard-completion.md`](../superpowers/plans/2026-10-04-dashboard-completion.md) | The single implementation plan for the current spec (Q1–Q21): 10 tasks with R01–R21/AC01–AC12 traceability. Implemented in source; native tests and the ESP32 build pass. Hardware acceptance (board, NFC cards, IR remote) and the 15 s / 2 s measurements are still pending. |
| [`../../NCM-AP.md`](../../NCM-AP.md) | Operating notes for the two management paths (USB NCM `192.168.7.1` vs AP `192.168.4.1`), verified flash/Serial recipes, and the on-board acceptance record for the evil-twin payload. |

## Ticket status

| Tickets | State |
|---|---|
| 01 pin mapping, 02 scaffold, 03 dashboard UI/UX, 04 native test infra | Done. |
| 05 CC1101, 06 NRF24, 07 PN532, 08 IR bring-up | **Implemented in source** — real health/liveness and enable/disable; hardware acceptance on a board is still pending. The old `"not implemented"` stubs are gone. |
| 09 RGB status LED | **Implemented in source** (`src/core/status_health.*` pure aggregate + `src/core/status_led.*` FastLED wrapper); on-board LED check pending. |

Caveats when reading the tickets and existing plan:

- `issues/05`–`issues/09` criteria have been rewritten to the current spec
  (off/on lifecycle, enabled-only RGB, bounded drivers, AC mapping); the old
  boot-time auto-poll wording is historical and no longer the criteria.
- The plan already follows the current spec's `actionError`, WiFi deadline,
  liveness deadline, error precedence and export schema; the earlier
  "needs reconcile" caveat is resolved.
- Ticket status above records implementation in source, not completion of the
  current release. Host-side verification exists (`pio test -e native` 50/50,
  ESP32 build + LittleFS image, browser smoke); **hardware acceptance (board,
  cards, remote, 15 s / 2 s measurements, 10-minute heap run) has not been
  performed**.

## Provenance

The initial documents were copied from the author's scratch workspace.
For this release, changes and approvals are recorded in this repository:
`src/` is the truth for what exists, `spec.md` is the truth for what must be built,
and `map.md` is the historical record. Do not copy an older scratch spec over the
current approved specification.

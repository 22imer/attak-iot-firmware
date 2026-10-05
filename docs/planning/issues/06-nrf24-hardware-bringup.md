# 06 — NRF24 hardware bring-up

**What to build:** the dashboard shows the real health of the wired NRF24 module, under the
approved enabled/on-demand model — no boot-time auto-poll, no radio traffic.

**Blocked by:** None.

**Status:** implemented-in-source; board verification pending
(`src/modules/nrf24_module.cpp`). Spec: §3, §7.4; AC02, AC12.

- [ ] Boot leaves the module `off`; no action/liveness I/O until enable (spec §3.1).
- [ ] `setEnabled(true)` brings the radio up on the shared SPI bus
      (`PIN_SPI_*` / `PIN_NRF24_CS` / `PIN_NRF24_CE`) and reports `connected: true` only when
      `RF24::begin(&spiBus::instance())` **and** `isChipConnected()` both succeed — the latter
      compares `SETUP_AW`, so a floating MISO fails the check.
- [ ] CE stays low and no `startListening`/`write`/probe packet is ever sent; liveness never
      transmits (§7.4).
- [ ] While enabled, liveness is re-checked (~500 ms) whether connected or not; unplugging
      flips `connected=false`, reinserting returns to `ready` with **no automatic action**;
      failure keeps `enabled=true` (R08).
- [ ] Disable releases the backend in `poll()` (`powerDown()`) and clears `cleanupPending` only
      after that release; enable during cleanup must not start a second bring-up (§3.1, R21).
- [ ] Board (AC02): CC1101 + NRF24 present at the same time must not conflict on the shared
      bus; record measured loss/recovery latency against the 2 s target. If missed, keep the
      limit and file evidence + a spec-revision request.

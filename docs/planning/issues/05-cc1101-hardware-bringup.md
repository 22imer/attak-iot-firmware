# 05 — CC1101 hardware bring-up

**What to build:** the dashboard shows the real health of the wired CC1101 module, under the
approved enabled/on-demand model — not a boot-time auto-poll and not a transmit check.

**Blocked by:** None.

**Status:** implemented-in-source; board verification pending (`src/modules/cc1101_module.cpp`,
`src/modules/spi_bus.*`). Spec: §3, §7.4; AC02, AC12.

- [ ] Boot leaves the module `off` (`enabled=false`, `connected=false`, `detail="off"`); no
      action/liveness I/O runs until enable (spec §3.1).
- [ ] `setEnabled(true)` performs one bounded bring-up on the shared SPI bus
      (`PIN_SPI_*` / `PIN_CC1101_CS`) and reports `connected: true` only when PARTNUM reads
      0x00 **and** VERSION is a plausible non-floating value — never merely "the SPI
      transaction completed".
- [ ] Bounded register access: the identity read must not block the loop (the vendor helper's
      IDF<5 MISO wait can stall ~1 s; do not use it for health). CS returns high on every exit.
- [ ] No `SetTx`/`SetRx`/`SendData` and no carrier or probe packet; liveness never transmits
      (§7.4).
- [ ] While enabled, liveness is re-checked (~500 ms) whether connected or not, so unplugging
      flips `connected=false` and reinserting returns to `ready` **without any automatic
      action**; a live failure keeps `enabled=true` (§3.1, R08).
- [ ] Disable is logical first: cancel/clear payload, ack after the cancel; there is no
      persistent backend to release, so no leftover `cleanupPending`.
- [ ] Board (AC02): record measured loss/recovery latency against the 2 s target together with
      board/core/driver versions. If the target is missed, keep the limit and file evidence +
      a spec-revision request — never relax it to pass.

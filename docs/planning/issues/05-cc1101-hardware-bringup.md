# 05 — CC1101 hardware bring-up

**What to build:** the dashboard shows the real connect/health state of the physically wired CC1101 module in place of the current "not implemented" placeholder.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `begin()` initializes SPI on the shared bus using the `PIN_SPI_*` / `PIN_CC1101_CS` / `PIN_CC1101_GDO0` pins from `board_pins.h`
- [ ] `status()` reports `connected: true` only after reading the chip's PARTNUM/VERSION registers and confirming the expected non-zero chip identity — not merely "the SPI transaction completed"
- [ ] `status()` reports `connected: false` with a descriptive `detail` string when the chip doesn't respond as expected
- [ ] `poll()` re-checks liveness on a fixed interval, so unplugging the module flips `connected` to `false` within one poll cycle, without a reboot
- [ ] Verified on the physical board: dashboard shows CC1101 connected when the module is wired in, and disconnected when it's unplugged

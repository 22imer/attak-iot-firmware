# 06 — NRF24 hardware bring-up

**What to build:** the dashboard shows the real connect/health state of the physically wired NRF24 module in place of the current "not implemented" placeholder.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `begin()` initializes RF24 on the shared SPI bus using the `PIN_SPI_*` / `PIN_NRF24_CS` / `PIN_NRF24_CE` pins from `board_pins.h`
- [ ] `status()` reports `connected: true` only when `RF24::begin()` succeeds AND `RF24::isChipConnected()` confirms the chip is present
- [ ] `status()` reports `connected: false` with a descriptive `detail` string when the chip doesn't respond as expected
- [ ] `poll()` re-checks liveness on a fixed interval, so unplugging the module flips `connected` to `false` within one poll cycle, without a reboot
- [ ] Verified on the physical board: dashboard shows NRF24 connected when the module is wired in, and disconnected when it's unplugged

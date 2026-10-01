# 07 — PN532 hardware bring-up

**What to build:** the dashboard shows the real connect/health state of the physically wired PN532 module in place of the current "not implemented" placeholder.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `begin()` initializes Adafruit_PN532 in I2C mode using the `PIN_PN532_SDA` / `PIN_PN532_SCL` pins from `board_pins.h`
- [ ] `status()` reports `connected: true` only when `getFirmwareVersion()` returns a non-zero value, and includes the chip/firmware info it encodes in `detail`
- [ ] `status()` reports `connected: false` with a descriptive `detail` string when `getFirmwareVersion()` returns zero
- [ ] `poll()` re-checks liveness on a fixed interval, so unplugging the module flips `connected` to `false` within one poll cycle, without a reboot
- [ ] Verified on the physical board: dashboard shows PN532 connected when the module is wired in, and disconnected when it's unplugged

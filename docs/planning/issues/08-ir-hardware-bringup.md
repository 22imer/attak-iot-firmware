# 08 — IR hardware bring-up

**What to build:** the dashboard shows the real RMT-init state of the IR RX/TX pins in place of the current "not implemented" placeholder — with the caveat, surfaced to the user, that IR has no presence handshake the way the SPI/I2C chips do.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `begin()` sets up IRrecv/IRsend on the `PIN_IR_RX` / `PIN_IR_TX` pins from `board_pins.h` via the RMT peripheral
- [ ] `status()` reports `connected: true` when RMT channel acquisition succeeds, `connected: false` when it fails, with a `detail` string that makes clear this reflects "RMT initialized," not "an IR module was detected" (IR has no hardware presence check)
- [ ] `poll()` re-checks RMT channel health on a fixed interval
- [ ] Verified on the physical board: dashboard shows the IR module's init state correctly, and the `detail` string doesn't misleadingly imply hardware presence detection

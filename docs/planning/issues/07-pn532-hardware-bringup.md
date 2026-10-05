# 07 — PN532 hardware bring-up + one-UID read

**What to build:** the dashboard shows the real health of the wired PN532 (I2C) and can read one
UID per command. Not a periodic multi-card loop.

**Blocked by:** None.

**Status:** implemented-in-source; board verification pending (`src/modules/pn532_module.cpp`,
`src/core/nfc_frame.*`, `src/core/nfc_uid_format.*`). Spec: §3, §7.2; AC02, AC07, AC12.

- [ ] Boot leaves the module `off`; no I2C I/O until enable (spec §3.1).
- [ ] `setEnabled(true)` runs the bounded init (`SAMConfiguration` normal mode,
      `SetParameters` with `fAutomaticRATS` off, `RFConfiguration` max-retries) and reports
      `connected: true` only when `GetFirmwareVersion` returns IC=0x32 — an I2C address ACK is
      not presence proof. "No chip" must be distinguishable from "no card".
- [ ] The reader must not use `Adafruit_PN532::readPassiveTargetID()`: it reads a fixed 20-byte
      packet, so 7-byte and 10-byte UIDs are corrupted. Read the full advertised frame with a
      bounded I2C transaction and decode UID lengths 4/7/10 with checksum/length validation.
- [ ] `read_uid` waits up to 5 s for exactly one UID, issuing short bounded probes per poll
      (never blocking the 5 s window); a new command aborts an outstanding transaction per the
      protocol. Timeout → `read_timeout` while health stays `ready`.
- [ ] A read that yields nothing keeps the previous payload and does not invent one
      (§3.2, R09).
- [ ] Disable while waiting cancels the session (old ticket invalid), aborts the outstanding
      transaction with an ACK frame, and exposes `cleanupPending` until release; enable does not
      clear the flag (R17, R21).
- [ ] While enabled and idle, liveness re-checks firmware (~500 ms); recovery returns to
      `ready` with **no automatic read** (R08).
- [ ] Board (AC07): two cards with different UIDs, the same card twice (sequence still
      increments), no card for 5 s, PN532 unplugged (hardware_error ≠ timeout), disable while
      waiting.

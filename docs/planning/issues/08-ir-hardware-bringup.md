# 08 — IR RX bring-up + one-message capture

**What to build:** the dashboard shows the real IR RX driver state and can capture one
non-repeat message per command. RX only this release — no transmit/replay.

**Blocked by:** None.

**Status:** implemented-in-source; board verification pending (`src/modules/ir_module.cpp`,
`src/core/ir_capture_format.*`). Spec: §3, §7.3; AC08, AC09, AC12.

- [ ] Boot leaves the module `off`; the `IRrecv` buffer is allocated once at infrastructure
      build, with no I/O until enable (spec §3.1).
- [ ] Exactly one `IRrecv` instance exists (the library keeps one process-global buffer);
      `PIN_IR_RX` (6) is used, buffer 514 (512 timings + leading gap + overflow detection).
- [ ] `connected: true` means "RX driver initialised"; the `detail` string must say physical
      presence is not detectable, and must not imply a sensor handshake that does not exist
      (§7.4).
- [ ] TX7 stays mapped but **no `IRsend`/transmit/replay path exists** this release (§2.1).
- [ ] `capture` waits up to 10 s for one non-repeat message; a repeat-only decode keeps
      waiting inside the same deadline and never counts as a new button press.
- [ ] Payload carries real protocol name and `value` as `0x`+uppercase hex, or `value: null`
      for UNKNOWN (its numeric value is a synthetic hash); `rawTimingsUs` are microseconds with
      the leading idle gap excluded, max 512. Overflow/too-long → `capture_too_long` without
      silently truncating or overwriting the previous payload (§7.3, R09).
- [ ] On timeout/disable the library buffer is resumed/released; no pointer to the reused raw
      buffer is retained.
- [ ] Board (AC08): two different remote buttons, repeat-only, UNKNOWN, 10 s with no signal,
      >512 timings, disable mid-wait. Injection may cover the >512/overflow edge, labelled as
      such.

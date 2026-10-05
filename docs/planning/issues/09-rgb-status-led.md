# 09 — RGB status LED aggregation

**What to build:** the onboard addressable RGB LED (GPIO48) shows the aggregate **enabled**
health at a glance, without opening the dashboard.

**Blocked by:** None (the pure aggregate is native-testable).

**Status:** implemented-in-source; board verification pending (`src/core/status_health.*`,
`src/core/status_led.*`, wired in `src/main.cpp`). Spec: §7.5; AC11, AC12.

- [ ] The aggregate considers **only enabled categories**; a disabled module whose stale
      `connected` flag is true must not make the LED look healthy.
- [ ] No category enabled → LED **off**.
- [ ] Enabled categories with all `connected` → **green**.
- [ ] At least one enabled category not connected → **yellow**.
- [ ] A capture/read/scan timeout or error does **not** turn the LED yellow while the chip is
      still connected — action outcome is not chip health.
- [ ] Later "all chips present" assumptions are removed: the earlier ticket-09 rule that
      aggregated all four modules even when off is superseded by this enabled-only rule.
- [ ] Native test (`env:native`) covers all-off, enabled-healthy, enabled-degraded, action
      timeout staying healthy, and WiFi error degrading.
- [ ] FastLED drives `PIN_STATUS_RGB_LED` only when the aggregate level changes (no per-loop
      `show()` spam).
- [ ] Board (AC11): the LED visibly changes as modules are enabled/disabled and as an enabled
      chip is lost/recovered; it does not follow browser connect/disconnect.

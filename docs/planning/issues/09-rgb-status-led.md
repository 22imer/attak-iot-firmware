# 09 — RGB status LED aggregation

**What to build:** the onboard addressable RGB LED (GPIO48) reflects the aggregate connect state of the four modules at a glance, without needing to open the dashboard.

**Blocked by:** 04 — Native test infrastructure + config/status JSON tests

**Status:** ready-for-agent

- [ ] A pure aggregation function maps the four modules' `ModuleStatus.connected` values to one color/level (all-connected / some-connected / none-connected), visually distinguishable at all three levels
- [ ] Native test (`env:native`) covers all-connected, some-connected, and none-connected input combinations
- [ ] FastLED drives `PIN_STATUS_RGB_LED` with the aggregated color every loop, alongside the existing WebSocket status broadcast
- [ ] Verified on the physical board: the LED visibly changes color as modules are plugged and unplugged

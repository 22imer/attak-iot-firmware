# 04 — Native test infrastructure + config/status JSON tests

**Status:** ready-for-agent → **done**

**What to build:** a PlatformIO `native` build environment (Unity framework) that compiles and runs the pure logic tests — no ESP32 board, no LittleFS, no network — so config persistence and status-broadcast encoding can be regression-tested from a laptop.

**Blocked by:** None — can start immediately.

- [x] `pio test -e native` builds and runs without a connected board or any hardware dependency
- [x] The JSON⇄`DeviceConfig` mapping in `storage` is extracted into functions that take/return plain data (not `LittleFS.open` directly), callable from native tests
- [x] Config round-trip test: encoding then decoding a `DeviceConfig` reproduces the original values; decoding a missing or partial JSON document falls back to the documented defaults
- [x] `ModuleStatus → WebSocket JSON string` test covers `connected: true`, `connected: false`, and a `detail` string containing characters that need JSON escaping
- [x] The existing ESP32 build (`pio run`) still succeeds unchanged after the extraction

## Implemented (commit `a7ab4d5`)

`DeviceConfig`/`ModuleStatus.detail` changed `String → std::string` (drops the Arduino.h dependency instead of adding a mocking framework — no behavior change, `.c_str()` call sites at `wifi_ap.cpp`/`web_dashboard.cpp` unaffected). JSON encode/decode extracted into `storage_json.cpp` / `module_status_json.cpp` (ArduinoJson only, no LittleFS/network). `env:native` added (PlatformIO native + Unity, `test_build_src` + `build_src_filter` whitelisting only the two portable files) — `default_envs` pinned to the real firmware env so bare `pio run` doesn't try to link `native` as a standalone program.

Verified: `pio test -e native` → 6/6 passed. `pio run` (ESP32 target) → SUCCESS, unchanged behavior.

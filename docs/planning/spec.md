Status: ready-for-agent

> **Revision note (v2):** this spec froze the `ModuleStatus` seam at four fields —
> `{name, connected, detail, lastUpdateMs}` — and said not to widen it. That was
> deliberately changed during the v2 round: `enabled` and `output` were added, for
> the on/off polling model and so payload results do not collide with health text.
> The "do not widen" wording below is the v1 record and no longer matches the code.
> Current shape: `src/core/module_status.h`. See `map.md` § v2.

# attak-iot-firmware — v1: Module connectivity + live dashboard

## Problem Statement

A student building a personal IoT pentest multi-tool (sub-GHz RF via CC1101, 2.4GHz via NRF24L01, NFC via PN532, IR capture/replay) has four purchased breakout modules and an ESP32-S3-N16R8 dev board, but no software tying them together. They want to control the device entirely from a browser — no physical screen or buttons — and they want their own codebase, not a fork of the AGPL-licensed Bruce Device firmware they're using only as an architectural reference. Right now there's a project scaffold with four module stubs that always report "not implemented," a WebSocket-driven dashboard UI with no live data behind it, and no way to tell — without opening a serial monitor — whether a given module is actually wired up and working.

## Solution

Implement real hardware bring-up for all four modules (CC1101, NRF24, PN532, IR RX/TX) behind the existing `ModuleStatus` contract, so each one reports a genuine `connected`/`disconnected` state and a human-readable `detail` string the moment the board boots, and keeps that status live as modules are plugged in, unplugged, or fail. Drive the onboard addressable RGB LED (GPIO48) as an at-a-glance aggregate indicator. The dashboard (already built and prototyped) starts showing real status instead of placeholder "not implemented" text. No pentest actions (sniff, replay, clone, jam) ship in this version — v1 is strictly "is it there, and is it healthy."

## User Stories

1. As the device owner, I want the firmware to bring up its own WiFi AP on boot, so that I can connect to the dashboard without any other network infrastructure.
2. As the device owner, I want the AP SSID/password to come from persisted config (`/config.json` on LittleFS) with sane defaults, so that I don't have to reflash firmware to change them.
3. As the device owner, I want to open the dashboard in a browser and immediately see all four modules listed, so that I know at a glance what hardware the firmware recognizes.
4. As the device owner, I want the CC1101 module's real connect state (not "not implemented") shown on the dashboard, so that I know whether my sub-GHz RF hardware is wired correctly.
5. As the device owner, I want the NRF24 module's real connect state shown on the dashboard, so that I know whether my 2.4GHz hardware is wired correctly.
6. As the device owner, I want the PN532 module's real connect state shown on the dashboard, so that I know whether my NFC hardware is wired correctly.
7. As the device owner, I want the IR module's real init state shown on the dashboard, so that I know whether the IR RX/TX GPIOs initialized successfully.
8. As the device owner, I want each module's status to update automatically without refreshing the page, so that I can plug/unplug a module and watch the dashboard react live.
9. As the device owner, I want a short human-readable `detail` string per module (e.g. chip ID read, error reason), so that I have a starting point for debugging a module that won't connect.
10. As the device owner, I want the onboard RGB LED to reflect overall module health (e.g. all connected vs. some missing) without opening the dashboard, so that I get a physical at-a-glance signal.
11. As the device owner, I want to select any module in the dashboard sidebar and see its full detail panel, so that I can drill into one module's status without the others cluttering the view.
12. As the device owner, I want a running event log of every status change across all four modules, so that I can see the history of connects/disconnects during a debugging session, not just the current snapshot.
13. As the device owner, I want the dashboard to clearly indicate whether it's showing live device data versus simulated/no data, so that I don't mistake stale UI for a working connection.
14. As the device owner, I want the dashboard to work with no login, so that I don't need to manage credentials for a device on my own private AP.
15. As the device owner, I want multiple browser tabs/devices connected to the same AP to all see the same live status, so that I'm not limited to one dashboard session at a time.
16. As the device owner, I want a module that fails to initialize at boot to still show as "disconnected" rather than crash or hang the firmware, so that one broken module doesn't take down the whole device.
17. As the device owner, I want a module that initializes but later stops responding to eventually flip to "disconnected" on the dashboard, so that the status reflects live hardware state, not just a one-time boot check.
18. As a developer extending this firmware later, I want each module's hardware I/O fully contained behind its existing `begin()/poll()/status()` functions, so that adding real pentest features later doesn't require touching the dashboard or status-broadcast code.
19. As a developer extending this firmware later, I want the WebSocket JSON message shape and the `ModuleStatus` struct to stay exactly as scaffolded, so that the dashboard prototype already built and visually verified keeps working unmodified.
20. As a developer verifying this firmware, I want the pure logic (status→JSON encoding, config JSON encode/decode, RGB color aggregation) runnable and testable on a native (non-ESP32) build, so that I can catch regressions without flashing hardware every time.

## Implementation Decisions

- **Modules to build**: `src/modules/cc1101_module.cpp`, `nrf24_module.cpp`, `pn532_module.cpp`, `ir_module.cpp` — replace the current `not implemented` stub bodies with real hardware bring-up. Public interface per module (`begin()`, `poll()`, `status() -> ModuleStatus`) is unchanged — this is the seam confirmed with the user, and callers keep going through `status()`. The `ModuleStatus` struct itself was frozen at `{name, connected, detail, lastUpdateMs}` for v1 and later widened to `{name, enabled, connected, detail, output, lastUpdateMs}` in v2 (see the revision note above).
- **CC1101**: `begin()` initializes SPI via the shared bus pins (`PIN_SPI_SCK/MOSI/MISO`) and `PIN_CC1101_CS`/`PIN_CC1101_GDO0` from `include/board_pins.h`, using SmartRC-CC1101-Driver-Lib. Liveness check: read the chip's PARTNUM/VERSION status registers; `connected = true` only once both reads return the expected non-zero chip identity, not just "SPI transaction completed."
- **NRF24**: `begin()` initializes RF24 on the shared SPI bus with `PIN_NRF24_CS`/`PIN_NRF24_CE`. Liveness check: `RF24::begin()` return value AND `RF24::isChipConnected()`.
- **PN532**: `begin()` initializes Adafruit_PN532 in I2C mode on `PIN_PN532_SDA/SCL`. Liveness check: `getFirmwareVersion()` — zero return means not connected, non-zero encodes chip/firmware info usable as `detail`.
- **IR**: `begin()` sets up IRrecv/IRsend on `PIN_IR_RX/PIN_IR_TX` via the RMT peripheral. IR has no hardware presence/liveness handshake (unlike the SPI/I2C chips) — `connected` here means "RMT channels acquired successfully," not "a physical IR module was detected." State this distinction in the module's `detail` string so the dashboard doesn't imply a false positive/negative.
- **poll()**: each module's `poll()` re-runs its liveness check on a fixed interval (implementer's choice of interval, short enough to feel "live" on the dashboard, long enough not to saturate the shared SPI bus) and updates its `ModuleStatus` in place; a module that stops responding must flip `connected` to `false` within one poll cycle, not stay latched `true`.
- **RGB status LED**: new logic (not yet built) aggregates the four `ModuleStatus.connected` values into one color driven via FastLED on `PIN_STATUS_RGB_LED` (GPIO48) — e.g. all-connected vs. some-connected vs. none-connected, updated every loop alongside the existing WebSocket broadcast. Exact color mapping is an implementer decision; must be visually distinguishable at all three levels.
- **Config load/save**: extract the existing JSON⇄`DeviceConfig` mapping in `src/core/storage.cpp` into pure functions that don't call `LittleFS.open` directly (e.g. take/return a `String` or `Stream`), so the mapping logic is unit-testable on a native build; the thin LittleFS read/write wrapper stays ESP32-only and untested.
- **WebSocket JSON schema, WiFi AP mode, shared SPI bus, PN532-I2C, storage backend (LittleFS), license posture (GPL-2.0, accepted for RF24), no dashboard auth**: all already decided in the wayfinder map (`docs/planning/map.md`) and already implemented in the scaffold — unchanged by this spec, listed here for completeness, not re-litigated.
- **Dashboard**: already built and prototyped (`data/index.html`, sidebar + detail panel + event log layout, verified via agent-browser screenshots in the prior session) — no further UI decisions needed for v1; it already consumes the real WebSocket message shape the modules will now actually produce.

## Testing Decisions

- **Seam**: the `ModuleStatus` struct is the one seam for this spec. Everything downstream of it (WebSocket JSON encoding, config JSON encode/decode, RGB color aggregation) is pure logic that takes/produces plain data and is tested with synthetic `ModuleStatus`/`DeviceConfig` values — no hardware required.
- **Native tests**: add a PlatformIO `env:native` test environment (Unity framework) exercising: (a) `ModuleStatus → WebSocket JSON string` shape for connected/disconnected/edge-case detail strings; (b) `DeviceConfig ⇄ JSON` round-trip including a missing/partial config file falling back to defaults; (c) the RGB aggregation function across all-connected/some-connected/none-connected input combinations. This is new test infrastructure for the project — no prior art to follow in-repo; keep it minimal (Unity's plain assert style, no mocking framework).
- **Hardware-in-the-loop**: the actual SPI/I2C/RMT transactions inside each module's `begin()`/`poll()` cannot be meaningfully unit-tested without the physical board — verify by flashing to the real ESP32-S3-N16R8 with each module physically connected, then observing the dashboard show `connected: true` with a plausible `detail` string per module, and `connected: false` when a module is unplugged. This is a manual smoke test per module, not an automated one.
- **Dashboard**: already visually verified against the mocked/simulated data path via agent-browser (screenshots captured in the prior session) — re-verify only that real WebSocket messages (once modules are live) render identically to the mock ones already checked; no new dashboard test infrastructure needed.

## Out of Scope

- Any pentest-specific action per module — RF sniff/replay, NFC read/clone, IR capture/replay, NRF24 sniff/jam. Explicitly deferred past v1 per the wayfinder map's "Out of scope."
- Custom PCB design/fabrication — hardware is the purchased ESP32-S3-N16R8 board plus discrete breakout modules, wired by hand.
- WiFi STA mode / captive-portal provisioning — v1 is AP-only.
- Dashboard authentication.
- OTA firmware updates and battery/power management — both still open in the wayfinder map's "Not yet specified," neither blocks this spec.
- Any change to the WebSocket JSON schema or dashboard layout — both already decided and built; this spec is scoped to making the data behind them real. (The `ModuleStatus` struct shape is the one exception, recorded in the revision note above.)

## Further Notes

- Full decision history and rationale: `docs/planning/map.md` (wayfinder map) and its child tickets (`docs/planning/issues/01-pin-mapping.md`, `02-project-scaffold-architecture.md`, `03-dashboard-ui-ux.md`).
- Project intent/overview: `docs/planning/intent.md`.
- Dashboard prototype primary source (all 3 UI variants + switcher, pre-fold-in): branch `prototype/dashboard-ui` in `attak-iot-firmware`.
- Pin mapping source of truth: `include/board_pins.h`, cross-referenced against `docs/planning/research/01-pin-mapping-findings.md`.
- Repo license is GPL-2.0 (`attak-iot-firmware/LICENSE`) due to the RF24 dependency; any new dependency added while implementing this spec should be checked against that before adding.

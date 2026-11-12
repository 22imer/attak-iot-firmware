# AGENTS.md — attak-iot-firmware

## Which tree is real

This `/mnt/d` checkout is a **2026-10-01 snapshot**. The live checkout is
`/home/duck/school_proj/Attk_IOT/attak-iot-firmware` (WSL-native ext4). Read
[`docs/planning/checkout-state.md`](docs/planning/checkout-state.md) before
editing: it lists what exists only in the native tree (USB NCM, module runtime,
full PN532/NRF24/IR implementations) and the flash/NCM guardrails.

## Read before implementing

- Planning index, ticket status, and the stale-acceptance-criteria warning:
  [`docs/planning/README.md`](docs/planning/README.md).
- Pin map: [`include/board_pins.h`](include/board_pins.h) — PN532 I2C on
  SDA=GPIO4/SCL=GPIO5, RST=GPIO16, IRQ=GPIO15.
- PN532 wiring and bring-up notes: [`docs/pn532-i2c-wiring.md`](docs/pn532-i2c-wiring.md).

## Checks

- `tools/check.sh` — native tests + release build + i2c-scan build (exactly
  what CI runs).
- On `/mnt/*`, build through `tools/wslenv-build.sh <pio args…>`; it copies the
  tree to ext4 first because PlatformIO on 9p is several times slower.
- I2C wiring scan: `pio run -e i2c-scan -t upload` (diagnostic, not a liveness
  check — PN532 sleeps at power-up).

## Standing decisions

- Headless: no display hardware, the UI is the web dashboard.
- License: reference `firmware/` (Bruce, AGPL-3.0) only, never copy code; this
  project is GPL-2.0 because of RF24. Audit: `docs/planning/map.md` § License.
- Review rules: [`CODING_STANDARDS.md`](CODING_STANDARDS.md).

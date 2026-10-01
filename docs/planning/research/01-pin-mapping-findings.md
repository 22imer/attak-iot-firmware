# Pin mapping — ESP32-S3-N16R8 dev board (16MB flash / 8MB octal PSRAM)

**Correction (superseding the original version of this file):** the physical board is confirmed via its own product-listing photo (filename `n16r82-esp32-s3-n16r8-development-board-16mb-flash-8mb-psram-dual-usb-c-3-in-delhi-india-2.webp`) to be an **ESP32-S3-N16R8** dev board — not the earlier "YD-ESP32-S3 / VCC-GND Studio" identification, which was based on a different, less authoritative pinout image (`pin_layout.jpeg`) the user later confirmed did not match the board they actually have. GPIO4–14 and the strapping/USB/UART pin assignments are identical between both images, so only one line item changes: the onboard **RGB LED moved from GPIO47 to GPIO48** (see below).

## Pin assignment

| Module | Signal | GPIO | Rationale |
|---|---|---|---|
| Shared SPI bus (CC1101 + NRF24) | SCK | 12 | Native FSPICLK IOMUX pin (board silkscreen: `FSPICLK`) |
| | MOSI | 11 | Native FSPID |
| | MISO | 13 | Native FSPIQ |
| CC1101 | CS | 10 | Native FSPICS0 |
| CC1101 | GDO0 | 8 | Free GPIO, no special function |
| CC1101 | GDO2 | unused (-1) | Optional; Bruce's own driver defaults `CC1101_GDO2_PIN` to -1 (`firmware/src/modules/rf/rf_utils.h` / `configPins.h`) — GDO0 alone is sufficient |
| NRF24 | CS | 14 | Free GPIO (board silkscreen `FSPIWP`, unused function here) |
| NRF24 | CE | 9 | Free GPIO (board silkscreen `FSPIHD`, unused function here) |
| PN532 (I2C) | SDA | 4 | I2C is routed via GPIO matrix on ESP32-S3, no fixed IOMUX requirement |
| PN532 (I2C) | SCL | 5 | |
| IR | RX | 6 | RMT peripheral is GPIO-matrix-routed on ESP32-S3, no fixed pin requirement |
| IR | TX | 7 | |
| Status | RGB LED | 48 (onboard, already wired) | Board has an onboard addressable RGB LED (silkscreen labels GPIO48 `SPICLK_N RGB_LED` on the authoritative N16R8 image) — reuse it instead of adding a discrete status LED |

## Caution: GPIO33–37 shown on the diagram but likely PSRAM-reserved

The N16R8 image's silkscreen lists GPIO26, 33, 34, 35, 36, 37 as breakout pins. Per Espressif's ESP32-S3 hardware design guidelines, GPIO33–37 are occupied internally when a module uses octal PSRAM/flash — which an "R8" (8 MB octal PSRAM) module does. This mapping does not use any pin in that range, so it is unaffected either way; flagged here so a later session doesn't reach for GPIO33–37 based on the board's own (likely copy-paste, not adjusted per variant) diagram.

## Reserved / do not use

- **GPIO0, GPIO3, GPIO45, GPIO46** — the four ESP32-S3 strapping pins (boot mode / VDD_SPI voltage / ROM log control), confirmed via Espressif's official ESP32-S3 datasheet and hardware design guidelines. Board silkscreen labels these `BOOT`, `JTAG`, `VSPI`, `LOG` respectively.
- **GPIO19 (USB_D-), GPIO20 (USB_D+)** — routed to the board's native-USB `USB` connector.
- **GPIO43 (U0TXD), GPIO44 (U0RXD)** — routed through the onboard CH343 USB-UART bridge to the board's `UART` connector; this is the flashing/serial-monitor path and must stay free.

## Spare for future use

GPIO1, 2, 15, 16, 17, 18, 21, 47. (GPIO26, 33, 34, 35, 36, 37 avoided — see PSRAM caution above.)

## Sources

- User-supplied board product-listing photo (filename above) — primary source for the confirmed N16R8 identity, exposed-pin list, and onboard RGB LED at GPIO48.
- `pin_layout.jpeg` (earlier, superseded image) — GPIO4–14 and strapping/USB/UART assignments cross-checked as identical between both images.
- Espressif ESP32-S3 Datasheet, §1.3.9 Strapping Pins — https://documentation.espressif.com/esp32-s3_datasheet_en.html
- Espressif ESP-IDF GPIO & RTC GPIO reference (ESP32-S3) — https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html
- esptool Boot Mode Selection (ESP32-S3) — https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html
- `firmware/boards/reaper/pins_arduino.h` and `firmware/src/modules/rf/rf_utils.h` (this repo) — working precedent that IRremoteESP8266 and the SmartRC-CC1101 driver operate correctly on ESP32-S3, and that CC1101's GDO2 is optional.

// Pin mapping for the ESP32-S3-N16R8 board (16MB flash / 8MB octal PSRAM;
// confirmed via the board's own product-listing image, not the earlier
// mis-identified "YD-ESP32-S3" diagram).
// Source of truth: docs/planning/research/01-pin-mapping-findings.md
#pragma once

#include <Arduino.h>

// Shared SPI bus — CC1101 + NRF24
constexpr gpio_num_t PIN_SPI_SCK = GPIO_NUM_12;
constexpr gpio_num_t PIN_SPI_MOSI = GPIO_NUM_11;
constexpr gpio_num_t PIN_SPI_MISO = GPIO_NUM_13;

// CC1101
constexpr gpio_num_t PIN_CC1101_CS = GPIO_NUM_10;
constexpr gpio_num_t PIN_CC1101_GDO0 = GPIO_NUM_8;
// GDO2 intentionally unused — matches Bruce's own default (-1), GDO0 alone
// is sufficient for RX/TX signalling.

// NRF24
constexpr gpio_num_t PIN_NRF24_CS = GPIO_NUM_14;
constexpr gpio_num_t PIN_NRF24_CE = GPIO_NUM_9;

// PN532 (I2C mode)
constexpr gpio_num_t PIN_PN532_SDA = GPIO_NUM_4;
constexpr gpio_num_t PIN_PN532_SCL = GPIO_NUM_5;

// IR
constexpr gpio_num_t PIN_IR_RX = GPIO_NUM_6;
constexpr gpio_num_t PIN_IR_TX = GPIO_NUM_7;

// Status — reuses the board's onboard addressable RGB LED, already wired.
constexpr gpio_num_t PIN_STATUS_RGB_LED = GPIO_NUM_48;

// Reserved — do not repurpose:
//   GPIO0, GPIO3, GPIO45, GPIO46  — ESP32-S3 strapping pins
//   GPIO19, GPIO20                — native USB D-/D+
//   GPIO43, GPIO44                — UART0 via onboard CH343 (flashing/monitor)
//
// CAUTION: GPIO33–37 are shown as breakout pins on this board's diagram, but
// on an N16R8 (octal PSRAM) module these are documented by Espressif as
// internally reserved for the PSRAM bus. Do not use them for peripherals —
// this mapping already avoids them.

#include "pn532_module.h"

#include <Adafruit_PN532.h>
#include <Wire.h>

#include <cstdio>
#include <memory>

#include "board_pins.h"

// PN532 NFC reader in I2C mode. The Adafruit driver owns the host protocol:
// begin() pulses RSTPD_N and issues SAMConfig to wake the chip, and
// getFirmwareVersion() returns 0 when nothing answers at I2C 0x24 (or the
// frame is malformed). An I2C address scan is not a valid presence check —
// the chip is asleep after power-up (docs/pn532-i2c-wiring.md §5).
namespace pn532 {

namespace {
// Liveness cadence. A probe on a missing module burns up to two 100ms
// waitready timeouts (firmware version + recovery begin()), so back off while
// absent; both intervals stay well under a dashboard refresh.
constexpr uint32_t kProbeIntervalMs = 1000;        // chip responding
constexpr uint32_t kMissingProbeIntervalMs = 2000; // no response

constexpr const char *kNotResponding =
    "not responding at I2C 0x24 (check wiring, 3V3, SDA/SCL, SEL0=ON, SEL1=OFF)";

ModuleStatus currentStatus = {"pn532", false, false, "not implemented", "", 0};

// Created inside begin(): the Adafruit_PN532 constructor calls pinMode() on
// the IRQ/RST pins, which must not run during static initialization before
// setup() (docs/pn532-i2c-wiring.md §5).
std::unique_ptr<Adafruit_PN532> nfc;
uint32_t lastProbeMs = 0;

// getFirmwareVersion() packs the chip's four ID bytes: IC | Ver | Rev | Support.
void applyFirmwareVersion(uint32_t version) {
    if (version == 0) {
        currentStatus.connected = false;
        currentStatus.detail = kNotResponding;
        return;
    }

    char detail[64];
    std::snprintf(detail, sizeof detail,
                  "PN532 v%u.%u (IC 0x%02X, support 0x%02X)",
                  static_cast<unsigned>((version >> 16) & 0xFF),
                  static_cast<unsigned>((version >> 8) & 0xFF),
                  static_cast<unsigned>((version >> 24) & 0xFF),
                  static_cast<unsigned>(version & 0xFF));
    currentStatus.connected = true;
    currentStatus.detail = detail;
}

uint32_t readFirmwareVersion() { return nfc ? nfc->getFirmwareVersion() : 0; }

// A failed read re-runs the full init sequence so a module soldered/plugged in
// after boot gets its reset pulse and SAMConfig wakeup; a present chip reaches
// normal mode once and then only answers the version command.
void probe() {
    uint32_t version = readFirmwareVersion();
    if (version == 0 && nfc) {
        nfc->begin();
        version = readFirmwareVersion();
    }
    applyFirmwareVersion(version);
}
} // namespace

void begin() {
    Wire.begin(static_cast<int>(PIN_PN532_SDA), static_cast<int>(PIN_PN532_SCL));
    // The Arduino ESP32 toolchain builds as gnu++11, so no std::make_unique.
    nfc.reset(new Adafruit_PN532(static_cast<uint8_t>(PIN_PN532_IRQ),
                                 static_cast<uint8_t>(PIN_PN532_RST), &Wire));
    nfc->begin(); // reset + wakeup; absence is reported by the probe below
    applyFirmwareVersion(readFirmwareVersion());
    lastProbeMs = millis();
    currentStatus.lastUpdateMs = lastProbeMs;
}

void poll() {
    const uint32_t now = millis();
    const uint32_t interval =
        currentStatus.connected ? kProbeIntervalMs : kMissingProbeIntervalMs;
    if (now - lastProbeMs < interval) return;

    lastProbeMs = now;
    probe();
    currentStatus.lastUpdateMs = now;
}

ModuleStatus status() { return currentStatus; }

} // namespace pn532

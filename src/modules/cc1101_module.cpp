#include "cc1101_module.h"

#include <SPI.h>
#include <cstdio>

#include "board_pins.h"
#include "core/module_runtime.h"
#include "modules/spi_bus.h"

namespace cc1101 {

namespace {

ModuleRuntime runtime("cc1101");

constexpr uint32_t kHealthRecheckMs = 500;
constexpr uint32_t kSoReadyTimeoutUs = 5000;
constexpr uint32_t kSpiHz = 2000000;
constexpr uint8_t kPartNum = 0x30;   // status register: 0x00 on a real CC1101
constexpr uint8_t kVersion = 0x31;   // status register: 0x14 on a real CC1101
constexpr uint8_t kReadBurst = 0xC0; // status/config registers are burst-read

uint32_t lastHealthMs = 0;

// Bounded status-register read. Deliberately avoids SmartRC's getCC1101()/
// checkMISO(), whose IDF<5 wait can block the loop for up to 1s and which only
// tests VERSION > 0 (a floating MISO reads 0xFF and would pass).
bool readStatusRegister(uint8_t address, uint8_t &value) {
    SPI.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CC1101_CS, LOW);

    const uint32_t start = micros();
    while (digitalRead(PIN_SPI_MISO)) {
        if (static_cast<uint32_t>(micros() - start) >= kSoReadyTimeoutUs) {
            digitalWrite(PIN_CC1101_CS, HIGH);
            SPI.endTransaction();
            return false;
        }
    }
    SPI.transfer(address | kReadBurst);
    value = SPI.transfer(0x00);

    digitalWrite(PIN_CC1101_CS, HIGH);
    SPI.endTransaction();
    return true;
}

// Presence = PARTNUM == 0x00 and VERSION is a plausible non-floating byte.
bool probeChip(uint8_t &version) {
    uint8_t partNum = 0xFF;
    version = 0x00;
    if (!readStatusRegister(kPartNum, partNum) || partNum != 0x00) return false;
    if (!readStatusRegister(kVersion, version)) return false;
    return version != 0x00 && version != 0xFF;
}

void describe(uint8_t version, char *buffer, size_t size) {
    snprintf(buffer, size, "cc1101 ok (ver 0x%02X)", version);
}

} // namespace

void begin() { spiBus::begin(); }

CommandError setEnabled(bool enabled) {
    const uint32_t now = millis();

    if (enabled) {
        if (runtime.status().enabled) return CommandError::None; // idempotent
        runtime.setEnabled(true, now);

        uint8_t version = 0;
        char detail[32];
        lastHealthMs = now;
        if (!probeChip(version)) {
            runtime.setHealth(false, "cc1101 not responding", now);
            return CommandError::HardwareError;
        }
        describe(version, detail, sizeof(detail));
        runtime.setHealth(true, detail, now);
        return CommandError::None;
    }

    runtime.setEnabled(false, now, false); // no persistent backend to release
    return CommandError::None;
}

CommandError handleAction(ActionId) { return CommandError::UnsupportedAction; }

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) runtime.finishCleanup(now);
    if (!runtime.status().enabled) return;
    if (static_cast<uint32_t>(now - lastHealthMs) < kHealthRecheckMs) return;
    lastHealthMs = now;

    uint8_t version = 0;
    char detail[32];
    if (!probeChip(version)) {
        runtime.setHealth(false, "cc1101 not responding", now);
        return;
    }
    describe(version, detail, sizeof(detail));
    runtime.setHealth(true, detail, now);
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

} // namespace cc1101

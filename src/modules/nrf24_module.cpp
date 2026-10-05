#include "nrf24_module.h"

#include <RF24.h>

#include "board_pins.h"
#include "core/module_runtime.h"
#include "modules/spi_bus.h"

namespace nrf24 {

namespace {

ModuleRuntime runtime("nrf24");

constexpr uint32_t kHealthRecheckMs = 500;

// Constructor is I/O-free; the single shared bus is passed at begin().
RF24 radio(static_cast<uint16_t>(PIN_NRF24_CE), static_cast<uint16_t>(PIN_NRF24_CS));
bool initialized = false;
uint32_t lastHealthMs = 0;

bool probeChip() { return radio.isChipConnected(); }

} // namespace

void begin() { spiBus::begin(); }

CommandError setEnabled(bool enabled) {
    const uint32_t now = millis();

    if (enabled) {
        if (runtime.status().enabled) return CommandError::None; // idempotent
        runtime.setEnabled(true, now);
        if (runtime.status().cleanupPending) return CommandError::None;

        lastHealthMs = now;
        if (radio.begin(&spiBus::instance()) && probeChip()) {
            initialized = true;
            runtime.setHealth(true, "nrf24 ok", now);
            return CommandError::None;
        }
        runtime.setHealth(false, "nrf24 not responding", now);
        return CommandError::HardwareError;
    }

    runtime.setEnabled(false, now, initialized); // powerDown() released in poll()
    return CommandError::None;
}

CommandError handleAction(ActionId) { return CommandError::UnsupportedAction; }

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) {
        if (initialized) {
            radio.powerDown();
            initialized = false;
        }
        runtime.finishCleanup(now);
    }
    if (!runtime.status().enabled) return;
    if (static_cast<uint32_t>(now - lastHealthMs) < kHealthRecheckMs) return;
    lastHealthMs = now;

    if (!probeChip()) {
        runtime.setHealth(false, "nrf24 not responding", now);
        return;
    }
    runtime.setHealth(true, "nrf24 ok", now);
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

} // namespace nrf24

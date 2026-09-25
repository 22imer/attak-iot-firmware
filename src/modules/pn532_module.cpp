#include "pn532_module.h"

#include "board_pins.h"

namespace pn532 {

namespace {
ModuleStatus currentStatus = {"pn532", false, "not implemented", 0};
}

void begin() {
    // Not implemented — Adafruit_PN532 I2C init against
    // PIN_PN532_SDA/PIN_PN532_SCL from include/board_pins.h belongs to a
    // follow-up implementation ticket.
}

void poll() { currentStatus.lastUpdateMs = millis(); }

ModuleStatus status() { return currentStatus; }

} // namespace pn532

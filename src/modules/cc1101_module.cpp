#include "cc1101_module.h"

#include "board_pins.h"

namespace cc1101 {

namespace {
ModuleStatus currentStatus = {"cc1101", false, "not implemented", 0};
}

void begin() {
    // Not implemented — SPI + SmartRC-CC1101-Driver-Lib init against
    // PIN_SPI_*/PIN_CC1101_* from include/board_pins.h belongs to a
    // follow-up implementation ticket, not this scaffold.
}

void poll() { currentStatus.lastUpdateMs = millis(); }

ModuleStatus status() { return currentStatus; }

} // namespace cc1101

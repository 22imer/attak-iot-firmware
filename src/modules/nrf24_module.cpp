#include "nrf24_module.h"

#include "board_pins.h"

namespace nrf24 {

namespace {
ModuleStatus currentStatus = {"nrf24", false, "not implemented", 0};
}

void begin() {
    // Not implemented — RF24 lib init against PIN_SPI_*/PIN_NRF24_* from
    // include/board_pins.h belongs to a follow-up implementation ticket.
}

void poll() { currentStatus.lastUpdateMs = millis(); }

ModuleStatus status() { return currentStatus; }

} // namespace nrf24

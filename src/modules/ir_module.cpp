#include "ir_module.h"

#include "board_pins.h"

namespace ir {

namespace {
ModuleStatus currentStatus = {"ir", false, "not implemented", 0};
}

void begin() {
    // Not implemented — IRrecv/IRsend init against PIN_IR_RX/PIN_IR_TX from
    // include/board_pins.h belongs to a follow-up implementation ticket.
}

void poll() { currentStatus.lastUpdateMs = millis(); }

ModuleStatus status() { return currentStatus; }

} // namespace ir

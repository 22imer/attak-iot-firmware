#include "ir_module.h"

#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>

#include "board_pins.h"
#include "core/ir_capture_format.h"
#include "core/module_runtime.h"

namespace ir {

namespace {

ModuleRuntime runtime("ir");

constexpr uint16_t kIrBufferSize = 514; // 512 timings + leading gap + overflow detection
constexpr uint32_t kCaptureDeadlineMs = 10000;
constexpr uint32_t kIrTickUs = 2; // library kRawTick; its IRrecv.h comment ".5us" is stale

IRrecv *receiver = nullptr;
decode_results results;
uint32_t activeTicket = 0;
bool rxEnabled = false;

void stepCapture(uint32_t now) {
    if (runtime.expire(now, ActionError::CaptureTimeout, false)) {
        activeTicket = 0;
        return;
    }
    if (!receiver->decode(&results)) return;

    if (results.repeat || results.rawlen <= 1) {
        receiver->resume();
        return;
    }

    const bool numericValue = results.decode_type != UNKNOWN && results.bits > 0;
    std::string json = runtime.status().output; // only replaced on FormatResult::Ready
    const irCapture::FormatResult result =
        irCapture::format(false, results.overflow, results.rawbuf, results.rawlen, kIrTickUs,
                          typeToString(results.decode_type).c_str(), numericValue, results.value, json);
    receiver->resume();

    if (result == irCapture::FormatResult::Ready) {
        runtime.completeAction(activeTicket, std::move(json), now);
        activeTicket = 0;
    } else if (result == irCapture::FormatResult::TooLong) {
        runtime.failAction(activeTicket, ActionError::CaptureTooLong, now, false);
        activeTicket = 0;
    }
}

} // namespace

void begin() {
    // Infrastructure only: allocates the RX buffer, no interrupt/chip I/O.
    receiver = new IRrecv(static_cast<uint16_t>(PIN_IR_RX), kIrBufferSize);
}

CommandError setEnabled(bool enabled) {
    const uint32_t now = millis();

    if (enabled) {
        if (runtime.status().enabled) return CommandError::None; // idempotent
        runtime.setEnabled(true, now);
        if (runtime.status().cleanupPending) return CommandError::None;

        receiver->enableIRIn();
        rxEnabled = true;
        runtime.setHealth(true, "ir rx initialized (physical presence not detectable)", now);
        return CommandError::None;
    }

    runtime.setEnabled(false, now, rxEnabled);
    activeTicket = 0;
    return CommandError::None;
}

CommandError handleAction(ActionId action) {
    if (action != ActionId::Capture) return CommandError::UnsupportedAction;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), kCaptureDeadlineMs, ticket);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    receiver->resume(); // drop any frame decoded before this action started
    return CommandError::None;
}

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) {
        if (rxEnabled) {
            receiver->disableIRIn();
            rxEnabled = false;
        }
        runtime.finishCleanup(now);
    }
    if (!runtime.status().enabled) return;
    if (runtime.status().actionState != ActionState::Running) return;

    stepCapture(now);
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

} // namespace ir

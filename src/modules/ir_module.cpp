#include "ir_module.h"

#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRutils.h>
#include <driver/rmt.h>
#include <esp_err.h>

#include <string>
#include <string_view>

#include "board_pins.h"
#include "core/ir_record.h"
#include "core/ir_tx.h"
#include "core/module_runtime.h"

namespace ir {

namespace {

ModuleRuntime runtime("ir");

constexpr uint16_t kIrBufferSize = 514; // 512 timings + leading gap + overflow detection
constexpr uint32_t kCaptureDeadlineMs = 10000;
constexpr uint32_t kIrTickUs = 2; // library kRawTick; its IRrecv.h comment ".5us" is stale

// Transmit backend: the ESP32 RMT peripheral, not the IRremoteESP8266 software
// bit-bang (that library call delays for the whole waveform). rmt_write_items()
// with wait_tx_done=false returns after filling the first RMT memory block and
// lets the TX interrupt stream the rest, so a poll() call only pays the bounded
// item-copy cost (microseconds); the waveform duration runs entirely in
// hardware. The RMT item duration field is 15 bits, so a longer mark/space is
// split into consecutive same-level items (no inter-item gap).
//
// Stop/expiry: rmt_tx_stop() aborts the in-flight burst; because the driver
// keeps its completion semaphore taken until the TX-end interrupt fires, an
// aborted channel is torn down (rmt_driver_uninstall) and lazily re-created on
// the next transmit, so a later write cannot block on a semaphore that will
// never be given.
//
// One burst is queued per poll. TV-B-Gone has kTvbgoneCount bursts; completion
// is polled with rmt_wait_tx_done(..., 0) between bursts, so Stop always
// prevents every subsequent burst. A raw replay is a single burst, so its
// timings are never split by a poll boundary.
constexpr uint32_t kTxDeadlineMs = 5000;
constexpr uint32_t kTvbgoneDeadlineMs = 15000;

// Channel allocation (ESP32-S3 legacy RMT driver): the single group has 8
// channels; ch0..3 are TX-capable and ch4..7 are RX-only (IDF
// RMT_IS_TX_CHANNEL(channel) == channel <= SOC_RMT_TX_CANDIDATES_PER_GROUP-1,
// i.e. <= 3). Two other owners exist, so the IR backend must not share either:
//   ch0 - FastLED's legacy RMT4 driver (this build is IDF 4.4, so
//         FASTLED_RMT5 == 0) claims TX channels sequentially from 0 for the one
//         status strip and never releases them, so a second
//         rmt_driver_install(0) fails with ESP_ERR_INVALID_STATE;
//   ch1 - the CC1101 RF TX backend (checked in with the RF module).
// IR therefore uses ch2, the next free TX-capable channel. IRrecv does not use
// RMT at all (the library captures with an ESP32 hardware timer + GPIO ISR).
constexpr rmt_channel_t kTxChannel = RMT_CHANNEL_2;
constexpr uint8_t kRmtClkDiv = 80;              // APB 80 MHz / 80 => 1 us per tick
constexpr uint32_t kRmtMaxItemTicks = 0x7FFF;   // rmt_item32_t::duration field is 15 bits
// Worst case: every burst timing needs 3 RMT items after 15-bit splitting;
// the array is over-sized and the conversion fails closed if ever exceeded.
constexpr size_t kMaxRmtItems = irTx::kMaxTimings * 3;

IRrecv *receiver = nullptr;
decode_results results;
uint32_t activeTicket = 0;
bool rxEnabled = false;
bool txActive = false;
bool txReplay = false; // Raw plan came from replay (payload shape differs)
irTx::Plan txPlan;
irRecord::Record replayRecord; // decoded replay source, for the result payload
uint32_t txSentCodes = 0;

bool rmtReady = false;
bool txBusy = false; // an RMT burst is in flight
uint32_t rmtCarrierHz = 0;
uint8_t rmtDuty = 0;
rmt_item32_t txItems[kMaxRmtItems];
irTx::Burst pendingBurst; // module-owned so the RMT write can reference it until done

// ActionParams -> irTx semantic validation. The WebSocket schema only enforces
// type/range, so protocol support, valid bit sizes and the "required unless
// RAW" rules live here and become a synchronous invalid_params before any
// action starts.
irTx::Error buildCustomRequest(const ActionParams &params, irTx::CustomRequest &out) {
    // Order matches the descriptor's ParamSpec array (protocol, code, bits, frequency, raw).
    if (params.size() < 5) return irTx::Error::MissingRequired;
    return irTx::buildCustom(params.string(0), params.present(1), params.string(1), params.present(2),
                             params.integer(2), params.present(3), params.integer(3), params.present(4),
                             std::string_view(params.string(4), params.stringLength(4)), out);
}

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
    irRecord::Record record;
    const String protocol = typeToString(results.decode_type);
    const irRecord::Result result =
        irRecord::makeRecord(false, results.overflow, results.rawbuf, results.rawlen, kIrTickUs,
                             protocol.c_str(), numericValue, results.bits, results.value, record);

    if (result == irRecord::Result::Ready) {
        uint8_t encoded[irRecord::kMaxEncodedBytes];
        size_t length = 0;
        if (irRecord::encodeToBytes(record, encoded, sizeof(encoded), length)) {
            runtime.completeRecord(activeTicket, irRecord::toJson(record), encoded, length, now);
        } else {
            runtime.failAction(activeTicket, ActionError::CaptureTooLong, now, false);
        }
        activeTicket = 0;
    } else if (result == irRecord::Result::TooLong || result == irRecord::Result::Invalid) {
        runtime.failAction(activeTicket, ActionError::CaptureTooLong, now, false);
        activeTicket = 0;
    }
    receiver->resume(); // only after normalized bytes have been copied into RAM
}

void finishTx(uint32_t now) {
    std::string payload;
    switch (txPlan.kind) {
    case irTx::PlanKind::Protocol: payload = irTx::customProtocolToJson(txPlan.code); break;
    case irTx::PlanKind::Raw:
        payload = txReplay ? irTx::replayToJson(replayRecord, txPlan.waveform)
                           : irTx::customRawToJson(txPlan.waveform.count, txPlan.waveform.totalUs, txPlan.frequencyHz);
        break;
    case irTx::PlanKind::Tvbgone: payload = irTx::tvbgoneToJson(txSentCodes); break;
    case irTx::PlanKind::None: break;
    }
    runtime.completeAction(activeTicket, std::move(payload), now);
    activeTicket = 0;
    txActive = false;
    txPlan = irTx::Plan{};
}

bool txPlanFinished() {
    switch (txPlan.kind) {
    case irTx::PlanKind::Protocol:
    case irTx::PlanKind::Raw: return true;
    case irTx::PlanKind::Tvbgone: return txPlan.cursor >= irTx::kTvbgoneCount;
    case irTx::PlanKind::None: return true;
    }
    return true;
}

// ---- RMT backend ---------------------------------------------------------

// (Re)configures the channel carrier. Called only while idle: a repeated
// rmt_config() resets the channel memory pointer, which an in-flight burst
// cannot tolerate.
bool ensureRmt(uint32_t carrierHz, uint8_t dutyPercent) {
    rmt_config_t config = {};
    config.rmt_mode = RMT_MODE_TX;
    config.channel = kTxChannel;
    config.gpio_num = static_cast<gpio_num_t>(PIN_IR_TX);
    config.clk_div = kRmtClkDiv;
    config.mem_block_num = 1;
    config.flags = 0;
    config.tx_config.carrier_freq_hz = carrierHz;
    config.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH;
    config.tx_config.carrier_duty_percent = dutyPercent;
    config.tx_config.carrier_en = true;
    config.tx_config.loop_en = false;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    config.tx_config.idle_output_en = true;

    if (!rmtReady) {
        if (rmt_config(&config) != ESP_OK) return false;
        if (rmt_driver_install(kTxChannel, 0, 0) != ESP_OK) return false;
        rmtReady = true;
        rmtCarrierHz = carrierHz;
        rmtDuty = dutyPercent;
        return true;
    }
    if (carrierHz != rmtCarrierHz || dutyPercent != rmtDuty) {
        if (rmt_config(&config) != ESP_OK) return false;
        rmtCarrierHz = carrierHz;
        rmtDuty = dutyPercent;
    }
    return true;
}

// Expands a burst into rmt_item32_t. Alternating burst entries start with a
// mark (level 1 = carrier), and each entry is chunked to the 15-bit duration
// limit preserving its level, so long marks/spaces stay contiguous.
bool burstToRmt(const irTx::Burst &burst, size_t &itemCount) {
    size_t count = 0;
    bool mark = true;
    bool pending = false;
    uint32_t pendingLevel = 0;
    uint32_t pendingDuration = 0;

    for (uint16_t i = 0; i < burst.count; ++i) {
        uint32_t remaining = burst.timings[i];
        while (remaining > 0) {
            const uint32_t chunk = remaining > kRmtMaxItemTicks ? kRmtMaxItemTicks : remaining;
            if (!pending) {
                pendingLevel = mark ? 1u : 0u;
                pendingDuration = chunk;
                pending = true;
            } else {
                if (count >= kMaxRmtItems) return false;
                txItems[count].level0 = pendingLevel;
                txItems[count].duration0 = pendingDuration;
                txItems[count].level1 = mark ? 1u : 0u;
                txItems[count].duration1 = chunk;
                ++count;
                pending = false;
            }
            remaining -= chunk;
        }
        mark = !mark;
    }
    if (pending) {
        if (count >= kMaxRmtItems) return false;
        txItems[count].level0 = pendingLevel;
        txItems[count].duration0 = pendingDuration;
        txItems[count].level1 = 0;
        txItems[count].duration1 = 0;
        ++count;
    }
    itemCount = count;
    return count > 0;
}

void abortTx() {
    txBusy = false;
    if (rmtReady) {
        rmt_tx_stop(kTxChannel);           // stop any in-flight burst
        rmt_driver_uninstall(kTxChannel);  // reset the completion semaphore
        rmtReady = false;
        rmtCarrierHz = 0;
        rmtDuty = 0;
    }
}

bool rmtTransmitDone() { return rmt_wait_tx_done(kTxChannel, 0) == ESP_OK; }

bool rmtStart(const irTx::Burst &burst) {
    if (!ensureRmt(burst.carrierHz, burst.dutyPercent)) return false;
    size_t itemCount = 0;
    if (!burstToRmt(burst, itemCount)) return false;
    return rmt_write_items(kTxChannel, txItems, static_cast<int>(itemCount), false) == ESP_OK;
}

bool buildPendingBurst(const irTx::Step &step) {
    if (step.kind == irTx::StepKind::Protocol) {
        return irTx::buildProtocolBurst(step.code, pendingBurst) == irTx::Error::None;
    }
    pendingBurst.count = step.count;
    for (uint16_t i = 0; i < step.count; ++i) pendingBurst.timings[i] = step.timings[i];
    pendingBurst.carrierHz = step.frequencyHz;
    pendingBurst.dutyPercent = 33; // matches the library's sendRaw default
    return pendingBurst.count > 0;
}

void stepTx(uint32_t now) {
    if (runtime.expire(now, ActionError::HardwareError, false)) {
        abortTx();
        activeTicket = 0;
        txActive = false;
        txPlan = irTx::Plan{};
        return;
    }

    if (txBusy) {
        if (!rmtTransmitDone()) return; // still streaming in hardware; loop stays free
        txBusy = false;
        if (txPlanFinished()) finishTx(now);
        return;
    }

    const irTx::Step step = irTx::nextStep(txPlan);
    if (step.kind == irTx::StepKind::None || step.kind == irTx::StepKind::Done) {
        finishTx(now);
        return;
    }

    if (!buildPendingBurst(step) || !rmtStart(pendingBurst)) {
        abortTx();
        runtime.failAction(activeTicket, ActionError::HardwareError, now, false);
        activeTicket = 0;
        txActive = false;
        txPlan = irTx::Plan{};
        return;
    }

    txBusy = true;
    if (step.kind == irTx::StepKind::Protocol) ++txSentCodes;
}

} // namespace

void begin() {
    // Infrastructure only: allocates the receive buffer, no interrupt/chip I/O.
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

    // Oscillating the flag drives the deferred cleanup in poll.
    runtime.setEnabled(false, now, rxEnabled || rmtReady || txBusy);
    activeTicket = 0;
    txActive = false;
    txReplay = false;
    txPlan = irTx::Plan{};
    txSentCodes = 0;
    return CommandError::None;
}

CommandError handleAction(ActionId action, const ActionParams &params) {
    const ActionDescriptor *descriptor = catalog::findAction(ModuleId::Ir, action);
    if (descriptor == nullptr) return CommandError::UnsupportedAction;
    if (action != ActionId::Capture && action != ActionId::IrReplay && action != ActionId::IrTvbgone &&
        action != ActionId::IrCustomTx) {
        return CommandError::UnsupportedAction;
    }

    // Validate every TX parameter before starting the action, so malformed input
    // is a synchronous invalid_params rather than an async failure.
    irTx::Plan plan;
    uint32_t deadlineMs = kCaptureDeadlineMs;
    if (action == ActionId::IrCustomTx) {
        irTx::CustomRequest request;
        if (buildCustomRequest(params, request) != irTx::Error::None) return CommandError::InvalidParams;
        deadlineMs = kTxDeadlineMs;
        if (request.raw) {
            plan.kind = irTx::PlanKind::Raw;
            plan.waveform = request.waveform;
            plan.frequencyHz = request.frequencyHz;
        } else {
            plan.kind = irTx::PlanKind::Protocol;
            plan.code = request.code;
        }
    } else if (action == ActionId::IrTvbgone) {
        plan.kind = irTx::PlanKind::Tvbgone;
        deadlineMs = kTvbgoneDeadlineMs;
    } else if (action == ActionId::IrReplay) {
        deadlineMs = kTxDeadlineMs;
    }

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), deadlineMs, ticket, *descriptor);
    if (error != CommandError::None) return error;

    if (action == ActionId::Capture) {
        activeTicket = ticket;
        receiver->resume(); // drop any frame decoded before this action started
        return CommandError::None;
    }

    if (action == ActionId::IrReplay) {
        const RecordBuffer &buffer = runtime.recordBuffer();
        if (!irRecord::decode(buffer.data(), buffer.size(), replayRecord)) {
            runtime.failAction(ticket, ActionError::HardwareError, millis(), false);
            return CommandError::None;
        }
        if (irTx::buildReplayWaveform(replayRecord, plan.waveform) != irTx::Error::None) {
            runtime.failAction(ticket, ActionError::CaptureTooLong, millis(), false);
            return CommandError::None;
        }
        plan.kind = irTx::PlanKind::Raw;
        plan.frequencyHz = irTx::kDefaultFrequencyHz;
    }

    txPlan = plan;
    txReplay = action == ActionId::IrReplay;
    txSentCodes = 0;
    txActive = true;
    activeTicket = ticket;
    return CommandError::None;
}

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) {
        if (rxEnabled) {
            receiver->disableIRIn();
            rxEnabled = false;
        }
        abortTx(); // stops an in-flight burst and releases the RMT channel
        pinMode(static_cast<uint8_t>(PIN_IR_TX), OUTPUT);
        digitalWrite(static_cast<uint8_t>(PIN_IR_TX), LOW);
        runtime.finishCleanup(now);
    }
    if (!runtime.status().enabled) return;
    if (runtime.status().actionState != ActionState::Running) return;
    if (activeTicket == 0) return;

    if (txActive) {
        stepTx(now);
    } else {
        stepCapture(now);
    }
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

bool takeActionOutput(ActionOutput &output) { return runtime.takeActionOutput(output); }

} // namespace ir

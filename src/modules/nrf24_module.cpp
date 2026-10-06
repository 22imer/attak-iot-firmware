#include "nrf24_module.h"

#include <RF24.h>

#include <cstdio>
#include <string>

#include "board_pins.h"
#include "core/action_catalog.h"
#include "core/jam_plan.h"
#include "core/module_runtime.h"
#include "core/nrf_spectrum.h"
#include "modules/spi_bus.h"

namespace nrf24 {

namespace {

ModuleRuntime runtime("nrf24");

constexpr uint32_t kHealthRecheckMs = 500;
constexpr uint32_t kMinSampleIntervalMs = 100;

// Constructor is I/O-free; the single shared bus is passed at begin().
RF24 radio(static_cast<uint16_t>(PIN_NRF24_CE), static_cast<uint16_t>(PIN_NRF24_CS));
bool initialized = false;
uint32_t lastHealthMs = 0;

nrfSpectrum::Sweeper sweeper;
uint32_t activeTicket = 0;
uint32_t lastSampleMs = 0;

#ifdef ENABLE_DISRUPTIVE
// nrf_jammer (disruptive; PLAN §2.4): a CONT_WAVE constant carrier that hops
// channels. RF24::startConstCarrier() sets RF_SETUP.CONT_WAVE + PLL_LOCK; the
// ChannelHopper is the portable, native-tested dwell plan.
jamPlan::ChannelHopper jamHopper;
ActionId activeActionId = ActionId::None;
bool jammerActive = false;
uint32_t jamStartMs = 0;
uint32_t lastJamPublishMs = 0;
#endif

bool probeChip() { return radio.isChipConnected(); }

// Prepares the radio for RPD channel sweeping: raw RX, no addressing/CRC/ack.
// startListening() powers up in PRIM_RX; RPD only becomes valid after the
// synthesizer settles, which the Sweeper's dwell gate enforces. Reference for
// the technique only (no code copied): RF24's testRPD()/testCarrier() docs and
// the common nRF24 RPD scanner approach.
// (Re)enters RX for one channel: CE low, write RF_CH, CE high. The nRF24L01+
// latches RPD for an RX session, so a fresh session is required before every
// probe (including when the channel index is unchanged), and the settle gate
// must be armed from the real startListening(), never from the previous read.
// Reference: Nordic nRF24L01+ Product Specification (RPD register, RX settle)
// and RF24's startListening()/testRPD() in .pio/libdeps/RF24/RF24.cpp.
void beginChannel(uint8_t channel) {
    radio.stopListening(); // CE low before touching RF_CH
    radio.setChannel(channel);
    radio.startListening(); // CE high -> new RX session, RPD re-latched
    sweeper.armSettle(micros());
}

void beginSweep(uint8_t first, uint8_t last, uint32_t dwellUs) {
    radio.setAutoAck(false);
    radio.setDataRate(RF24_1MBPS);
    radio.setPALevel(RF24_PA_MIN);
    radio.disableCRC();
    radio.setAddressWidth(3);
    radio.setPayloadSize(32);
    radio.flush_rx();
    radio.flush_tx();
    sweeper.begin(micros(), first, last, dwellUs);
    beginChannel(first);
}

void maybePublish(uint32_t now) {
    if (static_cast<uint32_t>(now - lastSampleMs) < kMinSampleIntervalMs) return;
    std::string payload;
    if (!nrfSpectrum::formatSpectrumJson(sweeper, payload)) return;
    if (runtime.publishOutput(activeTicket, std::move(payload), now)) lastSampleMs = now;
}

// Cooperative step: probe at most one settled channel per call and never
// delay/spin. RPD is read once from the current RX session, the session is then
// ended (CE low) and a fresh RX session is entered for the next channel.
void stepSweep(uint32_t now) {
    const uint32_t nowUs = micros();
    if (!sweeper.ready(nowUs)) return;

    const bool rpd = radio.testRPD(); // latched RPD of this RX session
    radio.stopListening();            // end the session: the latch must not leak
    const bool swept = sweeper.submit(rpd, nowUs);
    beginChannel(sweeper.channel()); // fresh RX session + settle gate
    if (swept) maybePublish(now);
}

#ifdef ENABLE_DISRUPTIVE
void stopJammer() {
    if (!jammerActive) return;
    radio.stopConstCarrier(); // powerDown() then clears CONT_WAVE/PLL_LOCK
    jammerActive = false;
}

// Constant carrier that hops channels. The portable ChannelHopper owns the
// dwell plan; the radio is only re-armed on a hop (or once at start).
CommandError startJammer(const ActionParams &params, const ActionDescriptor &descriptor) {
    int64_t first = params.present(0) ? params.integer(0) : nrfSpectrum::kChannelFirst;
    int64_t last = params.present(1) ? params.integer(1) : nrfSpectrum::kChannelLast;
    if (first > last) {
        const int64_t swap = first;
        first = last;
        last = swap;
    }
    if (first < nrfSpectrum::kChannelFirst) first = nrfSpectrum::kChannelFirst;
    if (last > nrfSpectrum::kChannelLast) last = nrfSpectrum::kChannelLast;

    uint32_t dwellMs = 100;
    if (params.present(2)) {
        int64_t value = params.integer(2);
        if (value < 20) value = 20;
        if (value > 5000) value = 5000;
        dwellMs = static_cast<uint32_t>(value);
    }

    if (!initialized) {
        if (!radio.begin(&spiBus::instance()) || !probeChip()) {
            runtime.setHealth(false, "nrf24 not responding", millis());
            return CommandError::HardwareError;
        }
        initialized = true;
    }

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), 0, ticket, descriptor); // Continuous, no deadline
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    activeActionId = ActionId::NrfJammer;
    jamHopper.begin(static_cast<uint8_t>(first), static_cast<uint8_t>(last), dwellMs, millis());
    radio.stopListening();
    radio.startConstCarrier(RF24_PA_MAX, jamHopper.channel());
    jammerActive = true;
    jamStartMs = millis();
    lastJamPublishMs = jamStartMs - kMinSampleIntervalMs; // first sample may publish immediately
    return CommandError::None;
}

void stepJammer(uint32_t now) {
    if (jamHopper.due(now)) {
        jamHopper.advance(now);
        if (jamHopper.spansMultiple()) radio.startConstCarrier(RF24_PA_MAX, jamHopper.channel());
    }

    if (static_cast<uint32_t>(now - lastJamPublishMs) < kMinSampleIntervalMs) return;
    char buf[192];
    const int written = snprintf(buf, sizeof(buf),
        "{\"kind\":\"nrf_jammer\",\"channel\":%u,\"first\":%u,\"last\":%u,\"dwellMs\":%lu,\"hops\":%lu,"
        "\"elapsedMs\":%lu}",
        static_cast<unsigned>(jamHopper.channel()), static_cast<unsigned>(jamHopper.first()),
        static_cast<unsigned>(jamHopper.last()), static_cast<unsigned long>(jamHopper.dwellMs()),
        static_cast<unsigned long>(jamHopper.hops()), static_cast<unsigned long>(now - jamStartMs));
    if (written <= 0) return;
    const size_t length = (static_cast<size_t>(written) < sizeof(buf)) ? static_cast<size_t>(written) : sizeof(buf) - 1;
    std::string payload(buf, length);
    if (runtime.publishOutput(activeTicket, std::move(payload), now)) lastJamPublishMs = now;
}
#endif

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

#ifdef ENABLE_DISRUPTIVE
    stopJammer(); // cut the carrier immediately; powerDown() still runs in poll()
    activeActionId = ActionId::None;
#endif
    runtime.setEnabled(false, now, initialized); // powerDown() released in poll()
    activeTicket = 0;
    return CommandError::None;
}

CommandError handleAction(ActionId action, const ActionParams &params) {
    const ActionDescriptor *descriptor = catalog::findAction(ModuleId::Nrf24, action);
    if (descriptor == nullptr) return CommandError::UnsupportedAction;
#ifdef ENABLE_DISRUPTIVE
    if (action == ActionId::NrfJammer) return startJammer(params, *descriptor);
#endif
    if (action != ActionId::NrfScan) return CommandError::UnsupportedAction;

    int64_t first = params.present(nrfSpectrum::ParamStartChannel)
        ? params.integer(nrfSpectrum::ParamStartChannel) : nrfSpectrum::kChannelFirst;
    int64_t last = params.present(nrfSpectrum::ParamEndChannel)
        ? params.integer(nrfSpectrum::ParamEndChannel) : nrfSpectrum::kChannelLast;
    if (first > last) {
        const int64_t swap = first;
        first = last;
        last = swap;
    }
    if (first < nrfSpectrum::kChannelFirst) first = nrfSpectrum::kChannelFirst;
    if (last > nrfSpectrum::kChannelLast) last = nrfSpectrum::kChannelLast;

    const uint32_t dwellUs = params.present(nrfSpectrum::ParamDwellUs)
        ? nrfSpectrum::clampDwellUs(params.integer(nrfSpectrum::ParamDwellUs))
        : nrfSpectrum::kDefaultDwellUs;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), 0, ticket, *descriptor);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    lastSampleMs = 0; // the first sample is emitted as soon as a sweep completes
    beginSweep(static_cast<uint8_t>(first), static_cast<uint8_t>(last), dwellUs);
    return CommandError::None;
}

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) {
        if (initialized) {
#ifdef ENABLE_DISRUPTIVE
            stopJammer();
#endif
            radio.stopListening();
            radio.powerDown();
            initialized = false;
        }
        activeTicket = 0;
#ifdef ENABLE_DISRUPTIVE
        activeActionId = ActionId::None;
#endif
        runtime.finishCleanup(now);
    }
    if (!runtime.status().enabled) return;

    if (runtime.status().actionState == ActionState::Running) {
        // Keep the F0 enabled-module liveness contract (<=2 s) while streaming:
        // one bounded chip probe per kHealthRecheckMs. A lost chip ends the
        // action and releases RX/power in the cleanup branch above, before the
        // arbiter lease is dropped.
        if (static_cast<uint32_t>(now - lastHealthMs) >= kHealthRecheckMs) {
            lastHealthMs = now;
            if (!probeChip()) {
                runtime.setHealth(false, "nrf24 not responding", now);
#ifdef ENABLE_DISRUPTIVE
                stopJammer(); // the carrier must not outlive a lost chip
                activeActionId = ActionId::None;
#endif
                runtime.failAction(activeTicket, ActionError::HardwareError, now, true);
                activeTicket = 0;
                return;
            }
        }
#ifdef ENABLE_DISRUPTIVE
        if (activeActionId == ActionId::NrfJammer) {
            stepJammer(now);
            return;
        }
#endif
        stepSweep(now);
        return;
    }

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

bool takeActionOutput(ActionOutput &output) { return runtime.takeActionOutput(output); }

} // namespace nrf24

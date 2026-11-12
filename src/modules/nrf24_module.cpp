#include "nrf24_module.h"

#include <RF24.h>

#include "board_pins.h"

// 2.4GHz jammer: the nRF24L01 emits its constant carrier (CONT_WAVE) while
// hopping across the whole 2400-2525 MHz band. Educational use only — the
// carrier is strong enough to disrupt nearby BLE/WiFi traffic.
//
// Toggle model (v2): disabled = radio idle, enabled = sweep runs until the
// dashboard sends "disable". A carrier cannot be retuned with plain
// setChannel() — on PA+LNA modules that leaves the PLL in an undefined
// state and the carrier freezes after the first hop — so every hop goes
// through stopConstCarrier() → powerUp() → startConstCarrier().
namespace nrf24 {

namespace {
constexpr uint8_t kFirstChannel = 0;  // 2400 MHz
constexpr uint8_t kLastChannel = 125; // 2525 MHz

// Dwell per channel. Power-up alone takes ~5ms (RF24_POWERUP_DELAY), so a
// full 0..125 sweep takes ~1.3s with roughly 50% carrier duty cycle.
constexpr uint32_t kHopIntervalMs = 10;

// Liveness cadence: 1s while jamming, 2s while idle so a module hot-plugged
// after boot is picked up without hammering the shared SPI bus.
constexpr uint32_t kJammingLivenessIntervalMs = 1000;
constexpr uint32_t kIdleLivenessIntervalMs = 2000;

// The dashboard only publishes at 100ms, so rebuilding the channel label on
// every hop (100+ allocations/second) just churns the heap. Match the output
// cadence to the publish cadence.
constexpr uint32_t kOutputIntervalMs = 100;

ModuleStatus currentStatus = {"nrf24", false, false, "off", "", 0};
RF24 radio(PIN_NRF24_CE, PIN_NRF24_CS);

bool radioReady = false;
bool jamming = false;
uint8_t channel = kFirstChannel;
uint32_t lastHopMs = 0;
uint32_t lastLivenessMs = 0;
uint32_t lastOutputMs = 0;

std::string channelLabel() {
    return "ch " + std::to_string(channel) + "/" + std::to_string(kLastChannel) +
           " (" + std::to_string(2400 + channel) + " MHz)";
}

// (Re-)initializes the radio on the shared SPI bus (RF24 starts the ESP32-S3
// core's default SPI pins, which match board_pins.h) and confirms the chip
// actually answers, not merely that the SPI transaction completed.
bool initRadio() {
    if (!radio.begin()) {
        radioReady = false;
        return false;
    }
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_1MBPS);
    radio.setAutoAck(false);
    radio.setAddressWidth(5);
    radioReady = radio.isChipConnected();
    return radioReady;
}

bool startJamming() {
    if (!radioReady || !radio.isChipConnected()) {
        radioReady = false;
        return false;
    }
    radio.stopConstCarrier(); // clean slate after a pre-reset carrier, too
    radio.powerUp();
    channel = kFirstChannel;
    radio.startConstCarrier(RF24_PA_MAX, channel);
    jamming = true;
    lastHopMs = millis();
    return true;
}

// Register reads still work while the radio is powered down, so this leaves
// an accurate presence reading even after stopping the carrier.
void stopJamming() {
    radio.stopConstCarrier(); // powers the radio down per datasheet
    jamming = false;
    radioReady = radio.isChipConnected();
}

void hopChannel() {
    radio.stopConstCarrier();
    channel = (channel >= kLastChannel) ? kFirstChannel : channel + 1;
    radio.powerUp();
    radio.startConstCarrier(RF24_PA_MAX, channel);
    lastHopMs = millis();
}

void markIdle() {
    currentStatus.connected = radioReady;
    currentStatus.detail = radioReady ? "ready" : "not responding";
    currentStatus.output = "";
}

void markJamming() {
    currentStatus.connected = true;
    currentStatus.detail = "jamming 2.4 GHz sweep";
    currentStatus.output = channelLabel();
    lastOutputMs = millis();
}

void markNotResponding(const char *detail) {
    currentStatus.connected = false;
    currentStatus.detail = detail;
    currentStatus.output = "";
}
} // namespace

void begin() {
    lastLivenessMs = millis();
    initRadio();
    if (radioReady) {
        // Clear a carrier that survived an MCU reset while jamming; CONFIG
        // writes in begin() do not touch the CONT_WAVE bit.
        radio.stopConstCarrier();
    }
    markIdle();
    currentStatus.lastUpdateMs = lastLivenessMs;
}

void setEnabled(bool enabled) {
    if (currentStatus.enabled == enabled) return;

    currentStatus.enabled = enabled;
    currentStatus.lastUpdateMs = millis();
    lastLivenessMs = currentStatus.lastUpdateMs;

    if (!enabled) {
        stopJamming();
        markIdle();
        return;
    }

    if (initRadio() && startJamming()) {
        markJamming();
    } else {
        markNotResponding("not responding (check wiring)");
    }
}

void poll() {
    const uint32_t now = millis();

    if (!currentStatus.enabled) {
        if (now - lastLivenessMs >= kIdleLivenessIntervalMs) {
            lastLivenessMs = now;
            if (radioReady) {
                radioReady = radio.isChipConnected();
            } else {
                initRadio(); // pick up a module plugged in after boot
            }
            markIdle();
            currentStatus.lastUpdateMs = now;
        }
        return;
    }

    // Enabled but the carrier never started (e.g. module was missing when
    // enable was pressed) — keep retrying on the liveness cadence.
    if (!jamming) {
        if (now - lastLivenessMs >= kJammingLivenessIntervalMs) {
            lastLivenessMs = now;
            if (initRadio() && startJamming()) {
                markJamming();
            } else {
                markNotResponding("not responding (check wiring)");
            }
            currentStatus.lastUpdateMs = now;
        }
        return;
    }

    if (now - lastLivenessMs >= kJammingLivenessIntervalMs) {
        lastLivenessMs = now;
        if (!radio.isChipConnected()) {
            radio.stopConstCarrier();
            jamming = false;
            radioReady = false;
            markNotResponding("lost while jamming");
            currentStatus.lastUpdateMs = now;
            return;
        }
    }

    if (now - lastHopMs >= kHopIntervalMs) {
        hopChannel();
    }
    if (now - lastOutputMs >= kOutputIntervalMs) {
        currentStatus.output = channelLabel();
        lastOutputMs = now;
    }
    currentStatus.lastUpdateMs = now;
}

ModuleStatus status() { return currentStatus; }

} // namespace nrf24

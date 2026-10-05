#include "wifi_module.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdio>
#include <string>

#include "core/module_runtime.h"
#include "core/wifi_scan_result.h"

namespace wifiModule {

namespace {

ModuleRuntime runtime("wifi");

constexpr uint32_t kScanDeadlineMs = 15000; // measured on board, not auto-relaxed
constexpr uint32_t kHealthRecheckMs = 500;

uint32_t activeTicket = 0;
uint32_t lastHealthMs = 0;
bool scanInFlight = false;
bool stopRequested = false;
volatile bool scanDoneNotice = false;

void onScanEvent(arduino_event_id_t) { scanDoneNotice = true; }

std::string buildScanOutput(int16_t count) {
    wifiScan::TopNetworks top;
    for (int16_t i = 0; i < count; ++i) {
        wifiScan::Candidate candidate;
        candidate.index = static_cast<size_t>(i);
        candidate.rssi = WiFi.RSSI(i);
        const uint8_t *bssid = WiFi.BSSID(i);
        if (bssid) {
            for (size_t b = 0; b < 6; ++b) candidate.bssid[b] = bssid[b];
        }
        top.consider(candidate);
    }

    JsonDocument doc;
    doc["kind"] = "wifi_scan";
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (size_t n = 0; n < top.size(); ++n) {
        const int16_t index = static_cast<int16_t>(top.at(n).index);
        // Use the BSSID captured during consider() rather than re-fetching it:
        // WiFi.BSSID(index) can return nullptr if the entry aged out of the
        // driver's list between the two passes, and the old code dereferenced it
        // without a null check (crash). The stored copy is always 6 valid bytes.
        const std::array<uint8_t, 6> &bssid = top.at(n).bssid;
        char bssidText[18];
        snprintf(bssidText, sizeof(bssidText), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2],
                 bssid[3], bssid[4], bssid[5]);

        JsonObject entry = networks.add<JsonObject>();
        entry["ssid"] = WiFi.SSID(index).c_str();
        entry["bssid"] = bssidText;
        entry["rssi"] = top.at(n).rssi;
        entry["channel"] = WiFi.channel(index);
        entry["secure"] = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    }
    doc["truncated"] = top.truncated();

    std::string output;
    serializeJson(doc, output);
    return output;
}

void stepScan(uint32_t now) {
    if (scanDoneNotice) {
        scanDoneNotice = false;
        const int16_t count = WiFi.scanComplete();
        if (count < 0) {
            runtime.failAction(activeTicket, ActionError::ScanFailed, now, false);
        } else {
            runtime.completeAction(activeTicket, buildScanOutput(count), now);
        }
        activeTicket = 0;
        scanInFlight = false;
        stopRequested = false;
        WiFi.scanDelete();
        return;
    }
    if (runtime.expire(now, ActionError::ScanTimeout, scanInFlight)) activeTicket = 0;
}

void performCleanup(uint32_t now) {
    if (!scanInFlight) {
        stopRequested = false;
        scanDoneNotice = false;
        runtime.finishCleanup(now);
        return;
    }
    if (!stopRequested) {
        esp_wifi_scan_stop(); // ESP_ERR_WIFI_NOT_STARTED just means it already finished
        stopRequested = true;
    }
    if (!scanDoneNotice) return; // keep cleanupPending until the event really lands

    scanDoneNotice = false;
    WiFi.scanDelete();
    scanInFlight = false;
    stopRequested = false;
    runtime.finishCleanup(now);
}

} // namespace

void begin() { WiFi.onEvent(onScanEvent, ARDUINO_EVENT_WIFI_SCAN_DONE); }

CommandError setEnabled(bool enabled) {
    const uint32_t now = millis();

    if (enabled) {
        if (runtime.status().enabled) return CommandError::None; // idempotent
        runtime.setEnabled(true, now);
        if (runtime.status().cleanupPending) return CommandError::None;
        runtime.setHealth(true, "ready", now);
        lastHealthMs = now;
        return CommandError::None;
    }

    runtime.setEnabled(false, now, scanInFlight);
    activeTicket = 0;
    return CommandError::None;
}

CommandError handleAction(ActionId action) {
    if (action != ActionId::Scan) return CommandError::UnsupportedAction;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), kScanDeadlineMs, ticket);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    scanInFlight = true;
    stopRequested = false;
    scanDoneNotice = false;

    if (WiFi.scanNetworks(/*async=*/true) == WIFI_SCAN_FAILED) {
        scanInFlight = false;
        activeTicket = 0;
        runtime.failAction(ticket, ActionError::ScanFailed, millis(), false);
    }
    return CommandError::None; // accepted; failures surface via actionState
}

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) {
        performCleanup(now);
        if (runtime.status().cleanupPending) return;
    }
    if (!runtime.status().enabled) return; // never tears down the dashboard AP

    if (runtime.status().actionState == ActionState::Running) {
        stepScan(now);
        return;
    }

    if (static_cast<uint32_t>(now - lastHealthMs) < kHealthRecheckMs) return;
    lastHealthMs = now;
    const bool radioOk = (WiFi.getMode() & WIFI_MODE_AP) != 0;
    runtime.setHealth(radioOk, radioOk ? "ready" : "ap not active", now);
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

} // namespace wifiModule

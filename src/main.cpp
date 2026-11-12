#include <Arduino.h>

#include <atomic>

#include "core/storage.h"
#include "core/web_dashboard.h"
#include "core/wifi_ap.h"
#include "modules/cc1101_module.h"
#include "modules/ir_module.h"
#include "modules/nrf24_module.h"
#include "modules/pn532_module.h"

namespace {
DeviceConfig config;

// Periodic status broadcast — the jammer updates its channel output far
// faster than a dashboard can use, so publishing every loop would saturate
// the AP with WebSocket traffic. 100ms keeps the UI live without that cost.
constexpr uint32_t kPublishIntervalMs = 100;
uint32_t lastPublishMs = 0;

// AsyncWebSocket dispatches commands on its own task, while module poll() and
// RF24 SPI work run on the Arduino loop task. Running setEnabled() right in
// the callback would race that SPI transaction and the ModuleStatus strings,
// so the callback only records the latest command and loop() applies it.
std::atomic<uint8_t> pendingNrf24Command{0}; // 0 = none, 1 = enable, 2 = disable

void publishAllModules() {
    webDashboard::publishStatus(cc1101::status());
    webDashboard::publishStatus(nrf24::status());
    webDashboard::publishStatus(pn532::status());
    webDashboard::publishStatus(ir::status());
}

// Dashboard commands are dispatched here because main.cpp is the one place
// that already knows about every module. Only nrf24 has a payload so far;
// the other modules ignore enable/disable (their stubs have no toggle).
void handleCommand(const WsCommand &command) {
    if (command.module != "nrf24") return;

    // The jammer payload is the sweep itself — enable starts it, disable
    // stops it; nrf24 has no named actions.
    if (command.cmd == "enable") {
        pendingNrf24Command.store(1);
    } else if (command.cmd == "disable") {
        pendingNrf24Command.store(2);
    }
}

void applyPendingNrf24Command() {
    const uint8_t pending = pendingNrf24Command.exchange(0);
    if (pending == 0) return;

    nrf24::setEnabled(pending == 1);
    // Answer the toggle immediately instead of waiting for the next
    // periodic broadcast.
    webDashboard::publishStatus(nrf24::status());
}

// One line per module at boot so a board can be brought up over serial even
// when the dashboard network is not reachable. The dashboard remains the
// primary UI; this is a debugging aid, not a second status channel.
void logBootStatus() {
    const ModuleStatus statuses[] = {
        cc1101::status(),
        nrf24::status(),
        pn532::status(),
        ir::status(),
    };
    for (const ModuleStatus &status : statuses) {
        Serial.printf("%s: %s (%s)\n", status.name,
                      status.connected ? "connected" : "disconnected",
                      status.detail.c_str());
    }
}
} // namespace

void setup() {
    Serial.begin(115200);

    storage::begin();
    config = storage::load();

    wifiAp::begin(config);
    webDashboard::onCommand(handleCommand);
    webDashboard::begin();

    cc1101::begin();
    nrf24::begin();
    pn532::begin();
    ir::begin();

    logBootStatus();
}

void loop() {
    applyPendingNrf24Command();

    cc1101::poll();
    nrf24::poll();
    pn532::poll();
    ir::poll();

    const uint32_t now = millis();
    if (now - lastPublishMs >= kPublishIntervalMs) {
        lastPublishMs = now;
        publishAllModules();
    }

    webDashboard::loop();
}

#include <Arduino.h>

#include "core/command_types.h"
#include "core/module_status.h"
#include "core/status_health.h"
#include "core/status_led.h"
#include "core/storage.h"
#include "core/web_dashboard.h"
#include "core/wifi_ap.h"
#include "core/ws_command.h"
#include "modules/cc1101_module.h"
#include "modules/ir_module.h"
#include "modules/nrf24_module.h"
#include "modules/pn532_module.h"
#include "modules/wifi_module.h"

namespace {

DeviceConfig config;

// One table so loop() can poll/publish every category and route commands by
// typed ModuleId without a registry abstraction.
struct ModuleHandle {
    ModuleId id;
    void (*begin)();
    CommandError (*setEnabled)(bool);
    CommandError (*handleAction)(ActionId);
    void (*poll)();
    const ModuleStatus &(*status)();
    uint32_t (*revision)();
};

const ModuleHandle kModules[] = {
    {ModuleId::Cc1101, cc1101::begin, cc1101::setEnabled, cc1101::handleAction, cc1101::poll, cc1101::status,
     cc1101::revision},
    {ModuleId::Nrf24, nrf24::begin, nrf24::setEnabled, nrf24::handleAction, nrf24::poll, nrf24::status, nrf24::revision},
    {ModuleId::Pn532, pn532::begin, pn532::setEnabled, pn532::handleAction, pn532::poll, pn532::status, pn532::revision},
    {ModuleId::Ir, ir::begin, ir::setEnabled, ir::handleAction, ir::poll, ir::status, ir::revision},
    {ModuleId::Wifi, wifiModule::begin, wifiModule::setEnabled, wifiModule::handleAction, wifiModule::poll,
     wifiModule::status, wifiModule::revision},
};

constexpr size_t kModuleTotal = sizeof(kModules) / sizeof(kModules[0]);
uint32_t lastRevision[kModuleTotal] = {};
uint32_t lastPublishMs[kModuleTotal] = {};
uint32_t lastHeartbeatMs = 0;
const ModuleStatus *statusPtrs[kModuleTotal] = {};

CommandError dispatchCommand(const WsCommand &command) {
    for (size_t i = 0; i < kModuleTotal; ++i) {
        if (kModules[i].id != command.module) continue;
        switch (command.cmd) {
        case CommandKind::Enable: return kModules[i].setEnabled(true);
        case CommandKind::Disable: return kModules[i].setEnabled(false);
        case CommandKind::Action: return kModules[i].handleAction(command.action);
        }
    }
    return CommandError::InvalidCommand;
}

void publishModule(size_t index, uint32_t now, bool heartbeatDue) {
    const uint32_t revision = kModules[index].revision();
    const bool dirty = heartbeatDue || revision != lastRevision[index];
    const bool due = heartbeatDue || static_cast<uint32_t>(now - lastPublishMs[index]) >= 100;
    if (!dirty || !due) return;

    webDashboard::publishStatus(kModules[index].status());
    lastRevision[index] = revision;
    lastPublishMs[index] = now;
}

void publishAll(uint32_t now) {
    const bool heartbeatDue = static_cast<uint32_t>(now - lastHeartbeatMs) >= 1000;
    for (size_t i = 0; i < kModuleTotal; ++i) publishModule(i, now, heartbeatDue);
    if (heartbeatDue) lastHeartbeatMs = now;
}

} // namespace

void setup() {
    Serial.begin(115200);

    storage::begin();
    config = storage::load();

    wifiAp::begin(config);
    webDashboard::begin();

    for (size_t i = 0; i < kModuleTotal; ++i) kModules[i].begin();
    statusLed::begin();
}

void loop() {
    for (size_t i = 0; i < kModuleTotal; ++i) kModules[i].poll();

    EnqueuedCommand request;
    while (webDashboard::nextCommand(request)) {
        webDashboard::reply(request, dispatchCommand(request.command));
    }

    publishAll(millis());

    for (size_t i = 0; i < kModuleTotal; ++i) statusPtrs[i] = &kModules[i].status();
    statusLed::update(aggregateHealth(statusPtrs, kModuleTotal));

    webDashboard::loop();
}

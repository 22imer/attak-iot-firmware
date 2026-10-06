#include <Arduino.h>
#include <ArduinoJson.h>

#include "core/command_types.h"
#include "core/module_status.h"
#include "core/module_runtime.h"
#include "core/radio_arbiter.h"
#include "core/serial_console.h"
#include "core/status_health.h"
#include "core/status_led.h"
#include "core/storage.h"
#include "core/usb_network.h"
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
    CommandError (*handleAction)(ActionId, const ActionParams &);
    void (*poll)();
    const ModuleStatus &(*status)();
    uint32_t (*revision)();
    bool (*takeActionOutput)(ActionOutput &);
};

const ModuleHandle kModules[] = {
    {ModuleId::Cc1101, cc1101::begin, cc1101::setEnabled, cc1101::handleAction, cc1101::poll, cc1101::status,
     cc1101::revision, cc1101::takeActionOutput},
    {ModuleId::Nrf24, nrf24::begin, nrf24::setEnabled, nrf24::handleAction, nrf24::poll, nrf24::status,
     nrf24::revision, nrf24::takeActionOutput},
    {ModuleId::Pn532, pn532::begin, pn532::setEnabled, pn532::handleAction, pn532::poll, pn532::status,
     pn532::revision, pn532::takeActionOutput},
    {ModuleId::Ir, ir::begin, ir::setEnabled, ir::handleAction, ir::poll, ir::status,
     ir::revision, ir::takeActionOutput},
    {ModuleId::Wifi, wifiModule::begin, wifiModule::setEnabled, wifiModule::handleAction, wifiModule::poll,
     wifiModule::status, wifiModule::revision, wifiModule::takeActionOutput},
};

constexpr size_t kModuleTotal = sizeof(kModules) / sizeof(kModules[0]);
uint32_t lastRevision[kModuleTotal] = {};
uint32_t lastPublishMs[kModuleTotal] = {};
uint32_t lastHeartbeatMs = 0;
const ModuleStatus *statusPtrs[kModuleTotal] = {};
radio::RadioArbiter radioArbiter;
uint32_t lastRestoreMs = 0;

radio::Owner radioOwner(ModuleId module) {
    return module == ModuleId::Unknown ? radio::kNoOwner : static_cast<radio::Owner>(module) + 1;
}

bool isSpiModule(ModuleId module) { return module == ModuleId::Cc1101 || module == ModuleId::Nrf24; }

const ModuleHandle *ownerModule(radio::Owner owner) {
    for (const auto &module : kModules) if (radioOwner(module.id) == owner) return &module;
    return nullptr;
}

void publishRadioState(uint32_t now, bool heartbeat = false) {
    static radio::WifiPhase previous = radio::WifiPhase::Idle;
    static radio::Owner previousOwner = radio::kNoOwner;
    static bool previousApRunning = false;
    static bool previousApRequested = false;
    static bool sent = false;
    const radio::WifiPhase phase = radioArbiter.wifiPhase();
    const radio::Owner owner = radioArbiter.ownerOf(radio::RadioResource::WifiExclusive);
    const bool apRunning = wifiAp::running();
    const bool apRequested = wifiAp::requested();
    if (sent && !heartbeat && phase == previous && owner == previousOwner && apRunning == previousApRunning &&
        apRequested == previousApRequested)
        return;
    const char *phaseName = "idle";
    switch (phase) {
    case radio::WifiPhase::Idle: break;
    case radio::WifiPhase::AwaitSuspend: phaseName = "await_suspend"; break;
    case radio::WifiPhase::Active: phaseName = "active"; break;
    case radio::WifiPhase::Restoring: phaseName = "restoring"; break;
    }
    const ModuleHandle *module = ownerModule(owner);
    const uint32_t delta = radioArbiter.wifiDeadline() - now;
    JsonDocument doc;
    doc["type"] = "radio_state";
    doc["phase"] = phaseName;
    doc["owner"] = module ? moduleName(module->id) : "";
    doc["remainingMs"] = owner != radio::kNoOwner && static_cast<int32_t>(delta) > 0 ? delta : 0;
    doc["maxDurationMs"] = radio::kMaxWifiExclusiveMs;
    doc["graceMs"] = radio::kWifiSuspendGraceMs;
    doc["apRunning"] = apRunning;
    doc["apRequested"] = apRequested;
    // Global USB presence is informational only: the frontend picks its warning
    // text from the per-client transport_info frame, so a browser on the AP is
    // never told the USB link saves it.
    doc["usbUp"] = usbNetwork::connected();
    std::string json;
    serializeJson(doc, json);
    webDashboard::publishFrame(json);
    previous = phase;
    previousOwner = owner;
    previousApRunning = apRunning;
    previousApRequested = apRequested;
    sent = true;
}

bool apRequestAdmitted() {
    const ModuleStatus &wifi = wifiModule::status();
    if (wifi.enabled || wifi.actionState == ActionState::Running || wifi.cleanupPending) return false;
    if (radioArbiter.wifiPhase() != radio::WifiPhase::Idle) return false;
    return radioArbiter.ownerOf(radio::RadioResource::WifiExclusive) == radio::kNoOwner;
}

// Explicit UART `ap on` / `ap off`: only admitted while the WiFi module is idle
// and no radio lease is held, so it can never race an action, cleanup or AP
// suspend/restore.
void handleApRequest(serialConsole::ApRequest request) {
    if (!apRequestAdmitted()) {
        serialConsole::replyApRequest(request, false, "wifi module busy or radio in use");
        return;
    }
    const bool on = request == serialConsole::ApRequest::On;
    const bool ok = on ? wifiAp::requestOn() : wifiAp::requestOff();
    serialConsole::replyApRequest(request, ok, ok ? "" : "hardware error");
    publishRadioState(millis()); // reflect apRunning/apRequested immediately
}

void releaseFinishedRadios() {
    for (const auto &module : kModules) {
        const ModuleStatus &status = module.status();
        if (status.actionState != ActionState::Running && !status.cleanupPending) {
            radioArbiter.release(radioOwner(module.id));
        }
    }
}

void pollRadio(uint32_t now) {
    releaseFinishedRadios();
    const ModuleHandle *expired = ownerModule(radioArbiter.expiredOwner(now));
    if (expired && expired->status().enabled) expired->setEnabled(false);
    if (radioArbiter.suspendDue(now)) {
        const ModuleHandle *owner = ownerModule(radioArbiter.ownerOf(radio::RadioResource::WifiExclusive));
        if (wifiAp::suspend()) radioArbiter.markSuspended();
        else if (owner) owner->setEnabled(false); // fail closed, cleanup before release
    }
    if (radioArbiter.restorePending() && static_cast<uint32_t>(now - lastRestoreMs) >= 1000) {
        lastRestoreMs = now;
        if (wifiAp::restore()) radioArbiter.restorationComplete();
        else radioArbiter.restorationError();
    }
    publishRadioState(now);
}

CommandError dispatchCommand(const WsCommand &command) {
    for (size_t i = 0; i < kModuleTotal; ++i) {
        if (kModules[i].id != command.module) continue;
        switch (command.cmd) {
        case CommandKind::Enable:
            if ((isSpiModule(command.module) && radioArbiter.spiBlockedFor(radioOwner(command.module))) ||
                (command.module == ModuleId::Wifi && radioArbiter.wifiPhase() != radio::WifiPhase::Idle)) {
                return CommandError::Busy;
            }
            return kModules[i].setEnabled(true);
        case CommandKind::Disable: return kModules[i].setEnabled(false);
        case CommandKind::Action: {
            const ActionDescriptor *descriptor = catalog::findAction(command.module, command.action);
            if (!descriptor) return CommandError::UnsupportedAction;
            const CommandError admission = actionAdmission(kModules[i].status(), descriptor->needsBuffer);
            if (admission != CommandError::None) return admission;
            if (command.module == ModuleId::Wifi && !radioArbiter.canAcquireWifi()) return CommandError::Busy;
            const radio::RadioResource resource = isSpiModule(command.module) ? radio::RadioResource::SharedSpi
                : descriptor->radioExclusive ? radio::RadioResource::WifiExclusive : radio::RadioResource::None;
            const radio::Owner owner = radioOwner(command.module);
            if (!radioArbiter.tryAcquire(owner, resource, millis())) return CommandError::Busy;
            const CommandError error = kModules[i].handleAction(command.action, command.params);
            if (error != CommandError::None) radioArbiter.release(owner);
            publishRadioState(millis());
            return error;
        }
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
    for (size_t i = 0; i < kModuleTotal; ++i) {
        publishModule(i, now, heartbeatDue);
        ActionOutput output;
        if (kModules[i].takeActionOutput(output)) {
            // Publish the generation before its first sample, even inside the
            // normal status coalescing window. Clients reject stale tickets.
            if (lastRevision[i] != kModules[i].revision()) {
                webDashboard::publishStatus(kModules[i].status());
                lastRevision[i] = kModules[i].revision();
                lastPublishMs[i] = now;
            }
            webDashboard::publishActionOutput(output);
        }
    }
    if (heartbeatDue) {
        lastHeartbeatMs = now;
        publishRadioState(now, true);
    }
}

// F0 snapshots drain into free UART space; no heartbeat logging or waiting.
void publishSerial() {
    static uint32_t loggedRevision[kModuleTotal] = {};
    static bool logged[kModuleTotal] = {};
    static size_t nextModule = 0;
    static char line[192];
    static size_t length = 0, offset = 0;
    if (offset == length) {
        length = offset = 0;
        for (size_t n = 0; n < kModuleTotal; ++n) {
            const size_t index = (nextModule + n) % kModuleTotal;
            const uint32_t revision = kModules[index].revision();
            if (logged[index] && loggedRevision[index] == revision) continue;
            const ModuleStatus &status = kModules[index].status();
            const int count = snprintf(line, sizeof(line),
                "[F0] ms=%lu %s en=%u ok=%u action=%s error=%s cleanup=%u seq=%lu detail=%.64s\n",
                static_cast<unsigned long>(status.lastUpdateMs), status.name,
                static_cast<unsigned>(status.enabled), static_cast<unsigned>(status.connected),
                actionStateName(status.actionState), actionErrorName(status.actionError),
                static_cast<unsigned>(status.cleanupPending),
                static_cast<unsigned long>(status.resultSequence), status.detail.c_str());
            if (count > 0) length = static_cast<size_t>(count) < sizeof(line)
                ? static_cast<size_t>(count) : sizeof(line) - 1;
            loggedRevision[index] = revision;
            logged[index] = true;
            nextModule = (index + 1) % kModuleTotal;
            break;
        }
    }
    const int available = Serial.availableForWrite();
    if (available <= 0 || offset == length) return;
    size_t count = length - offset;
    if (count > static_cast<size_t>(available)) count = static_cast<size_t>(available);
    if (count > 32) count = 32; // bounded UART work per loop
    offset += Serial.write(reinterpret_cast<const uint8_t *>(line + offset), count);
}

} // namespace

void setup() {
    Serial.begin(115200);
    serialConsole::begin();

    storage::begin();
    config = storage::load();

    wifiAp::begin(config);
    usbNetwork::begin(); // USB NCM/TCP-IP first: the dashboard must answer on it
    webDashboard::begin();

    for (size_t i = 0; i < kModuleTotal; ++i) kModules[i].begin();
    statusLed::begin();
    lastRestoreMs = millis() - 1000;
}

void loop() {
    pollRadio(millis());
    for (const auto &module : kModules) {
        const radio::Owner owner = radioOwner(module.id);
        const ModuleStatus &status = module.status();
        if (isSpiModule(module.id) && radioArbiter.spiBlockedFor(owner)) continue;
        if (radioArbiter.holds(owner, radio::RadioResource::WifiExclusive) &&
            radioArbiter.wifiPhase() == radio::WifiPhase::AwaitSuspend &&
            status.actionState == ActionState::Running && !status.cleanupPending) continue;
        if (module.id == ModuleId::Wifi && radioArbiter.wifiPhase() != radio::WifiPhase::Idle &&
            !radioArbiter.holds(owner, radio::RadioResource::WifiExclusive) && !status.cleanupPending) continue;
        module.poll();
    }
    releaseFinishedRadios();

    EnqueuedCommand request;
    while (webDashboard::nextCommand(request)) {
        webDashboard::reply(request, dispatchCommand(request.command));
    }

    // Serial control channel: same JSON command schema and dispatch path as the
    // WebSocket, so disruptive WiFi payloads that shadow the AP stay stoppable.
    // It also carries the plain `ap on` / `ap off` requests, admitted centrally.
    WsCommand serialCommand;
    serialConsole::ApRequest apRequest = serialConsole::ApRequest::None;
    if (serialConsole::nextCommand(serialCommand, apRequest)) {
        if (apRequest != serialConsole::ApRequest::None) {
            handleApRequest(apRequest);
        } else {
            CommandError error = CommandError::None;
            if (!serialCommand.correlationValid) error = CommandError::InvalidCommand;
            else if (serialCommand.error != CommandError::None) error = serialCommand.error;
            else error = dispatchCommand(serialCommand);
            serialConsole::reply(serialCommand, error);
        }
    }

    publishAll(millis());
    publishSerial();

    for (size_t i = 0; i < kModuleTotal; ++i) statusPtrs[i] = &kModules[i].status();
    statusLed::update(aggregateHealth(statusPtrs, kModuleTotal));

    webDashboard::loop();
}

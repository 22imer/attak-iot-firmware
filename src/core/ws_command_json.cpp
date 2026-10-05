#include "ws_command.h"

#include <ArduinoJson.h>

namespace {

bool parseModuleName(JsonVariantConst value, ModuleId &out) {
    if (!value.is<const char *>()) return false;
    const char *name = value.as<const char *>();
    if (!name) return false;

    const std::string_view text(name);
    if (text == "cc1101") { out = ModuleId::Cc1101; return true; }
    if (text == "nrf24") { out = ModuleId::Nrf24; return true; }
    if (text == "pn532") { out = ModuleId::Pn532; return true; }
    if (text == "ir") { out = ModuleId::Ir; return true; }
    if (text == "wifi") { out = ModuleId::Wifi; return true; }
    return false;
}

// Allowlist per spec §6.2: wifi/scan, pn532/read_uid, ir/capture. CC1101 and
// NRF24 have no action, so any well-typed action for them is unsupported.
bool parseAllowlistedAction(ModuleId module, std::string_view action, ActionId &out) {
    switch (module) {
    case ModuleId::Wifi:
        if (action == "scan") { out = ActionId::Scan; return true; }
        return false;
    case ModuleId::Pn532:
        if (action == "read_uid") { out = ActionId::ReadUid; return true; }
        return false;
    case ModuleId::Ir:
        if (action == "capture") { out = ActionId::Capture; return true; }
        return false;
    default:
        return false;
    }
}

} // namespace

WsCommand parseWsCommand(std::string_view json) {
    WsCommand out;

    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) != DeserializationError::Ok) return out;

    JsonVariantConst idValue = doc["id"];
    if (!idValue.is<uint32_t>()) return out; // rejects fractional/string/overflow/missing
    const uint32_t id = idValue.as<uint32_t>();
    if (id == 0) return out;

    ModuleId module = ModuleId::Unknown;
    if (!parseModuleName(doc["module"], module)) return out;

    out.id = id;
    out.module = module;
    out.correlationValid = true; // error still defaults to InvalidCommand

    JsonVariantConst cmdValue = doc["cmd"];
    if (!cmdValue.is<const char *>()) return out;
    const char *cmd = cmdValue.as<const char *>();
    const std::string_view cmdName(cmd);

    if (cmdName == "enable") {
        out.cmd = CommandKind::Enable;
        out.error = CommandError::None;
        return out;
    }
    if (cmdName == "disable") {
        out.cmd = CommandKind::Disable;
        out.error = CommandError::None;
        return out;
    }
    if (cmdName != "action") return out;

    out.cmd = CommandKind::Action;

    JsonVariantConst actionValue = doc["action"];
    if (!actionValue.is<const char *>()) return out;
    const char *action = actionValue.as<const char *>();
    const std::string_view actionName(action);
    if (actionName.empty()) return out;

    ActionId parsed = ActionId::None;
    if (!parseAllowlistedAction(module, actionName, parsed)) {
        out.error = CommandError::UnsupportedAction;
        return out;
    }
    out.action = parsed;
    out.error = CommandError::None;
    return out;
}

std::string commandResultToJson(uint32_t id, ModuleId module, CommandError error) {
    JsonDocument doc;
    doc["type"] = "command_result";
    doc["id"] = id;
    doc["module"] = moduleName(module);
    doc["ok"] = (error == CommandError::None);
    doc["error"] = commandErrorName(error);

    std::string out;
    serializeJson(doc, out);
    return out;
}

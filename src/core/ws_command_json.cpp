#include "ws_command.h"

#include <ArduinoJson.h>

#include "action_catalog.h"

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

    // The module's catalog is the single source of truth: an action not declared
    // there is unsupported. CC1101/NRF24 declare none, so any action for them is
    // rejected here, exactly as the old hand-written allowlist did.
    const ActionDescriptor *descriptor = catalog::findAction(module, actionName);
    if (descriptor == nullptr) {
        out.error = CommandError::UnsupportedAction;
        return out;
    }
    out.action = descriptor->action;
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

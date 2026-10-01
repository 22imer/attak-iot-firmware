#include "ws_command.h"

#include <ArduinoJson.h>

WsCommand parseWsCommand(const std::string &json) {
    WsCommand out;

    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return out;

    const char *module = doc["module"] | static_cast<const char *>(nullptr);
    const char *cmd = doc["cmd"] | static_cast<const char *>(nullptr);
    if (!module || !cmd) return out;

    std::string cmdStr = cmd;
    if (cmdStr != "enable" && cmdStr != "disable" && cmdStr != "action") return out;

    out.module = module;
    out.cmd = cmdStr;
    out.action = doc["action"] | "";
    out.valid = true;
    return out;
}

// Pure JSON encoding for ModuleStatus — no Arduino/network dependency, so
// this file compiles on both the ESP32 build and the native test env.
#include "module_status.h"

#include <ArduinoJson.h>

std::string statusToJson(const ModuleStatus &status) {
    JsonDocument doc;
    doc["module"] = status.name;
    doc["enabled"] = status.enabled;
    doc["connected"] = status.connected;
    doc["detail"] = status.detail;
    doc["output"] = status.output;
    doc["lastUpdateMs"] = status.lastUpdateMs;

    std::string out;
    serializeJson(doc, out);
    return out;
}

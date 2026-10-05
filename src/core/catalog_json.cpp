// Pure JSON encoding of the action catalog — no network dependency, so it
// compiles on both the ESP32 build and the native test env. Mirrors the
// ArduinoJson v7 patterns used in module_status_json.cpp / ws_command_json.cpp.
#include "action_catalog.h"

#include <ArduinoJson.h>

std::string catalogToJson() {
    JsonDocument doc;
    doc["type"] = "catalog";
    JsonArray modules = doc["modules"].to<JsonArray>();

    // Emit every selectable module in the fixed dashboard order, including those
    // with no actions so the UI can still show them.
    const ModuleId ids[] = {ModuleId::Cc1101, ModuleId::Nrf24, ModuleId::Pn532, ModuleId::Ir, ModuleId::Wifi};
    for (ModuleId id : ids) {
        JsonObject moduleObj = modules.add<JsonObject>();
        moduleObj["module"] = moduleName(id);
        JsonArray actions = moduleObj["actions"].to<JsonArray>();

        size_t count = 0;
        const ActionDescriptor *descriptors = catalog::moduleActions(id, count);
        for (size_t i = 0; i < count; ++i) {
            const ActionDescriptor &descriptor = descriptors[i];
            JsonObject entry = actions.add<JsonObject>();
            entry["id"] = descriptor.id;
            entry["label"] = descriptor.label;
            entry["kind"] = catalog::actionKindName(descriptor.kind);
            entry["tier"] = catalog::legalTierName(descriptor.tier);
            entry["radioExclusive"] = descriptor.radioExclusive;
            entry["needsBuffer"] = descriptor.needsBuffer;
        }
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

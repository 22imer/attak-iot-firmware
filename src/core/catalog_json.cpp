// Pure JSON encoding of the action catalog — no network dependency, so it
// compiles on both the ESP32 build and the native test env. Mirrors the
// ArduinoJson v7 patterns used in module_status_json.cpp / ws_command_json.cpp.
#include "action_catalog.h"

#include <ArduinoJson.h>

namespace {

// Appends one action entry, including its `params` array (empty when the
// descriptor declares no parameters). Numeric types emit min/max, String emits
// the effective byte cap; Boolean emits neither.
void appendAction(JsonArray actions, const ActionDescriptor &descriptor) {
    JsonObject entry = actions.add<JsonObject>();
    entry["id"] = descriptor.id;
    entry["label"] = descriptor.label;
    entry["kind"] = catalog::actionKindName(descriptor.kind);
    entry["tier"] = catalog::legalTierName(descriptor.tier);
    entry["radioExclusive"] = descriptor.radioExclusive;
    entry["needsBuffer"] = descriptor.needsBuffer;

    JsonArray params = entry["params"].to<JsonArray>();
    if (descriptor.params == nullptr) return;
    for (size_t i = 0; i < descriptor.paramCount; ++i) {
        const ParamSpec &spec = descriptor.params[i];
        JsonObject param = params.add<JsonObject>();
        param["name"] = spec.name;
        param["label"] = spec.label;
        param["type"] = catalog::paramTypeName(spec.type);
        param["required"] = spec.required;
        switch (spec.type) {
        case ParamType::Integer:
        case ParamType::Number:
            param["min"] = spec.minimum;
            param["max"] = spec.maximum;
            break;
        case ParamType::String:
            param["maxLength"] = (spec.maxLength == 0 || spec.maxLength > kMaxParamStringBytes)
                                     ? kMaxParamStringBytes
                                     : spec.maxLength;
            break;
        case ParamType::Boolean:
            break;
        }
    }
}

} // namespace

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
            appendAction(actions, descriptors[i]);
        }
    }

    std::string out;
    serializeJson(doc, out);
    return out;
}

#include "ws_command.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>
#include <string_view>

#include "action_catalog.h"
#include "json_strict.h"

namespace {

// 2^63 and -2^63 are exactly representable as double, so they are safe range
// guards for int64 comparisons. Converting an int64 *to* double can round by up
// to ~1024 near the top of the range, so a value must never be rounded before
// being checked against a double bound: these helpers compare exactly.
constexpr double kTwoPow63 = 9223372036854775808.0;

// value >= bound, without rounding `value`.
bool integerAtLeast(int64_t value, double bound) {
    if (std::isnan(bound)) return false;
    if (bound >= kTwoPow63) return false; // bound above every int64
    if (bound <= -kTwoPow63) return true;
    int64_t threshold = static_cast<int64_t>(bound); // truncates toward zero
    if (static_cast<double>(threshold) < bound) ++threshold; // ceil
    return value >= threshold;
}

// value <= bound, without rounding `value`.
bool integerAtMost(int64_t value, double bound) {
    if (std::isnan(bound)) return false;
    if (bound >= kTwoPow63) return true;
    if (bound < -kTwoPow63) return false;
    int64_t threshold = static_cast<int64_t>(bound);
    if (static_cast<double>(threshold) > bound) --threshold; // floor
    return value <= threshold;
}

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

// The spec whose name matches, or nullptr for an unknown key.
const ParamSpec *findSpec(const ActionDescriptor &descriptor, std::string_view name) {
    for (size_t i = 0; i < descriptor.paramCount; ++i) {
        if (name == descriptor.params[i].name) return &descriptor.params[i];
    }
    return nullptr;
}

// Type/range/byte-cap check for one supplied value. Never reads past the JSON
// document: the string branch copies into ActionParams' own storage.
bool applyParam(const ParamSpec &spec, JsonVariantConst value, ActionParams &out, size_t index) {
    switch (spec.type) {
    case ParamType::Integer: {
        // is<int64_t>() is true only for JSON integer variants that fit int64:
        // floats, strings, bools and null are all rejected. The bound check is
        // done on the integer value itself so a double bound cannot round an
        // out-of-range int64 back inside the range.
        if (!value.is<int64_t>()) return false;
        const int64_t raw = value.as<int64_t>();
        if (!integerAtLeast(raw, spec.minimum) || !integerAtMost(raw, spec.maximum)) return false;
        return out.setInteger(index, raw);
    }
    case ParamType::Number: {
        // is<double>() accepts JSON integers and floats but rejects bool/string.
        if (!value.is<double>()) return false;
        const double numeric = value.as<double>();
        if (!std::isfinite(numeric)) return false; // NaN fails the range checks too
        if (!(numeric >= spec.minimum && numeric <= spec.maximum)) return false;
        return out.setNumber(index, numeric);
    }
    case ParamType::Boolean:
        if (!value.is<bool>()) return false;
        return out.setBoolean(index, value.as<bool>());
    case ParamType::String: {
        if (!value.is<JsonString>()) return false;
        const JsonString text = value.as<JsonString>();
        if (text.isNull()) return false;
        const size_t limit = (spec.maxLength == 0 || spec.maxLength > kMaxParamStringBytes)
                                 ? kMaxParamStringBytes
                                 : spec.maxLength;
        if (text.size() > limit) return false;
        // size() is the stored length, so a decoded \u0000 is visible here even
        // though c_str() would otherwise look truncated.
        if (text.size() != 0 && memchr(text.c_str(), '\0', text.size()) != nullptr) return false;
        return out.setString(index, text.c_str(), text.size());
    }
    }
    return false;
}

// Validates the `params` member of a command. `paramsPresent` distinguishes an
// omitted member (legal when every declared parameter is optional) from an
// explicit `null` or any non-object value, both of which are InvalidParams.
// Absence of an individual value is decided by containsKey, never by isNull(),
// so an explicit typed null is rejected instead of being treated as absent.
CommandError validateParams(bool paramsPresent, JsonVariantConst params, const ActionDescriptor &descriptor,
                            ActionParams &out) {
    if (!validateActionDescriptor(descriptor)) return CommandError::InvalidParams;

    out.reset(descriptor.paramCount);

    JsonObjectConst object;
    if (paramsPresent) {
        if (!params.is<JsonObjectConst>()) return CommandError::InvalidParams; // explicit null / array / scalar
        object = params.as<JsonObjectConst>();
        // Any supplied key must name a declared parameter; a declared key whose
        // value is an explicit typed null is caught per-spec below.
        for (JsonPairConst pair : object) {
            const JsonString key = pair.key();
            const std::string_view name(key.c_str(), key.size());
            if (findSpec(descriptor, name) == nullptr) return CommandError::InvalidParams;
        }
    }

    for (size_t i = 0; i < descriptor.paramCount; ++i) {
        const ParamSpec &spec = descriptor.params[i];
        if (!paramsPresent || !object.containsKey(spec.name)) {
            if (spec.required) return CommandError::InvalidParams;
            continue; // omitted optional stays absent
        }
        const JsonVariantConst value = object[spec.name];
        if (value.isNull()) return CommandError::InvalidParams; // explicit typed null
        if (!applyParam(spec, value, out, i)) return CommandError::InvalidParams;
    }
    return CommandError::None;
}

} // namespace

CommandError parseActionParams(std::string_view json, const ActionDescriptor &descriptor, ActionParams &out) {
    if (json.empty()) return validateParams(false, JsonVariantConst(), descriptor, out);

    // The whole member must be exactly one JSON value: deserializeJson() alone
    // would accept a valid object prefix plus trailing garbage/extensions.
    if (!isStrictJsonValue(json.data(), json.size())) return CommandError::InvalidParams;

    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) != DeserializationError::Ok) {
        return CommandError::InvalidParams;
    }
    return validateParams(true, doc.as<JsonVariantConst>(), descriptor, out);
}

WsCommand parseWsCommand(std::string_view json) {
    WsCommand out;

    // Reject a valid-command prefix with trailing garbage or non-RFC syntax
    // before deserializeJson() would silently accept it.
    if (!isStrictJsonValue(json.data(), json.size())) return out;

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
    // rejected here, exactly as the old hand-written allowlist did. Missing or
    // unsupported actions keep their precedence over params validation.
    const ActionDescriptor *descriptor = catalog::findAction(module, actionName);
    if (descriptor == nullptr) {
        out.error = CommandError::UnsupportedAction;
        return out;
    }
    out.action = descriptor->action;

    // Presence, not null-ness, decides whether the member was supplied: an
    // explicit `"params": null` must be rejected, not treated as omitted.
    const JsonObjectConst root = doc.as<JsonObjectConst>();
    const CommandError paramError =
        validateParams(root.containsKey("params"), root["params"], *descriptor, out.params);
    out.error = paramError;
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

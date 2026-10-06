// Portable, Arduino-free implementation of the ActionParams value store and the
// descriptor schema check. Kept out of ws_command_json.cpp so the parameter
// container (and its tests) do not pull in ArduinoJson, and so the schema check
// can reject a mis-declared descriptor before the parser ever looks at JSON.
#include "action_params.h"

#include <cmath>
#include <cstring>

#include "action_catalog.h"

void ActionParams::reset(size_t count) {
    if (count > kMaxActionParams) count = kMaxActionParams;
    count_ = count;
    // Clear every slot, not just the declared ones: a later reset() can grow the
    // count back, and a stale present/type from the previous action must never
    // leak into the new one.
    for (size_t i = 0; i < kMaxActionParams; ++i) values_[i] = ActionParamValue{};
}

bool ActionParams::present(size_t index) const {
    return index < count_ && values_[index].present;
}

ParamType ActionParams::type(size_t index) const {
    return index < count_ ? values_[index].type : ParamType::Integer;
}

int64_t ActionParams::integer(size_t index) const {
    return index < count_ ? values_[index].integer : 0;
}

double ActionParams::number(size_t index) const {
    return index < count_ ? values_[index].number : 0.0;
}

bool ActionParams::boolean(size_t index) const {
    return index < count_ ? values_[index].boolean : false;
}

const char *ActionParams::string(size_t index) const {
    return index < count_ ? values_[index].text : "";
}

size_t ActionParams::stringLength(size_t index) const {
    return index < count_ ? values_[index].textLength : 0;
}

bool ActionParams::setInteger(size_t index, int64_t value) {
    if (index >= count_) return false;
    ActionParamValue &slot = values_[index];
    slot.type = ParamType::Integer;
    slot.integer = value;
    slot.present = true;
    return true;
}

bool ActionParams::setNumber(size_t index, double value) {
    if (index >= count_) return false;
    if (!std::isfinite(value)) return false;
    ActionParamValue &slot = values_[index];
    slot.type = ParamType::Number;
    slot.number = value;
    slot.present = true;
    return true;
}

bool ActionParams::setBoolean(size_t index, bool value) {
    if (index >= count_) return false;
    ActionParamValue &slot = values_[index];
    slot.type = ParamType::Boolean;
    slot.boolean = value;
    slot.present = true;
    return true;
}

bool ActionParams::setString(size_t index, const char *data, size_t length) {
    if (index >= count_) return false;
    if (data == nullptr) return length == 0 ? true : false;
    if (length > kMaxParamStringBytes) return false;
    // Embedded NUL can never be represented in the NUL-terminated slot, so reject
    // it rather than silently truncating the caller's data.
    if (length != 0 && std::memchr(data, '\0', length) != nullptr) return false;

    ActionParamValue &slot = values_[index];
    if (length != 0) std::memcpy(slot.text, data, length);
    slot.text[length] = '\0';
    slot.textLength = length;
    slot.type = ParamType::String;
    slot.present = true;
    return true;
}

bool validateActionDescriptor(const ActionDescriptor &descriptor) {
    if (descriptor.paramCount > kMaxActionParams) return false;
    if (descriptor.paramCount != 0 && descriptor.params == nullptr) return false;

    for (size_t i = 0; i < descriptor.paramCount; ++i) {
        const ParamSpec &spec = descriptor.params[i];
        if (spec.name == nullptr || spec.label == nullptr) return false;

        switch (spec.type) {
        case ParamType::Integer:
        case ParamType::Number:
            if (!std::isfinite(spec.minimum) || !std::isfinite(spec.maximum)) return false;
            if (spec.minimum > spec.maximum) return false;
            break;
        case ParamType::Boolean:
            break;
        case ParamType::String:
            if (spec.maxLength > kMaxParamStringBytes) return false;
            break;
        default:
            return false; // not a declared ParamType value
        }

        for (size_t j = 0; j < i; ++j) {
            if (std::strcmp(spec.name, descriptor.params[j].name) == 0) return false;
        }
    }
    return true;
}

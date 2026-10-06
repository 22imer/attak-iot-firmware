// Pure JSON encoding for one streamed ActionOutput frame. Portable: ArduinoJson
// only, so it compiles on the ESP32 build and the native test env alike.
#include "module_status.h"

#include <ArduinoJson.h>

#include <string>

#include "json_strict.h"
#include "module_runtime.h"

std::string actionOutputToJson(const ActionOutput &output) {
    // The payload must already be a complete JSON value: we embed it as a
    // nested value, never as an escaped opaque string. Validate the whole
    // buffer lexically first — deserializeJson() alone would accept a valid
    // prefix followed by trailing garbage or non-RFC extensions — and drop the
    // frame rather than broadcast it, so a bad producer cannot corrupt a
    // client's JSON.
    if (!isStrictJsonValue(output.payload.data(), output.payload.size(), kMaxActionOutputBytes)) return "";

    JsonDocument payload;
    if (deserializeJson(payload, output.payload) != DeserializationError::Ok) return "";

    JsonDocument doc;
    doc["type"] = "action_output";
    doc["module"] = output.module != nullptr ? output.module : "";
    doc["action"] = output.action != nullptr ? output.action : "";
    doc["ticket"] = output.ticket;
    doc["sequence"] = output.sequence;
    doc["uptimeMs"] = output.uptimeMs;
    doc["payload"].set(payload.as<JsonVariantConst>());

    std::string json;
    serializeJson(doc, json);
    return json;
}

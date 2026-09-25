// Pure JSON mapping for DeviceConfig — no LittleFS/Serial dependency, so
// this file compiles on both the ESP32 build and the native test env.
#include "storage.h"

#include <ArduinoJson.h>

namespace storage {

std::string toJson(const DeviceConfig &config) {
    JsonDocument doc;
    doc["apSsid"] = config.apSsid;
    doc["apPassword"] = config.apPassword;
    doc["deviceName"] = config.deviceName;

    std::string out;
    serializeJson(doc, out);
    return out;
}

DeviceConfig fromJson(const std::string &json) {
    DeviceConfig config;
    JsonDocument doc;
    if (deserializeJson(doc, json) == DeserializationError::Ok) {
        config.apSsid = doc["apSsid"] | config.apSsid;
        config.apPassword = doc["apPassword"] | config.apPassword;
        config.deviceName = doc["deviceName"] | config.deviceName;
    }
    return config;
}

} // namespace storage

#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

namespace storage {

namespace {
constexpr const char *kConfigPath = "/config.json";
}

void begin() {
    if (!LittleFS.begin(true)) { Serial.println("storage: LittleFS mount failed"); }
}

DeviceConfig load() {
    DeviceConfig config;
    File f = LittleFS.open(kConfigPath, "r");
    if (!f) return config;

    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        config.apSsid = doc["apSsid"] | config.apSsid;
        config.apPassword = doc["apPassword"] | config.apPassword;
        config.deviceName = doc["deviceName"] | config.deviceName;
    }
    f.close();
    return config;
}

void save(const DeviceConfig &config) {
    JsonDocument doc;
    doc["apSsid"] = config.apSsid;
    doc["apPassword"] = config.apPassword;
    doc["deviceName"] = config.deviceName;

    File f = LittleFS.open(kConfigPath, "w");
    if (!f) {
        Serial.println("storage: failed to open config for write");
        return;
    }
    serializeJson(doc, f);
    f.close();
}

} // namespace storage

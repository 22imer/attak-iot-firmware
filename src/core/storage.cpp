#include "storage.h"

#include <LittleFS.h>

namespace storage {

namespace {
constexpr const char *kConfigPath = "/config.json";
}

void begin() {
    if (!LittleFS.begin(true)) { Serial.println("storage: LittleFS mount failed"); }
}

DeviceConfig load() {
    File f = LittleFS.open(kConfigPath, "r");
    if (!f) return DeviceConfig();

    std::string content;
    while (f.available()) content += static_cast<char>(f.read());
    f.close();
    return fromJson(content);
}

void save(const DeviceConfig &config) {
    File f = LittleFS.open(kConfigPath, "w");
    if (!f) {
        Serial.println("storage: failed to open config for write");
        return;
    }
    std::string json = toJson(config);
    f.write(reinterpret_cast<const uint8_t *>(json.data()), json.size());
    f.close();
}

} // namespace storage

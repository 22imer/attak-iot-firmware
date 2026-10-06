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

bool readTextFile(const char *path, std::string &out, size_t maxBytes) {
    File probe = LittleFS.open(path, "r");
    if (!probe) return false;
    const size_t size = probe.size();
    probe.close();
    // Refuse an empty or oversized file instead of streaming it: the caller
    // falls back to the built-in page, and flash content never lands in RAM
    // unbounded.
    if (size == 0 || size > maxBytes) return false;

    File f = LittleFS.open(path, "r");
    if (!f) return false;
    std::string content;
    content.reserve(size);
    uint8_t buffer[128];
    while (f.available()) {
        const size_t read = f.read(buffer, sizeof(buffer));
        if (read == 0) break;
        content.append(reinterpret_cast<const char *>(buffer), read);
    }
    f.close();
    if (content.empty()) return false;
    out = std::move(content);
    return true;
}

} // namespace storage

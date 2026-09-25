// Portable (no Arduino.h dependency) so DeviceConfig and the JSON codec
// below compile on the native test environment as well as the ESP32 build.
// begin()/load()/save() touch LittleFS and stay ESP32-only, in storage.cpp.
#pragma once

#include <string>

struct DeviceConfig {
    std::string apSsid = "AttakIoT";
    std::string apPassword = "attakiot123";
    std::string deviceName = "attak-iot-01";
};

namespace storage {
    // Mounts LittleFS. Call once from setup(), before load()/save().
    void begin();

    // Reads /config.json, falling back to DeviceConfig defaults for any
    // missing field (including a missing file).
    DeviceConfig load();

    void save(const DeviceConfig &config);

    // Pure JSON mapping — no filesystem I/O, testable on native builds.
    std::string toJson(const DeviceConfig &config);
    DeviceConfig fromJson(const std::string &json);
}

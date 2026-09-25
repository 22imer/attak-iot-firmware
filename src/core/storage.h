#pragma once

#include <Arduino.h>

struct DeviceConfig {
    String apSsid = "AttakIoT";
    String apPassword = "attakiot123";
    String deviceName = "attak-iot-01";
};

namespace storage {
    // Mounts LittleFS. Call once from setup(), before load()/save().
    void begin();

    // Reads /config.json, falling back to DeviceConfig defaults for any
    // missing field (including a missing file).
    DeviceConfig load();

    void save(const DeviceConfig &config);
}

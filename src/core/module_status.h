// Common status contract every peripheral module reports, so the web
// dashboard can render all four modules uniformly. Portable (no Arduino.h
// dependency) so it — and statusToJson() below — compile on the native
// test environment as well as the ESP32 build.
#pragma once

#include <cstdint>
#include <string>

struct ModuleStatus {
    const char *name;       // "cc1101" | "nrf24" | "pn532" | "ir" | "wifi"
    bool enabled;            // true once toggled on from the dashboard; a
                             // module's poll() is a no-op while disabled
    bool connected;          // liveness result — only meaningful while enabled
    std::string detail;     // human-readable health info (chip id, error, ...)
    std::string output;     // payload result (scanned SSIDs, last UID read,
                             // capture state, ...) — separate from `detail`
                             // so health and payload output don't collide
    uint32_t lastUpdateMs;
};

// Pure JSON encoding — no network I/O, testable on native builds.
// Shape: {"module": "cc1101", "enabled": false, "connected": false,
//         "detail": "...", "output": "...", "lastUpdateMs": 0}
std::string statusToJson(const ModuleStatus &status);

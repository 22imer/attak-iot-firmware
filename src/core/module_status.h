// Common status contract every peripheral module reports, so the web
// dashboard can render all four modules uniformly. Portable (no Arduino.h
// dependency) so it — and statusToJson() below — compile on the native
// test environment as well as the ESP32 build.
#pragma once

#include <cstdint>
#include <string>

struct ModuleStatus {
    const char *name;       // "cc1101" | "nrf24" | "pn532" | "ir"
    bool connected;
    std::string detail;     // human-readable extra info (chip id, error, ...)
    uint32_t lastUpdateMs;
};

// Pure JSON encoding — no network I/O, testable on native builds.
// Shape: {"module": "cc1101", "connected": false, "detail": "...", "lastUpdateMs": 0}
std::string statusToJson(const ModuleStatus &status);

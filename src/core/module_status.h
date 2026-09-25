// Common status contract every peripheral module reports, so the web
// dashboard can render all four modules uniformly.
#pragma once

#include <Arduino.h>

struct ModuleStatus {
    const char *name;       // "cc1101" | "nrf24" | "pn532" | "ir"
    bool connected;
    String detail;          // human-readable extra info (chip id, error, ...)
    uint32_t lastUpdateMs;
};

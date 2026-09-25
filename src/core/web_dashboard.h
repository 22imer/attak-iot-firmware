#pragma once

#include "module_status.h"

namespace webDashboard {
    // Starts the HTTP + WebSocket server. Call once from setup(), after
    // storage::begin() (serves data/ via LittleFS) and wifiAp::begin().
    void begin();

    // Broadcasts one module's current status to every connected dashboard
    // client over the /ws WebSocket. v1 message shape:
    //   {"module": "cc1101", "connected": false, "detail": "...", "lastUpdateMs": 0}
    void publishStatus(const ModuleStatus &status);

    // Call every loop() iteration to let AsyncWebSocket reap dead clients.
    void loop();
}

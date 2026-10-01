#pragma once

#include "module_status.h"
#include "ws_command.h"

namespace webDashboard {
    using CommandHandler = void (*)(const WsCommand &command);

    // Starts the HTTP + WebSocket server. Call once from setup(), after
    // storage::begin() (serves data/ via LittleFS) and wifiAp::begin().
    void begin();

    // Broadcasts one module's current status to every connected dashboard
    // client over the /ws WebSocket. v2 message shape:
    //   {"module": "cc1101", "enabled": false, "connected": false,
    //    "detail": "...", "output": "...", "lastUpdateMs": 0}
    void publishStatus(const ModuleStatus &status);

    // Registers the single callback that receives every valid inbound
    // dashboard command (enable/disable/action). The dashboard is
    // deliberately decoupled from module internals — main.cpp, which
    // already knows about every module, is the dispatch point.
    void onCommand(CommandHandler handler);

    // Call every loop() iteration to let AsyncWebSocket reap dead clients.
    void loop();
}

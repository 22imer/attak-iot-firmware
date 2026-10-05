#pragma once

#include "command_queue.h"
#include "module_status.h"

namespace webDashboard {
// Starts the HTTP + WebSocket server. Call once from setup(), after
// storage::begin() (serves data/ via LittleFS) and wifiAp::begin().
void begin();

// Loop-owned dequeue: returns the oldest accepted command (pure FIFO) and its
// originating client/session. The WebSocket callback only parses and enqueues.
bool nextCommand(EnqueuedCommand &request);

// Sends a command_result for `request` back to its originating client, but only
// if that same session is still connected. Never broadcasts an ack.
void reply(const EnqueuedCommand &request, CommandError error);

// Broadcasts one module's status to every connected dashboard client.
void publishStatus(const ModuleStatus &status);

// Call every loop() iteration to let AsyncWebSocket reap dead clients.
void loop();
} // namespace webDashboard

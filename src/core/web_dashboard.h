#pragma once

#include <cstdint>
#include <string>

#include "command_queue.h"
#include "module_status.h"

namespace webDashboard {
// Starts the HTTP + WebSocket server. Call once from setup(), after
// storage::begin() (serves data/ via LittleFS), usbNetwork::begin() (so the USB
// netif answers) and wifiAp::begin(). Each connecting client receives a
// transport_info frame
// {"type":"transport_info","transport":"usb"|"ap","address":"<localIP>"} built
// from that client's own local address, so the UI can tailor radio warnings.
void begin();

// Loop-owned dequeue: returns the oldest accepted command (pure FIFO) and its
// originating client/session. The WebSocket callback only parses and enqueues.
bool nextCommand(EnqueuedCommand &request);

// Sends a command_result for `request` back to its originating client, but only
// if that same session is still connected. Never broadcasts an ack.
void reply(const EnqueuedCommand &request, CommandError error);

// Broadcasts one module's status to every connected dashboard client.
void publishStatus(const ModuleStatus &status);

// Broadcasts one streamed action_output frame to every connected client. Drops
// (never sends) a frame whose payload is not valid JSON. Independent of the
// command-result reply path: a stream is never a final result.
void publishActionOutput(const ActionOutput &output);

// Broadcasts a loop-owned control-plane frame (radio_state).
void publishFrame(const std::string &json);

// --- Evil portal (disruptive; PLAN §2.4) ----------------------------------
// While enabled, HTTP requests that arrived on the management AP's own address
// are answered with the captive-portal page and their POST bodies captured as
// credential attempts. The dashboard's own pages are intentionally shadowed for
// AP clients; USB dashboard requests (a different local address) are untouched,
// so Stop stays available over USB. The Serial console remains the control
// channel either way.
void enableEvilPortal(const std::string &page);
void disableEvilPortal();

// Moves out the oldest captured POST body (raw, URL-encoded) with its 1-based
// sequence. Returns false when no capture is pending. The caller decodes the
// form fields (evilTwin::parseFormCredentials) when it builds the stream frame.
bool takePortalCapture(std::string &body, uint32_t &sequence);

// Total captures observed since boot.
uint32_t portalCaptureCount();

// Call every loop() iteration to let AsyncWebSocket reap dead clients.
void loop();
} // namespace webDashboard

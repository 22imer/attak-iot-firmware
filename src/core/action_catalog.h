// Per-module action catalog — the single source of truth for which actions a
// module exposes. The WebSocket parser (ws_command_json.cpp) validates inbound
// actions against it, and the dashboard renders its selectable action list from
// the `catalog` frame built here. Adding an action means adding a descriptor,
// not editing the parser or the dashboard.
//
// Portable (no Arduino.h) so it — and the native tests — link it too. The JSON
// serializer lives in catalog_json.cpp (ArduinoJson), declared at the bottom.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "command_types.h"

// How the dashboard should drive an action (Phase A only uses OneShot/Record).
enum class ActionKind : uint8_t { OneShot, Continuous, Record, Replay };

// Legal/safety tier shown in the UI; Disruptive gets a confirm gate.
enum class LegalTier : uint8_t { Observe, ActiveOwn, Disruptive };

struct ActionDescriptor {
    const char *id;         // wire id, e.g. "scan" (matches spec §6.2)
    const char *label;      // display label, e.g. "Quét WiFi"
    ActionId action;        // dispatch enum handed to the module's handleAction()
    ActionKind kind;
    LegalTier tier;
    bool radioExclusive;    // needs exclusive radio (teardown dashboard AP) — future phases
    bool needsBuffer;       // Replay actions that consume a prior Record buffer
};

namespace catalog {

// The descriptors for one module, or {nullptr, 0} for a module with no actions
// (cc1101, nrf24 in Phase A).
const ActionDescriptor *moduleActions(ModuleId module, size_t &count);

// The descriptor matching `id` within `module`, or nullptr if none — this is the
// admission check that replaces the hand-written allowlist.
const ActionDescriptor *findAction(ModuleId module, std::string_view id);

const char *actionKindName(ActionKind kind); // "oneshot" | "continuous" | "record" | "replay"
const char *legalTierName(LegalTier tier);   // "observe" | "active_own" | "disruptive"

} // namespace catalog

// {"type":"catalog","modules":[{"module":"wifi","actions":[{id,label,kind,tier,...}]}]}
// Defined in catalog_json.cpp. Sent to each dashboard client on connect.
std::string catalogToJson();

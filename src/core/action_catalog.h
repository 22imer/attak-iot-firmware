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

#include "action_params.h"
#include "command_types.h"

// How the dashboard should drive an action.
enum class ActionKind : uint8_t { OneShot, Continuous, Record, Replay };

// Legal/safety tier shown in the UI; Disruptive gets a confirm gate.
enum class LegalTier : uint8_t { Observe, ActiveOwn, Disruptive };

struct ActionDescriptor {
    const char *id;         // wire id, e.g. "scan" (matches spec §6.2)
    const char *label;      // display label, e.g. "Quét WiFi"
    ActionId action;        // dispatch enum handed to the module's handleAction()
    ActionKind kind;
    LegalTier tier;
    bool radioExclusive;    // exclusive WiFi radio: AP grace/deadline/restore through main arbiter
    bool needsBuffer;       // consumes a prior Record buffer (Replay or NFC UID clone)
    // Optional parameter schema. Left out (nullptr/0) by the real no-param
    // actions so their initializers stay simple.
    const ParamSpec *params = nullptr;
    size_t paramCount = 0;
};

namespace catalog {

// The descriptors for one module, or {nullptr, 0} for an unknown/empty module.
const ActionDescriptor *moduleActions(ModuleId module, size_t &count);

// The descriptor matching `id` within `module`, or nullptr if none — this is the
// admission check that replaces the hand-written allowlist.
const ActionDescriptor *findAction(ModuleId module, std::string_view id);

// The descriptor dispatching to `action` within `module`, or nullptr — used by
// the integrator to resolve an enqueued command back to its descriptor.
const ActionDescriptor *findAction(ModuleId module, ActionId action);

const char *actionKindName(ActionKind kind); // "oneshot" | "continuous" | "record" | "replay"
const char *legalTierName(LegalTier tier);   // "observe" | "active_own" | "disruptive"
const char *paramTypeName(ParamType type);   // "integer" | "number" | "boolean" | "string"

} // namespace catalog

// {"type":"catalog","modules":[{"module":"wifi","actions":[{id,label,kind,tier,...}]}]}
// Defined in catalog_json.cpp. Sent to each dashboard client on connect.
std::string catalogToJson();

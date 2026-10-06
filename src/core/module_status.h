// Common status contract every peripheral module reports, so the web
// dashboard can render all categories uniformly. Portable (no Arduino.h
// dependency) so it — and statusToJson() below — compile on the native test
// environment as well as the ESP32 build.
#pragma once

#include <cstdint>
#include <string>

// Action lifecycle: idle -> running -> succeeded | timeout | error.
enum class ActionState : uint8_t { Idle, Running, Succeeded, Timeout, Error };

// Async failure reason, distinct from command admission errors (CommandError).
// Timeout codes pair with ActionState::Timeout; the rest with ActionState::Error.
enum class ActionError : uint8_t {
    None,
    ScanFailed,
    ScanTimeout,
    ReadTimeout,
    CaptureTimeout,
    CaptureTooLong,
    HardwareError,
    NfcTagError, // presented tag does not satisfy the action's constraints
};

struct ModuleStatus {
    const char *name;        // "cc1101" | "nrf24" | "pn532" | "ir" | "wifi"
    bool enabled;            // operator intent; true once toggled on
    bool connected;          // liveness result — only meaningful while enabled
    std::string detail;      // health explanation, never an action error
    std::string output;      // last successful payload (JSON per spec §7), "" if none
    uint32_t lastUpdateMs;   // ESP32 uptime at the last state/health change
    ActionState actionState; // action lifecycle
    ActionError actionError; // "" on wire when None
    bool cleanupPending;     // backend has not released resources yet
    uint32_t resultSequence; // increments on every success, even identical payload
    uint32_t resultUpdateMs; // uptime of the last success; 0 when no payload

    // Phase 1 additions — defaults keep every existing aggregate initializer and
    // wire consumer valid.
    bool hasBuffer = false;        // a Record payload is retained and replayable
    const char *activeAction = ""; // descriptor id of the running action, "" otherwise
    uint32_t actionTicket = 0;     // stream generation of the running action, 0 when idle
};

// One streamed sample from a Continuous action. Transport DTO only: ModuleRuntime
// owns the single pending frame, and the dashboard validates ticket/sequence
// before rendering. A stream frame is never a command result.
struct ActionOutput {
    const char *module = "";
    const char *action = "";
    uint32_t ticket = 0;
    uint32_t sequence = 0;
    uint32_t uptimeMs = 0;
    std::string payload; // validated JSON value, never an opaque escaped string
};

const char *actionStateName(ActionState state); // "idle" | ... | "error"
const char *actionErrorName(ActionError error); // "" | "scan_failed" | ...

// Pure JSON encoding — no network I/O, testable on native builds. Emits the
// spec §6.1 field names; enums become strings, never their numeric value.
std::string statusToJson(const ModuleStatus &status);

// {"type":"action_output","module":...,"action":...,"ticket":N,"sequence":N,
//  "uptimeMs":N,"payload":<embedded JSON value>}. Returns "" when payload is not
// valid JSON — a malformed sample is dropped, never emitted as an escaped
// opaque string. Defined in action_output_json.cpp.
std::string actionOutputToJson(const ActionOutput &output);

// Inbound dashboard commands — the WebSocket is now bidirectional (v2):
// the dashboard sends {"module": "...", "cmd": "enable"|"disable"|"action",
// "action": "..."} to toggle a module on/off or trigger a named payload
// action (e.g. "record", "replay", "scan"). Portable (no Arduino.h
// dependency) so parseWsCommand() compiles on the native test env too.
#pragma once

#include <string>

struct WsCommand {
    bool valid = false;
    std::string module;
    std::string cmd;     // "enable" | "disable" | "action"
    std::string action;  // only meaningful when cmd == "action"
};

// Pure parsing — no network I/O, testable on native builds. Returns
// valid=false for malformed JSON or a missing/unrecognized "cmd".
WsCommand parseWsCommand(const std::string &json);

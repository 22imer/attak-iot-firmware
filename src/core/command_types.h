// Typed identifiers shared by the WebSocket parser (ws_command_json.cpp),
// the command queue (command_queue.cpp) and every module. Portable — no
// Arduino.h — so the native tests link them too.
#pragma once

#include <cstddef>
#include <cstdint>

// Order matters: the numeric values are the index used by
// CommandQueue::stopQueued_ (reserved-Stop slots per module).
enum class ModuleId : uint8_t { Cc1101 = 0, Nrf24 = 1, Pn532 = 2, Ir = 3, Wifi = 4, Unknown = 5 };

enum class CommandKind : uint8_t { Enable, Disable, Action };
enum class ActionId : uint8_t { None, Scan, ReadUid, Capture };

enum class CommandError : uint8_t {
    None,
    InvalidCommand,
    UnsupportedAction,
    QueueFull,
    ModuleOff,
    Busy,
    HardwareError,
};

// Selectable modules (excludes Unknown); also the reserved-Stop table size.
constexpr size_t kModuleCount = 5;

// Wire names used by status/command_result payloads. Unknown/None -> "".
// Implemented as fixed switches (no registry) in module_runtime.cpp.
const char *moduleName(ModuleId id);
const char *commandErrorName(CommandError error);

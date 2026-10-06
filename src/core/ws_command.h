// Inbound dashboard commands. The WebSocket is bidirectional: the dashboard
// sends {"id":N,"module":"...","cmd":"enable"|"disable"|"action","action":"..."}.
// Portable (no Arduino.h) so parseWsCommand()/commandResultToJson() compile on
// the native test env too.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "action_params.h"
#include "command_types.h"

struct WsCommand {
    uint32_t id = 0;
    ModuleId module = ModuleId::Unknown;
    CommandKind cmd = CommandKind::Enable;
    ActionId action = ActionId::None; // only when cmd == Action
    ActionParams params;             // validated params; size()==0 for no-param actions
    CommandError error{CommandError::InvalidCommand};
    // true when id + module were schema-valid. Independent of command validity,
    // so `{"id":2,"module":"ir","cmd":"action","action":""}` still correlates and
    // gets an invalid_command reply addressed to the right client.
    bool correlationValid = false;
};

// Strict parse of one complete inbound text frame. Invalid JSON, a missing/zero
// id, an unknown module, an unknown cmd or an empty action => correlationValid
// false or error InvalidCommand. A well-typed but non-allowlisted action =>
// UnsupportedAction with correlationValid true. A known action whose `params`
// fail the descriptor's ParamSpec validation => InvalidParams, still correlated;
// the `params` member must be an object (or omitted when every declared
// parameter is optional) — an explicit `null`, array or scalar is invalid.
// Returns id=0/module=Unknown when the frame cannot be correlated.
WsCommand parseWsCommand(std::string_view json);

// {"type":"command_result","id":17,"module":"wifi","ok":true,"error":""}
std::string commandResultToJson(uint32_t id, ModuleId module, CommandError error);

// Serial control channel (PLAN §2.4). When the WiFi payloads repurpose the
// device's AP, the dashboard HTTP/WS is shadowed and the operator still needs a
// way to Stop. This reads newline-delimited JSON commands over USB serial —
// exactly the WebSocket schema — and main() dispatches them through the same
// dispatchCommand() path, replying with the same command_result JSON. Plain
// `ap on` / `ap off` lines request the management AP through the same channel.
#pragma once

#include <cstdint>

#include "ws_command.h"

namespace serialConsole {

// Prints the one-line usage hint. Call once from setup() after Serial.begin().
void begin();

// Local (non-JSON) line commands: `ap on` explicitly requests the management
// AP, `ap off` requests it down. Admission (WiFi module busy, cleanup, radio
// lease) is decided by main(), not here.
enum class ApRequest : uint8_t { None, On, Off };

// Non-blocking: consumes available Serial bytes and, when a complete line has
// been read, returns true. A JSON command line is parsed into `out` with
// `apOut == None`; an `ap on`/`ap off` line sets `apOut` and leaves `out`
// untouched. At most one line per call so the loop stays bounded; oversized
// lines are dropped, not truncated.
bool nextCommand(WsCommand &out, ApRequest &apOut);

// Prints the command_result JSON line for a serial-originated command.
void reply(const WsCommand &command, CommandError error);

// Prints the human-readable outcome of an `ap on`/`ap off` request; `detail`
// explains a rejection and may be null/empty.
void replyApRequest(ApRequest request, bool ok, const char *detail);

} // namespace serialConsole

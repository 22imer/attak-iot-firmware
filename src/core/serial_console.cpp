#include "serial_console.h"

#include <Arduino.h>

#include <string>
#include <string_view>

namespace serialConsole {

namespace {

// Same ceiling as the WebSocket command frame; matches the parser's contract.
constexpr size_t kMaxLineBytes = 512;

char lineBuffer[kMaxLineBytes + 1];
size_t lineLength = 0;
bool dropping = false; // an over-long line is discarded up to the next newline

// Recognizes the plain-text AP request lines (leading/trailing blanks allowed);
// every other line is a JSON command.
ApRequest parseApRequest(std::string_view line) {
    size_t first = 0;
    size_t last = line.size();
    while (first < last && (line[first] == ' ' || line[first] == '\t')) ++first;
    while (last > first && (line[last - 1] == ' ' || line[last - 1] == '\t')) --last;
    const std::string_view text = line.substr(first, last - first);
    if (text == "ap on") return ApRequest::On;
    if (text == "ap off") return ApRequest::Off;
    return ApRequest::None;
}

} // namespace

void begin() {
    Serial.println();
    Serial.println("serial-console ready: gui 1 dong JSON nhu WebSocket, vi du "
                   "{\"id\":1,\"module\":\"wifi\",\"cmd\":\"disable\"}");
    Serial.println("lenh AP: 'ap on' / 'ap off'");
}

bool nextCommand(WsCommand &out, ApRequest &apOut) {
    apOut = ApRequest::None;
    while (Serial.available() > 0) {
        const int value = Serial.read();
        if (value < 0) break; // nothing more this iteration
        const char c = static_cast<char>(value);

        if (c == '\r') continue; // tolerant of CRLF terminals
        if (c == '\n') {
            if (dropping) {
                dropping = false;
                lineLength = 0;
                Serial.println("serial-console: dong qua dai (>512 byte), bo qua");
                continue;
            }
            if (lineLength == 0) continue;
            const std::string_view line(lineBuffer, lineLength);
            apOut = parseApRequest(line);
            if (apOut == ApRequest::None) out = parseWsCommand(line);
            lineLength = 0;
            return true; // one line per call keeps loop() bounded
        }

        if (dropping) continue;
        if (lineLength >= kMaxLineBytes) {
            dropping = true;
            lineLength = 0;
            continue;
        }
        lineBuffer[lineLength++] = c;
    }
    return false;
}

void reply(const WsCommand &command, CommandError error) {
    const std::string json = commandResultToJson(command.id, command.module, error);
    Serial.println(json.c_str());
}

void replyApRequest(ApRequest request, bool ok, const char *detail) {
    const char *what = request == ApRequest::On ? "on" : "off";
    if (ok) {
        Serial.printf("ap %s: ok\n", what);
    } else {
        Serial.printf("ap %s: rejected — %s\n", what, detail && *detail ? detail : "busy");
    }
}

} // namespace serialConsole

#include "web_dashboard.h"

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "ws_command.h"

namespace webDashboard {

namespace {

constexpr size_t kMaxCommandBytes = 512;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Guards queue_ and liveSessions_ only. Never held across parse, serialize, or
// a socket send (AsyncWebSocket::text takes its own client lock).
std::mutex stateMutex;
CommandQueue queue_;
std::map<uint32_t, uint64_t> liveSessions_;
uint64_t nextSessionToken = 1;

void sendResult(uint32_t clientId, const std::string &json) { ws.text(clientId, json.c_str(), json.size()); }

void replyInvalid(AsyncWebSocketClient *client) {
    sendResult(client->id(), commandResultToJson(0, ModuleId::Unknown, CommandError::InvalidCommand));
}

void handleData(AsyncWebSocketClient *client, void *arg, uint8_t *data, size_t len) {
    auto *info = static_cast<AwsFrameInfo *>(arg);
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT || len > kMaxCommandBytes) {
        replyInvalid(client);
        return;
    }

    const std::string_view view(reinterpret_cast<const char *>(data), len);
    const WsCommand command = parseWsCommand(view);

    if (!command.correlationValid) {
        replyInvalid(client);
        return;
    }
    if (command.error != CommandError::None) {
        sendResult(client->id(), commandResultToJson(command.id, command.module, command.error));
        return;
    }

    EnqueuedCommand request;
    request.clientId = client->id();
    request.command = command;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        const auto it = liveSessions_.find(client->id());
        if (it != liveSessions_.end()) request.sessionToken = it->second;
    }

    CommandError admitted;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        admitted = queue_.push(request);
    }
    if (admitted != CommandError::None) {
        sendResult(client->id(), commandResultToJson(command.id, command.module, admitted));
    }
}

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT: {
        std::lock_guard<std::mutex> lock(stateMutex);
        liveSessions_[client->id()] = nextSessionToken++;
        break;
    }
    case WS_EVT_DISCONNECT: {
        std::lock_guard<std::mutex> lock(stateMutex);
        liveSessions_.erase(client->id());
        break;
    }
    case WS_EVT_DATA:
        handleData(client, arg, data, len);
        break;
    default:
        break;
    }
}

} // namespace

void begin() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

bool nextCommand(EnqueuedCommand &request) {
    std::lock_guard<std::mutex> lock(stateMutex);
    return queue_.pop(request);
}

void reply(const EnqueuedCommand &request, CommandError error) {
    bool live = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        const auto it = liveSessions_.find(request.clientId);
        live = it != liveSessions_.end() && it->second == request.sessionToken;
    }
    if (!live) return; // client left or a different session reused the id
    sendResult(request.clientId, commandResultToJson(request.command.id, request.command.module, error));
}

void publishStatus(const ModuleStatus &status) {
    const std::string payload = statusToJson(status);
    ws.textAll(payload.c_str(), payload.size());
}

void loop() { ws.cleanupClients(); }

} // namespace webDashboard

#include "web_dashboard.h"

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

namespace webDashboard {

namespace {
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
CommandHandler commandHandler = nullptr;

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type != WS_EVT_DATA || !commandHandler) return;

    auto *info = static_cast<AwsFrameInfo *>(arg);
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

    std::string message(reinterpret_cast<char *>(data), len);
    WsCommand command = parseWsCommand(message);
    if (command.valid) commandHandler(command);
}
} // namespace

void begin() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void publishStatus(const ModuleStatus &status) {
    std::string payload = statusToJson(status);
    ws.textAll(payload.c_str());
}

void onCommand(CommandHandler handler) { commandHandler = handler; }

void loop() { ws.cleanupClients(); }

} // namespace webDashboard

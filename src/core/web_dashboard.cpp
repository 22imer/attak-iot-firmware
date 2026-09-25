#include "web_dashboard.h"

#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

namespace webDashboard {

namespace {
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType, void *, uint8_t *, size_t) {
    // No inbound dashboard commands in v1 — status is broadcast-only.
}
} // namespace

void begin() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();
}

void publishStatus(const ModuleStatus &status) {
    JsonDocument doc;
    doc["module"] = status.name;
    doc["connected"] = status.connected;
    doc["detail"] = status.detail;
    doc["lastUpdateMs"] = status.lastUpdateMs;

    String payload;
    serializeJson(doc, payload);
    ws.textAll(payload);
}

void loop() { ws.cleanupClients(); }

} // namespace webDashboard

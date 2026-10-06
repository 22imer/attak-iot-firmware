#!/usr/bin/env python3
"""Exercise the real wifi_ap backend against a deterministic radio double.

Run: python3 tools/test_wifi_ap_lifecycle.py
This checks lifecycle/rollback, not ESP32 RF or USB hardware acceptance.
"""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

WIFI_HEADER = r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
enum wifi_mode_t { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 };
struct Address {
    std::string toString() const { return "192.168.4.1"; }
};
struct Radio {
    wifi_mode_t currentMode = WIFI_OFF;
    std::string ssid;
    std::string password;
    uint8_t channel = 1;
    bool failNextAp = false;
    wifi_mode_t getMode() const { return currentMode; }
    bool mode(wifi_mode_t next) {
        currentMode = next;
        if (next == WIFI_OFF) { ssid.clear(); password.clear(); channel = 1; }
        return true;
    }
    bool softAP(const char *name, const char *key, int requestedChannel = 1) {
        currentMode = static_cast<wifi_mode_t>(currentMode | WIFI_AP);
        if (failNextAp) { failNextAp = false; return false; }
        ssid = name;
        password = key ? key : "";
        channel = static_cast<uint8_t>(requestedChannel);
        return true;
    }
    Address softAPIP() const { return {}; }
};
inline Radio WiFi;
struct Console {
    void println(const char *message) { std::puts(message); }
    template <class... Args> void printf(const char *format, Args... args) {
        std::printf(format, args...);
    }
};
inline Console Serial;
'''

ESP_HEADER = r'''
#pragma once
#include "WiFi.h"
constexpr int ESP_OK = 0;
enum wifi_second_chan_t { WIFI_SECOND_CHAN_NONE = 0 };
inline int esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *secondary) {
    if (WiFi.getMode() == WIFI_OFF) return -1;
    *primary = WiFi.channel;
    *secondary = WIFI_SECOND_CHAN_NONE;
    return 0;
}
inline int esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t) {
    if (WiFi.getMode() == WIFI_OFF) return -1;
    WiFi.channel = primary;
    return 0;
}
'''

CASES = r'''
#include <cassert>
#include <cstdio>
#include "WiFi.h"
#include "core/wifi_ap.h"

void reset(wifi_mode_t mode = WIFI_STA) {
    WiFi = Radio{};
    DeviceConfig config;
    config.apSsid = "LabControl";
    config.apPassword = "labpassword";
    wifiAp::begin(config);
    WiFi.mode(mode);
}

int main() {
    reset();
    assert(wifiAp::beginPortal("LabTwin", 6));
    assert(wifiAp::running());
    assert(!wifiAp::requested());
    // The portal is a lure: it must be joinable without a key, so the cloned
    // AP is open even though the management AP is password-protected.
    assert(WiFi.ssid == "LabTwin" && WiFi.password.empty());
    assert(WiFi.channel == 6);
    wifiAp::endPortal();
    assert(!wifiAp::running() && !wifiAp::requested());
    assert(WiFi.getMode() == WIFI_STA);
    std::puts("PASS USB STA -> twin AP -> STA, without persistent AP intent");

    reset(WIFI_OFF);
    assert(wifiAp::beginPortal("", 0));
    assert(WiFi.ssid == "LabControl" && WiFi.password.empty() && wifiAp::running());
    wifiAp::endPortal();
    assert(WiFi.getMode() == WIFI_OFF && !wifiAp::running());
    std::puts("PASS AP-off -> configured portal AP -> AP-off");

    reset();
    assert(wifiAp::requestOn());
    WiFi.channel = 11;
    assert(wifiAp::beginPortal("LabTwin", 0));
    // While the portal runs the AP is open even though the management AP has a
    // password; endPortal() must bring the real key back.
    assert(WiFi.ssid == "LabTwin" && WiFi.password.empty());
    wifiAp::endPortal();
    assert(WiFi.ssid == "LabControl" && WiFi.password == "labpassword");
    assert(WiFi.channel == 11 && wifiAp::running() && wifiAp::requested());
    assert(WiFi.getMode() == WIFI_AP_STA);
    std::puts("PASS portal AP open, then management AP password/channel/mode restored");

    reset();
    WiFi.failNextAp = true;
    assert(!wifiAp::beginPortal("LabTwin", 6));
    assert(WiFi.getMode() == WIFI_STA && !wifiAp::running() && !wifiAp::requested());
    assert(wifiAp::beginPortal("LabTwin", 6));
    wifiAp::endPortal();
    std::puts("PASS failed startup rolls back and next startup succeeds");

    reset();
    assert(wifiAp::requestOn());
    WiFi.channel = 9;
    WiFi.failNextAp = true;
    assert(!wifiAp::beginPortal("LabTwin", 6));
    assert(wifiAp::running() && wifiAp::requested());
    assert(WiFi.ssid == "LabControl" && WiFi.channel == 9);
    std::puts("PASS failed clone restores the existing AP");

    reset();
    assert(!wifiAp::beginPortal("bad\nssid", 6));
    assert(WiFi.getMode() == WIFI_STA && !wifiAp::requested());
    assert(wifiAp::beginPortal("LabTwin", 6));
    assert(!wifiAp::beginPortal("OtherTwin", 11));
    assert(WiFi.ssid == "LabTwin" && WiFi.channel == 6);
    wifiAp::endPortal();
    wifiAp::endPortal();
    assert(WiFi.getMode() == WIFI_STA);
    std::puts("PASS invalid/nested startup has no radio side effects; Stop is idempotent");

    reset();
    assert(wifiAp::requestOn());
    WiFi.channel = 11;
    assert(wifiAp::beginPortal("", 6));
    assert(WiFi.ssid == "LabControl" && WiFi.channel == 6);
    wifiAp::endPortal();
    assert(wifiAp::running() && wifiAp::requested() && WiFi.channel == 11);
    std::puts("PASS channel-only portal restores the existing AP channel");
}
'''

with tempfile.TemporaryDirectory(prefix="wifi-ap-lifecycle-") as directory:
    scratch = Path(directory)
    (scratch / "WiFi.h").write_text(WIFI_HEADER)
    (scratch / "esp_wifi.h").write_text(ESP_HEADER)
    (scratch / "cases.cpp").write_text(CASES)
    executable = scratch / "lifecycle"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-DENABLE_DISRUPTIVE",
        f"-I{scratch}", f"-I{ROOT / 'src'}", str(scratch / "cases.cpp"),
        str(ROOT / "src/core/wifi_ap.cpp"), str(ROOT / "src/core/evil_twin.cpp"),
        "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)

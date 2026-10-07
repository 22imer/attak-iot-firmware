#include "action_catalog.h"
#include "nrf_spectrum.h"
#include "wifi_sniff.h"

namespace catalog {

namespace {

template <typename T, size_t N> constexpr size_t countOf(const T (&)[N]) { return N; }

constexpr ParamSpec kRfScanParams[] = {
    {"startMhz", "Start MHz", ParamType::Number, true, 300, 928, 0},
    {"endMhz", "End MHz", ParamType::Number, true, 300, 928, 0},
    {"stepKhz", "Step kHz", ParamType::Integer, true, 10, 5000, 0},
};
constexpr ParamSpec kRfRecordParams[] = {
    {"freqMhz", "Frequency MHz", ParamType::Number, false, 300, 928, 0},
    {"windowMs", "Window ms", ParamType::Integer, false, 200, 30000, 0},
};
constexpr ParamSpec kRfReplayParams[] = {
    {"repeat", "Repeat", ParamType::Integer, false, 1, 10, 0},
    {"gapMs", "Gap ms", ParamType::Integer, false, 20, 2000, 0},
};
constexpr ParamSpec kRfSpectrumParams[] = {
    {"startMhz", "Start MHz", ParamType::Number, false, 300, 928, 0},
    {"endMhz", "End MHz", ParamType::Number, false, 300, 928, 0},
    {"stepKhz", "Step kHz", ParamType::Integer, false, 25, 5000, 0},
};
constexpr ParamSpec kRfCustomTxParams[] = {
    {"freqMhz", "Frequency MHz", ParamType::Number, true, 300, 928, 0},
    {"payloadHex", "Payload (hex)", ParamType::String, true, 0, 0, 64},
    {"repeat", "Repeat", ParamType::Integer, false, 1, 10, 0},
};
#ifdef ENABLE_DISRUPTIVE
constexpr ParamSpec kRfJammerParams[] = {
    {"freqMhz", "Frequency MHz", ParamType::Number, false, 300, 928, 0},
    {"mode", "Mode (full|intermittent)", ParamType::String, false, 0, 0, 12},
    {"onMs", "Carrier on ms", ParamType::Integer, false, 10, 5000, 0},
    {"offMs", "Carrier off ms", ParamType::Integer, false, 10, 5000, 0},
};
#endif
constexpr ActionDescriptor kCc1101[] = {
    {"rf_scan", "Quét RF", ActionId::RfScan, ActionKind::OneShot, LegalTier::Observe, false, false, kRfScanParams, countOf(kRfScanParams)},
    {"rf_record", "Ghi tín hiệu RF", ActionId::RfRecord, ActionKind::Record, LegalTier::Observe, false, false, kRfRecordParams, countOf(kRfRecordParams)},
    {"rf_replay", "Phát lại RF", ActionId::RfReplay, ActionKind::Replay, LegalTier::ActiveOwn, false, true, kRfReplayParams, countOf(kRfReplayParams)},
    {"rf_spectrum", "Phổ RF", ActionId::RfSpectrum, ActionKind::Continuous, LegalTier::Observe, false, false, kRfSpectrumParams, countOf(kRfSpectrumParams)},
    {"rf_custom_tx", "Phát RF tùy chỉnh", ActionId::RfCustomTx, ActionKind::OneShot, LegalTier::ActiveOwn, false, false, kRfCustomTxParams, countOf(kRfCustomTxParams)},
#ifdef ENABLE_DISRUPTIVE
    {"rf_jammer", "Gây nhiễu RF", ActionId::RfJammer, ActionKind::Continuous, LegalTier::Disruptive, false, false,
     kRfJammerParams, countOf(kRfJammerParams)},
#endif
};
#ifdef ENABLE_DISRUPTIVE
constexpr ParamSpec kNrfJammerParams[] = {
    {"startChannel", "Kênh bắt đầu", ParamType::Integer, false, 0, 125, 0},
    {"endChannel", "Kênh kết thúc", ParamType::Integer, false, 0, 125, 0},
    {"dwellMs", "Dwell ms", ParamType::Integer, false, 20, 5000, 0},
};
#endif
constexpr ActionDescriptor kNrf[] = {
    {"nrf_scan", "Quét 2.4GHz", ActionId::NrfScan, ActionKind::Continuous, LegalTier::Observe, false, false,
     nrfSpectrum::kScanParams, nrfSpectrum::kScanParamCount},
#ifdef ENABLE_DISRUPTIVE
    {"nrf_jammer", "Gây nhiễu 2.4GHz", ActionId::NrfJammer, ActionKind::Continuous, LegalTier::Disruptive, false,
     false, kNrfJammerParams, countOf(kNrfJammerParams)},
#endif
};
#ifdef ENABLE_DISRUPTIVE
constexpr ParamSpec kWifiBeaconParams[] = {
    {"ssid", "SSID cố định", ParamType::String, false, 0, 0, 32},
    {"intervalMs", "Chu kỳ ms", ParamType::Integer, false, 20, 5000, 0},
};
constexpr ParamSpec kWifiDeauthParams[] = {
    {"mode", "Mode (target|flood)", ParamType::String, false, 0, 0, 8},
    {"bssid", "BSSID đích", ParamType::String, false, 0, 0, 17},
    {"client", "Client MAC", ParamType::String, false, 0, 0, 17},
    {"reason", "Reason code", ParamType::Integer, false, 1, 65535, 0},
    {"intervalMs", "Chu kỳ ms", ParamType::Integer, false, 20, 5000, 0},
};
constexpr ParamSpec kWifiEvilPortalParams[] = {
    {"ssid", "SSID clone (evil twin)", ParamType::String, false, 0, 0, 32},
    {"channel", "Kênh clone", ParamType::Integer, false, 1, 13, 0},
    {"deauth", "Đuổi client khỏi AP đích", ParamType::Boolean, false, 0, 0, 0},
    {"bssid", "BSSID AP đích", ParamType::String, false, 0, 0, 17},
    {"client", "Client MAC (bỏ trống = mọi client)", ParamType::String, false, 0, 0, 17},
    {"reason", "Reason code deauth", ParamType::Integer, false, 1, 65535, 0},
    {"intervalMs", "Chu kỳ deauth ms", ParamType::Integer, false, 20, 5000, 0},
};
// Evil-twin scenario orchestrator: clone AP + portal + targeted deauth as one
// preset, all three targets required. Param order fixes the indices read in
// wifi_module.cpp startAttack(WifiEvilTwin): 0 ssid, 1 bssid, 2 channel,
// 3 reason, 4 intervalMs.
constexpr ParamSpec kWifiEvilTwinParams[] = {
    {"ssid", "SSID nạn nhân", ParamType::String, true, 0, 0, 32},
    {"bssid", "BSSID đích", ParamType::String, true, 0, 0, 17},
    {"channel", "Kênh", ParamType::Integer, true, 1, 13, 0},
    {"reason", "Reason code", ParamType::Integer, false, 1, 65535, 0},
    {"intervalMs", "Chu kỳ deauth ms", ParamType::Integer, false, 20, 5000, 0},
};
#endif
constexpr ActionDescriptor kWifi[] = {
    {"scan", "Quét WiFi", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe, false, false},
    {"wifi_sniff", "Bắt gói WiFi", ActionId::WifiSniff, ActionKind::Continuous, LegalTier::Observe, true, false,
     wifiSniff::kSniffParams, wifiSniff::kSniffParamCount},
#ifdef ENABLE_DISRUPTIVE
    {"wifi_beacon", "Beacon spam", ActionId::WifiBeacon, ActionKind::Continuous, LegalTier::Disruptive, false, false,
     kWifiBeaconParams, countOf(kWifiBeaconParams)},
    {"wifi_deauth", "Deauth WiFi", ActionId::WifiDeauth, ActionKind::Continuous, LegalTier::Disruptive, false, false,
     kWifiDeauthParams, countOf(kWifiDeauthParams)},
    {"wifi_evil_portal", "Evil portal", ActionId::WifiEvilPortal, ActionKind::Continuous, LegalTier::Disruptive, false,
     false, kWifiEvilPortalParams, countOf(kWifiEvilPortalParams)},
    {"wifi_evil_twin", "Evil twin (clone+portal+deauth)", ActionId::WifiEvilTwin, ActionKind::Continuous,
     LegalTier::Disruptive, false, false, kWifiEvilTwinParams, countOf(kWifiEvilTwinParams)},
#endif
};
constexpr ParamSpec kNfcWriteNdefParams[] = {
    {"text", "Nội dung NDEF", ParamType::String, true, 0, 0, 64},
};
constexpr ActionDescriptor kPn532[] = {
    {"read_uid", "Đọc UID", ActionId::ReadUid, ActionKind::OneShot, LegalTier::Observe, false, false},
    {"nfc_read_dump", "Đọc NFC dump", ActionId::NfcReadDump, ActionKind::Record, LegalTier::Observe, false, false},
    {"nfc_clone_uid", "Clone UID", ActionId::NfcCloneUid, ActionKind::OneShot, LegalTier::ActiveOwn, false, true},
    {"nfc_write_ndef", "Ghi NDEF", ActionId::NfcWriteNdef, ActionKind::OneShot, LegalTier::ActiveOwn, false, false,
     kNfcWriteNdefParams, countOf(kNfcWriteNdefParams)},
    {"nfc_erase", "Xóa NFC", ActionId::NfcErase, ActionKind::OneShot, LegalTier::ActiveOwn, false, false},
};
constexpr ParamSpec kIrCustomTxParams[] = {
    {"protocol", "Giao thức", ParamType::String, true, 0, 0, 0},
    {"code", "Mã (hex)", ParamType::String, false, 0, 0, 18},
    {"bits", "Số bit", ParamType::Integer, false, 1, 64, 0},
    {"frequency", "Tần số (Hz)", ParamType::Integer, false, 30000, 60000, 0},
    {"raw", "Raw timings (us)", ParamType::String, false, 0, 0, 64},
};
constexpr ActionDescriptor kIr[] = {
    {"capture", "Capture", ActionId::Capture, ActionKind::Record, LegalTier::Observe, false, false},
    {"ir_replay", "Phát lại IR", ActionId::IrReplay, ActionKind::Replay, LegalTier::ActiveOwn, false, true},
    {"ir_tvbgone", "TV-B-Gone", ActionId::IrTvbgone, ActionKind::OneShot, LegalTier::ActiveOwn, false, false},
    {"ir_custom_tx", "Phát IR tùy chỉnh", ActionId::IrCustomTx, ActionKind::OneShot, LegalTier::ActiveOwn, false, false,
     kIrCustomTxParams, countOf(kIrCustomTxParams)},
};

} // namespace

const ActionDescriptor *moduleActions(ModuleId module, size_t &count) {
    switch (module) {
    case ModuleId::Wifi: count = countOf(kWifi); return kWifi;
    case ModuleId::Pn532: count = countOf(kPn532); return kPn532;
    case ModuleId::Ir: count = countOf(kIr); return kIr;
    case ModuleId::Cc1101: count = countOf(kCc1101); return kCc1101;
    case ModuleId::Nrf24: count = countOf(kNrf); return kNrf;
    case ModuleId::Unknown:
    default: count = 0; return nullptr;
    }
}

const ActionDescriptor *findAction(ModuleId module, std::string_view id) {
    size_t count = 0;
    const ActionDescriptor *actions = moduleActions(module, count);
    for (size_t i = 0; i < count; ++i) {
        if (id == actions[i].id) return &actions[i];
    }
    return nullptr;
}

const ActionDescriptor *findAction(ModuleId module, ActionId action) {
    size_t count = 0;
    const ActionDescriptor *actions = moduleActions(module, count);
    for (size_t i = 0; i < count; ++i) {
        if (actions[i].action == action) return &actions[i];
    }
    return nullptr;
}

const char *actionKindName(ActionKind kind) {
    switch (kind) {
    case ActionKind::OneShot: return "oneshot";
    case ActionKind::Continuous: return "continuous";
    case ActionKind::Record: return "record";
    case ActionKind::Replay: return "replay";
    }
    return "oneshot";
}

const char *legalTierName(LegalTier tier) {
    switch (tier) {
    case LegalTier::Observe: return "observe";
    case LegalTier::ActiveOwn: return "active_own";
    case LegalTier::Disruptive: return "disruptive";
    }
    return "observe";
}

const char *paramTypeName(ParamType type) {
    switch (type) {
    case ParamType::Integer: return "integer";
    case ParamType::Number: return "number";
    case ParamType::Boolean: return "boolean";
    case ParamType::String: return "string";
    }
    return "string";
}

} // namespace catalog

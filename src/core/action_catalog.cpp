#include "action_catalog.h"

namespace catalog {

namespace {

// Phase A: only the three actions already shipped are migrated into the catalog.
// cc1101 and nrf24 expose none (health-check only), so they get no table.
constexpr ActionDescriptor kWifi[] = {
    {"scan", "Quét WiFi", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe, false, false},
};
constexpr ActionDescriptor kPn532[] = {
    {"read_uid", "Đọc UID", ActionId::ReadUid, ActionKind::OneShot, LegalTier::Observe, false, false},
};
constexpr ActionDescriptor kIr[] = {
    {"capture", "Capture", ActionId::Capture, ActionKind::Record, LegalTier::Observe, false, false},
};

template <size_t N> constexpr size_t countOf(const ActionDescriptor (&)[N]) { return N; }

} // namespace

const ActionDescriptor *moduleActions(ModuleId module, size_t &count) {
    switch (module) {
    case ModuleId::Wifi: count = countOf(kWifi); return kWifi;
    case ModuleId::Pn532: count = countOf(kPn532); return kPn532;
    case ModuleId::Ir: count = countOf(kIr); return kIr;
    case ModuleId::Cc1101:
    case ModuleId::Nrf24:
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

} // namespace catalog

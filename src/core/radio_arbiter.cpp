#include "core/radio_arbiter.h"

namespace radio {

bool RadioArbiter::deadlineReached(std::uint32_t now, std::uint32_t deadline) {
    // Wrap-safe: the signed difference is non-negative once `deadline` has
    // passed, even across the uint32_t millisecond wrap.
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

bool RadioArbiter::tryAcquire(Owner owner, RadioResource resource, std::uint32_t now) {
    if (owner == kNoOwner) return false;

    switch (resource) {
    case RadioResource::None:
        // "No exclusive resource" needs no lease; reported as granted.
        return true;
    case RadioResource::SharedSpi:
        if (spiOwner_ != kNoOwner) return false; // single owner, no deadline change
        spiOwner_ = owner;
        return true;
    case RadioResource::WifiExclusive:
        if (wifiOwner_ != kNoOwner || wifiPhase_ != WifiPhase::Idle) return false;
        wifiOwner_ = owner;
        wifiPhase_ = WifiPhase::AwaitSuspend;
        wifiDeadline_ = now + kMaxWifiExclusiveMs;
        suspendDueAt_ = now + kWifiSuspendGraceMs;
        restoreAttempts_ = 0;
        return true;
    }
    return false;
}

bool RadioArbiter::release(Owner owner) {
    if (owner == kNoOwner) return false;

    bool released = false;
    if (spiOwner_ == owner) {
        spiOwner_ = kNoOwner;
        released = true;
    }
    if (wifiOwner_ == owner) {
        wifiOwner_ = kNoOwner;
        wifiDeadline_ = 0;
        wifiPhase_ = WifiPhase::Restoring; // cleanup done; AP must come back
        released = true;
    }
    return released;
}

Owner RadioArbiter::ownerOf(RadioResource resource) const {
    switch (resource) {
    case RadioResource::SharedSpi:
        return spiOwner_;
    case RadioResource::WifiExclusive:
        return wifiOwner_;
    case RadioResource::None:
    default:
        return kNoOwner;
    }
}

bool RadioArbiter::holds(Owner owner, RadioResource resource) const {
    return owner != kNoOwner && ownerOf(resource) == owner;
}

bool RadioArbiter::spiBlockedFor(Owner owner) const {
    return spiOwner_ != kNoOwner && spiOwner_ != owner;
}

WifiPhase RadioArbiter::wifiPhase() const { return wifiPhase_; }

bool RadioArbiter::canAcquireWifi() const {
    return wifiOwner_ == kNoOwner && wifiPhase_ == WifiPhase::Idle;
}

bool RadioArbiter::suspendDue(std::uint32_t now) const {
    return wifiPhase_ == WifiPhase::AwaitSuspend && deadlineReached(now, suspendDueAt_);
}

bool RadioArbiter::markSuspended() {
    if (wifiPhase_ != WifiPhase::AwaitSuspend) return false;
    wifiPhase_ = WifiPhase::Active;
    return true;
}

Owner RadioArbiter::expiredOwner(std::uint32_t now) const {
    if (wifiOwner_ == kNoOwner) return kNoOwner;
    if (wifiPhase_ != WifiPhase::AwaitSuspend && wifiPhase_ != WifiPhase::Active) {
        return kNoOwner;
    }
    return deadlineReached(now, wifiDeadline_) ? wifiOwner_ : kNoOwner;
}

std::uint32_t RadioArbiter::wifiDeadline() const { return wifiDeadline_; }

bool RadioArbiter::markRestoring() {
    if (wifiOwner_ == kNoOwner && wifiPhase_ == WifiPhase::Idle) return false;
    wifiOwner_ = kNoOwner;
    wifiDeadline_ = 0;
    wifiPhase_ = WifiPhase::Restoring;
    return true;
}

bool RadioArbiter::restorationComplete() {
    if (wifiPhase_ != WifiPhase::Restoring) return false;
    wifiPhase_ = WifiPhase::Idle;
    restoreAttempts_ = 0;
    return true;
}

bool RadioArbiter::restorationError() {
    if (wifiPhase_ != WifiPhase::Restoring) return false;
    ++restoreAttempts_;
    return true;
}

bool RadioArbiter::restorePending() const { return wifiPhase_ == WifiPhase::Restoring; }

std::uint32_t RadioArbiter::restoreAttempts() const { return restoreAttempts_; }

} // namespace radio

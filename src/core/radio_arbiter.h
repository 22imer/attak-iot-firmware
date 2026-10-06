#pragma once

#include <cstdint>

// Portable radio-resource arbiter (no Arduino/hardware includes). The loop owns
// one RadioArbiter instance; callers pass an injected millisecond clock so the
// state machine is testable on the native build.
//
// Invariants (see PLAN.md F4 / local://attak-phase1-contract.md):
//  - Independent resources (SharedSpi, WifiExclusive) may be held concurrently,
//    but each resource has at most one owner.
//  - A duplicate acquisition never succeeds and never extends a deadline.
//  - WifiExclusive gets a server-owned hard maximum (kMaxWifiExclusiveMs); the
//    deadline is wrap-safe and is NOT extended by status/probe traffic.
//  - AP suspension happens only after kWifiSuspendGraceMs, giving the loop time
//    to enqueue the warning notice/ack before the dashboard link drops.
//  - A resource is released only after the owner's runtime/backend cleanup; the
//    SharedSpi lease has no forced deadline (it lasts to real cleanup).
//  - While a WifiExclusive run is Restoring, no action may acquire WiFi.

namespace radio {

enum class RadioResource : std::uint8_t { None, SharedSpi, WifiExclusive };

// WifiExclusive lifecycle. Idle means the AP is up and the resource is free.
enum class WifiPhase : std::uint8_t { Idle, AwaitSuspend, Active, Restoring };

// Caller-supplied identity for whoever holds a lease (a runtime/ticket token).
// kNoOwner is the empty value and is never a valid acquisition owner.
using Owner = std::uint32_t;
inline constexpr Owner kNoOwner = 0;

// Server-owned safety limits chosen under the software-first instruction
// (PLAN option a): a disconnected run cannot exceed 30 s, and the AP is only
// torn down after a 250 ms window for the ack/notice to reach the client.
inline constexpr std::uint32_t kMaxWifiExclusiveMs = 30000;
inline constexpr std::uint32_t kWifiSuspendGraceMs = 250;

class RadioArbiter {
public:
    // --- acquisition / release ---------------------------------------------
    // Grants `resource` to `owner` if free. On WifiExclusive success it also
    // arms the grace window and the hard deadline from `now`. Returns false for
    // kNoOwner, for an already-owned resource (regardless of owner), and for
    // WiFi while a restoration is pending.
    bool tryAcquire(Owner owner, RadioResource resource, std::uint32_t now);

    // Releases every resource held by `owner` (call only after that owner's
    // cleanup). Releasing WifiExclusive enters Restoring: the AP is down and
    // must be brought back via restorationComplete()/restorationError().
    // Returns false when `owner` holds nothing, leaving state untouched.
    bool release(Owner owner);

    // --- inspection ---------------------------------------------------------
    Owner ownerOf(RadioResource resource) const;
    bool holds(Owner owner, RadioResource resource) const;
    // True when another owner currently holds SharedSpi (skip SPI polling/enable).
    bool spiBlockedFor(Owner owner) const;

    // --- WiFi lifecycle -----------------------------------------------------
    WifiPhase wifiPhase() const;
    bool canAcquireWifi() const;
    // Grace elapsed: the loop should now call wifiAp::suspend().
    bool suspendDue(std::uint32_t now) const;
    // AwaitSuspend -> Active, after wifiAp::suspend() succeeded.
    bool markSuspended();
    // WiFi owner whose hard deadline has passed (kNoOwner when none).
    Owner expiredOwner(std::uint32_t now) const;
    std::uint32_t wifiDeadline() const;
    // Idempotent signal that cleanup is done and the AP must be restored
    // (clears any WiFi owner, discards the deadline, enters Restoring).
    bool markRestoring();
    // Restoring -> Idle, after wifiAp::restore() succeeded.
    bool restorationComplete();
    // Records a failed restore attempt; stays Restoring so nothing else can
    // take the radio until the loop retries on its bounded cadence.
    bool restorationError();
    bool restorePending() const;
    std::uint32_t restoreAttempts() const;

private:
    static bool deadlineReached(std::uint32_t now, std::uint32_t deadline);

    Owner spiOwner_ = kNoOwner;
    Owner wifiOwner_ = kNoOwner;
    WifiPhase wifiPhase_ = WifiPhase::Idle;
    std::uint32_t wifiDeadline_ = 0;
    std::uint32_t suspendDueAt_ = 0;
    std::uint32_t restoreAttempts_ = 0;
};

} // namespace radio

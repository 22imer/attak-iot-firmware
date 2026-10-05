// Pure per-module lifecycle/result state machine shared by every module.
// Owns enabled/health/action transitions, generation (epoch) guards for late
// completions, the retained previous payload and the cleanup flag. Portable:
// every clock value is injected, so this compiles and is tested on native.
#pragma once

#include <cstdint>
#include <string>

#include "command_types.h"
#include "module_status.h"

class ModuleRuntime {
  public:
    explicit ModuleRuntime(const char *name);

    const ModuleStatus &status() const; // reference — no payload copy per poll
    uint32_t revision() const;          // changes when an observable field changes

    // Enable is idempotent (no re-init, no action/payload reset). Disable
    // invalidates the running ticket, clears the payload/time, keeps the
    // success sequence, and ORs cleanupRequired into any existing flag.
    // Enable never clears cleanupPending.
    void setEnabled(bool enabled, uint32_t now, bool cleanupRequired = false);

    // Health/detail only. A module losing its chip MUST failAction() first.
    void setHealth(bool connected, const std::string &detail, uint32_t now);

    // Admission gate. Precedence: off -> running/cleanup -> health. On success
    // issues a new ticket, records the injected clock/deadline, keeps the old
    // output.
    CommandError beginAction(uint32_t now, uint32_t deadlineMs, uint32_t &ticket);

    // Only the current ticket while Running; success moves the payload,
    // increments the sequence (even for identical bytes) and stamps the time.
    bool completeAction(uint32_t ticket, std::string payload, uint32_t now);

    // Same guard; preserves output/sequence/time, invalidates the ticket and
    // maps timeout codes to Timeout, other errors to Error.
    bool failAction(uint32_t ticket, ActionError error, uint32_t now, bool cleanupRequired);

    // Deadline-gated (wrap-safe): true only while Running and the stored
    // deadline has elapsed.
    bool expire(uint32_t now, ActionError timeoutError, bool cleanupRequired);

    // Only after the backend really released its resources.
    void finishCleanup(uint32_t now);

  private:
    ModuleStatus current_;
    uint32_t epoch_ = 0;      // generation; bumped by start/disable/fail/expire
    uint32_t startedMs_ = 0;  // uptime when the running action started
    uint32_t deadlineMs_ = 0; // duration, compared with wrap-safe subtraction
    uint32_t revision_ = 0;   // observable-change counter
};

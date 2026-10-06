// Pure per-module lifecycle/result state machine shared by every module.
// Owns enabled/health/action transitions, generation (epoch) guards for late
// completions, the retained previous payload, the RecordBuffer one-record
// retention and the single pending stream frame. Portable: every clock value is
// injected, so this compiles and is tested on native.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "action_catalog.h"
#include "command_types.h"
#include "module_status.h"
#include "record_buffer.h"

// The one admission gate used by both main() (WebSocket path) and each module's
// handleAction(). Precedence is fixed and shared: off -> running/cleanup ->
// health -> missing buffer. `needsBuffer` comes from the action descriptor.
CommandError actionAdmission(const ModuleStatus &status, bool needsBuffer);

// Payload ceiling and minimum cadence for publishOutput() and the dashboard.
constexpr size_t kMaxActionOutputBytes = 1024;
constexpr uint32_t kMinActionOutputIntervalMs = 100;

class ModuleRuntime {
  public:
    explicit ModuleRuntime(const char *name);

    const ModuleStatus &status() const; // reference — no payload copy per poll
    uint32_t revision() const;          // changes when an observable field changes

    // Enable is idempotent (no re-init, no action/payload reset). Disable
    // invalidates the running ticket, drops the pending stream frame, clears the
    // RecordBuffer/hasBuffer and the payload/time, keeps the success sequence,
    // and ORs cleanupRequired into any existing flag. Enable never clears
    // cleanupPending.
    void setEnabled(bool enabled, uint32_t now, bool cleanupRequired = false);

    // Health/detail only. A module losing its chip MUST failAction() first.
    void setHealth(bool connected, const std::string &detail, uint32_t now);

    // Descriptor-driven admission: descriptor.needsBuffer joins the gate and
    // descriptor.kind selects the deadline rule — Continuous with deadlineMs == 0
    // never expires, every other kind keeps today's wrap-safe finite deadline.
    // On success issues a new ticket, records the clock/deadline/kind, clears any
    // pending stream frame, and keeps the old output.
    CommandError beginAction(uint32_t now, uint32_t deadlineMs, uint32_t &ticket, const ActionDescriptor &descriptor);

    // Only the current ticket while Running; success moves the payload,
    // increments the sequence (even for identical bytes) and stamps the time.
    // Ends the stream generation: activeAction/ticket reset, pending frame dropped.
    bool completeAction(uint32_t ticket, std::string payload, uint32_t now);

    // Record actions only: atomically replaces the retained byte buffer together
    // with the payload/result. A rejected buffer (null/zero/overflow) leaves the
    // previous record and result untouched; a stale ticket/wrong kind is ignored.
    bool completeRecord(uint32_t ticket, std::string payload, const uint8_t *data, size_t length, uint32_t now);

    // Same guard; preserves output/sequence/time and the retained record,
    // invalidates the ticket and maps timeout codes to Timeout, other errors to
    // Error.
    bool failAction(uint32_t ticket, ActionError error, uint32_t now, bool cleanupRequired);

    // Deadline-gated (wrap-safe): true only while Running and the stored
    // deadline has elapsed. A Continuous action started with deadline 0 has no
    // deadline and never expires.
    bool expire(uint32_t now, ActionError timeoutError, bool cleanupRequired);

    // Only after the backend really released its resources.
    void finishCleanup(uint32_t now);

    // Continuous streams only: current Running ticket, payload <= 1024 bytes,
    // at most one sample per 100 ms (the first sample is always accepted), one
    // retained pending frame that the next accepted sample replaces. Never marks
    // the action Succeeded and never touches the retained result.
    bool publishOutput(uint32_t ticket, std::string payload, uint32_t now);

    // Moves the pending frame out (the dashboard broadcast path owns it then).
    bool takeActionOutput(ActionOutput &output);

    const RecordBuffer &recordBuffer() const;

  private:
    ModuleStatus current_;
    RecordBuffer record_;
    ActionOutput pending_;
    bool pendingValid_ = false;
    ActionKind activeKind_ = ActionKind::OneShot;
    uint32_t epoch_ = 0;          // generation; bumped by start/disable/end
    uint32_t startedMs_ = 0;      // uptime when the running action started
    uint32_t deadlineMs_ = 0;     // duration, compared with wrap-safe subtraction
    uint32_t outputSequence_ = 0; // monotonic per ticket, reset on a new generation
    uint32_t lastOutputMs_ = 0;   // uptime of the last accepted stream sample
    bool outputSampled_ = false;  // false until the first sample of this generation
    uint32_t revision_ = 0;       // observable-change counter
};

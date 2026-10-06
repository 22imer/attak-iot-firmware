// Portable timing plans for the disruptive RF jammers (PLAN §2.4: rf_jammer /
// nrf_jammer). Every clock is injected, so the state machines compile and are
// tested on native; the firmware backends only drive the carrier/channel from
// the outputs of these steps.
#pragma once

#include <cstdint>

namespace jamPlan {

// Channel hopper for the nRF24 constant-carrier jammer: a bounded cycle over
// [first, last] with a per-channel dwell. Wrap-safe (injected millis).
class ChannelHopper {
  public:
    void begin(uint8_t first, uint8_t last, uint32_t dwellMs, uint32_t nowMs);

    uint8_t channel() const { return current_; }
    uint8_t first() const { return first_; }
    uint8_t last() const { return last_; }
    uint32_t dwellMs() const { return dwellMs_; }
    uint32_t hops() const { return hops_; }
    bool spansMultiple() const { return first_ != last_; }

    // True once the current channel has been held for dwellMs (wrap-safe).
    bool due(uint32_t nowMs) const;

    // Moves to the next channel in [first_, last_] (wrapping) and re-arms the
    // dwell from `nowMs`. No-op timing-wise when only one channel is selected.
    void advance(uint32_t nowMs);

  private:
    uint8_t first_ = 0;
    uint8_t last_ = 0;
    uint8_t current_ = 0;
    uint32_t dwellMs_ = 100;
    uint32_t deadlineMs_ = 0;
    uint32_t hops_ = 0;
};

// Duty gate for the intermittent RF carrier: the carrier starts ON and toggles
// between onMs and offMs. Wrap-safe.
class DutyGate {
  public:
    void begin(uint32_t onMs, uint32_t offMs, uint32_t nowMs);

    bool carrierOn() const { return on_; }
    uint32_t toggles() const { return toggles_; }

    // True when the gate state changed since the previous call.
    bool update(uint32_t nowMs);

  private:
    uint32_t onMs_ = 100;
    uint32_t offMs_ = 100;
    uint32_t deadlineMs_ = 0;
    uint32_t toggles_ = 0;
    bool on_ = true;
};

} // namespace jamPlan

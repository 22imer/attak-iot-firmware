#include "jam_plan.h"

namespace jamPlan {

namespace {

bool elapsed(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

} // namespace

void ChannelHopper::begin(std::uint8_t first, std::uint8_t last, std::uint32_t dwellMs, std::uint32_t nowMs) {
    if (first > last) {
        const std::uint8_t swap = first;
        first = last;
        last = swap;
    }
    first_ = first;
    last_ = last;
    current_ = first;
    dwellMs_ = dwellMs;
    deadlineMs_ = nowMs + dwellMs;
    hops_ = 0;
}

bool ChannelHopper::due(std::uint32_t nowMs) const { return elapsed(nowMs, deadlineMs_); }

void ChannelHopper::advance(std::uint32_t nowMs) {
    deadlineMs_ = nowMs + dwellMs_;
    if (first_ == last_) return; // single channel: re-arm the dwell only
    current_ = (current_ >= last_) ? first_ : static_cast<std::uint8_t>(current_ + 1);
    ++hops_;
}

void DutyGate::begin(std::uint32_t onMs, std::uint32_t offMs, std::uint32_t nowMs) {
    onMs_ = onMs;
    offMs_ = offMs;
    on_ = true;
    deadlineMs_ = nowMs + onMs;
    toggles_ = 0;
}

bool DutyGate::update(std::uint32_t nowMs) {
    if (!elapsed(nowMs, deadlineMs_)) return false;
    on_ = !on_;
    ++toggles_;
    deadlineMs_ = nowMs + (on_ ? onMs_ : offMs_);
    return true;
}

} // namespace jamPlan

#include "core/nrf_spectrum.h"

#include <cstdarg>
#include <cstdio>

namespace nrfSpectrum {

namespace {

constexpr size_t kJsonCapacity = 1024;

bool appendFmt(char *buffer, size_t capacity, size_t &used, const char *format, ...) {
    if (used >= capacity) return false;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(buffer + used, capacity - used, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity - used) return false;
    used += static_cast<size_t>(written);
    return true;
}

} // namespace

uint32_t clampDwellUs(int64_t value) {
    if (value < static_cast<int64_t>(kMinDwellUs)) return kMinDwellUs;
    if (value > static_cast<int64_t>(kMaxDwellUs)) return kMaxDwellUs;
    return static_cast<uint32_t>(value);
}

void Sweeper::begin(uint32_t nowUs, uint8_t first, uint8_t last, uint32_t dwellUs) {
    if (first > last) {
        const uint8_t swap = first;
        first = last;
        last = swap;
    }
    if (first > kChannelLast) first = kChannelLast;
    if (last > kChannelLast) last = kChannelLast;
    first_ = first;
    last_ = last;
    dwellUs_ = clampDwellUs(static_cast<int64_t>(dwellUs));
    channel_ = first_;
    sweeps_ = 0;
    for (size_t i = 0; i < kChannelCount; ++i) hits_[i] = 0;
    armSettle(nowUs);
}

bool Sweeper::ready(uint32_t nowUs) const { return static_cast<int32_t>(nowUs - nextProbeUs_) >= 0; }

void Sweeper::armSettle(uint32_t nowUs) { nextProbeUs_ = nowUs + dwellUs_; }

uint16_t Sweeper::hits(uint8_t channel) const {
    return channel < kChannelCount ? hits_[channel] : 0;
}

bool Sweeper::submit(bool rpd, uint32_t nowUs) {
    if (rpd && hits_[channel_] < 0xFFFF) ++hits_[channel_];

    bool swept = false;
    if (channel_ >= last_) {
        channel_ = first_;
        ++sweeps_;
        swept = true;
    } else {
        ++channel_;
    }
    nextProbeUs_ = nowUs + dwellUs_;
    return swept;
}

bool formatSpectrumJson(const Sweeper &sweeper, std::string &out, size_t maxBytes) {
    if (maxBytes > kJsonCapacity) maxBytes = kJsonCapacity;
    if (maxBytes < 64) return false;

    char buffer[kJsonCapacity];
    size_t used = 0;
    if (!appendFmt(buffer, maxBytes, used,
                   "{\"kind\":\"nrf_spectrum\",\"sweeps\":%lu,\"first\":%u,\"last\":%u,\"dwellUs\":%lu,\"hits\":[",
                   static_cast<unsigned long>(sweeper.sweeps()), static_cast<unsigned>(sweeper.first()),
                   static_cast<unsigned>(sweeper.last()), static_cast<unsigned long>(sweeper.dwellUs()))) {
        return false;
    }
    for (uint8_t channel = kChannelFirst; channel <= kChannelLast; ++channel) {
        if (!appendFmt(buffer, maxBytes, used, channel == kChannelFirst ? "%u" : ",%u",
                       static_cast<unsigned>(sweeper.hits(channel)))) {
            return false;
        }
    }
    if (!appendFmt(buffer, maxBytes, used, "]}")) return false;

    out.assign(buffer, used);
    return true;
}

} // namespace nrfSpectrum

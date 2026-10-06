// Portable nRF24L01+ 2.4 GHz RPD channel-sweep aggregation (no Arduino/RF24
// dependency, so the sweep/publish boundaries are native-testable).
//
// The radio backend (src/modules/nrf24_module.cpp) drives a real
// setChannel()/testRPD() sweep; this file owns the cooperative settle gate, the
// per-channel hit counters, the sweep counter and the bounded JSON encoding.
// Reference studied for the technique only (no code copied): RF24 library
// examples / the common "nRF24 scanner" RPD approach; RF24's own
// testRPD()/testCarrier() doc in .pio/libdeps/RF24/RF24.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/action_params.h"

namespace nrfSpectrum {

inline constexpr uint8_t kChannelFirst = 0;
inline constexpr uint8_t kChannelLast = 125; // nRF24L01+ 2400..2525 MHz
inline constexpr uint8_t kChannelCount = kChannelLast - kChannelFirst + 1;

// Per-channel dwell (synthesizer + RPD settle) bounds. The nRF24L01+ datasheet
// needs >=130 us after RX is enabled before RPD is valid, so the floor is
// 150 us (a 50 us floor would report misleading "clear" channels). The sweep
// visits at most one channel per call and never delays/blocks: `ready()` gates
// on the injected microsecond clock.
inline constexpr uint32_t kMinDwellUs = 150;
inline constexpr uint32_t kMaxDwellUs = 5000;
inline constexpr uint32_t kDefaultDwellUs = 300;

// ParamSpec order for the `nrf_scan` descriptor (parent's action_catalog.cpp).
inline constexpr ParamSpec kScanParams[] = {
    {"startChannel", "Kênh bắt đầu", ParamType::Integer, false, 0, 125, 0},
    {"endChannel", "Kênh kết thúc", ParamType::Integer, false, 0, 125, 0},
    {"dwellUs", "Thời gian dừng (µs)", ParamType::Integer, false, 150, 5000, 0},
};
inline constexpr size_t kScanParamCount = sizeof(kScanParams) / sizeof(kScanParams[0]);
enum ScanParam : size_t { ParamStartChannel = 0, ParamEndChannel = 1, ParamDwellUs = 2 };

// Clamps a requested dwell (or a default) into [kMinDwellUs, kMaxDwellUs].
uint32_t clampDwellUs(int64_t value);

// Cooperative per-channel state machine. begin() arms the first channel; the
// caller reads RPD exactly once per ready() and calls submit(), which records
// the hit, advances to the next channel and re-arms the settle gate. A wrap
// past `last` completes one sweep.
class Sweeper {
  public:
    void begin(uint32_t nowUs, uint8_t first, uint8_t last, uint32_t dwellUs);

    // Wrap-safe: true when the current channel has settled and may be read.
    bool ready(uint32_t nowUs) const;

    // Re-arms the settle gate for the current channel. RPD is latched for an RX
    // session, so the backend restarts RX (CE low -> channel write -> CE high)
    // on every probe and must measure the dwell from the real startListening(),
    // not from the previous read.
    void armSettle(uint32_t nowUs);

    uint8_t channel() const { return channel_; }
    uint8_t first() const { return first_; }
    uint8_t last() const { return last_; }
    uint32_t dwellUs() const { return dwellUs_; }
    uint32_t sweeps() const { return sweeps_; }
    uint16_t hits(uint8_t channel) const;

    // Records the RPD result for channel() and advances. Returns true when a
    // whole sweep just completed (the counters now include it). The gate is
    // armed from `nowUs`; the backend calls armSettle() again once RX is
    // actually re-entered for the new channel.
    bool submit(bool rpd, uint32_t nowUs);

  private:
    uint32_t nextProbeUs_ = 0;
    uint32_t dwellUs_ = kDefaultDwellUs;
    uint8_t first_ = kChannelFirst;
    uint8_t last_ = kChannelLast;
    uint8_t channel_ = kChannelFirst;
    uint32_t sweeps_ = 0;
    uint16_t hits_[kChannelCount] = {};
};

// Encodes {"kind":"nrf_spectrum","sweeps":N,"first":F,"last":L,"dwellUs":D,
// "hits":[126 saturating counters]}. Returns false (and leaves `out`
// untouched) when it would exceed `maxBytes`; the fixed 126-value body is
// always < kMaxActionOutputBytes.
bool formatSpectrumJson(const Sweeper &sweeper, std::string &out, size_t maxBytes = 1024);

} // namespace nrfSpectrum

// Portable CC1101 sub-GHz record for raw OOK/ASK waveforms. Converts one burst
// of GDO0 edge durations into a fixed-capacity record (center frequency + the
// mark/space durations in microseconds + the level of the first duration), then
// serializes it to a bounded JSON summary for status output and to a compact
// lossless binary form the module persists through ModuleRuntime::completeRecord
// for rf_replay. Also owns the pure sweep/hex/frequency helpers the CC1101
// handlers use, so every bound stays native-testable and the module never has to
// re-derive them.
//
// No hardware, no ArduinoJson-in-header, no allocation. The module owns GDO0 and
// this file only models the bytes/validation, so a decode can never hand the
// replay path a malformed waveform.
//
// Exact binary layout (little-endian):
//   [0]      'R'
//   [1]      'F'
//   [2]      version = 1
//   [3]      flags, bit0 = firstDurationLevelHigh (no other bits used)
//   [4..7]   center frequency in Hz (uint32)
//   [8..9]   duration count (uint16)
//   [10..]   count * uint16 duration in microseconds
// Limits: count <= kMaxDurations; 0 < duration <= kMaxDurationUs (capture
// saturates at kMaxDurationUs); sum(durations) <= kMaxTotalDurationUs; frequency
// must fall inside one CC1101 calibration band (see supportedFrequencyHz).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rfRecord {

// Duration cap: a single stored pulse/gap, in microseconds. Capture clamps to
// this so a long carrier or a long idle cannot overflow the uint16 field.
constexpr uint32_t kMaxDurationUs = 65535;
// One stored burst cap; rf_replay transmits a burst atomically to keep OOK
// timing, so this is also the longest single non-yielding replay step.
constexpr uint32_t kMaxTotalDurationUs = 100000;
// RecordBuffer::capacity is 4096, so 1024 durations + 10 header bytes fit.
constexpr size_t kMaxDurations = 1024;
// JSON is a bounded summary only: the full waveform lives in the RAM buffer.
constexpr size_t kMaxJsonDurations = 128;

constexpr size_t kEncodedHeaderBytes = 10;
constexpr size_t kMaxEncodedBytes = kEncodedHeaderBytes + 2 * kMaxDurations;
static_assert(kMaxEncodedBytes <= 4096, "RF record must fit the 4096-byte record buffer");

// CC1101 tunable bands the vendor driver calibrates for (PA tables cover
// 315/433/868/915; 378-386 is deliberately excluded, it has no PA table).
constexpr uint32_t kBandLowHz[] = {300000000u, 387000000u, 779000000u};
constexpr uint32_t kBandHighHz[] = {348000000u, 464000000u, 928000000u};
constexpr size_t kBandCount = 3;

// Empty     -> no durations captured (nothing to store).
// TooLong   -> count over cap or total over cap.
// Invalid   -> null buffer with count, zero/duplicate-invalid duration, or an
//              out-of-band frequency.
// Ready     -> record fully replaces the previous one.
enum class Result { Empty, TooLong, Invalid, Ready };

struct Record {
    uint32_t freqHz = 433920000u;  // center frequency used for the capture
    uint16_t count = 0;            // stored durations
    bool firstLevelHigh = true;    // level driven during durationsUs[0]
    uint16_t durationsUs[kMaxDurations] = {};
};

// Validates the whole record before writing anything, so a rejected capture
// never leaves a partially overwritten record behind.
Result makeRecord(uint32_t freqHz, bool firstLevelHigh, const uint16_t *durationsUs, size_t count,
                  Record &record);

// Sum of durations, saturated at the cap; 0 for an empty record.
uint32_t totalDurationUs(const Record &record);

// Bounded status summary: {"kind":"rf_record","freqHz":...,"pulseCount":N,
// "totalUs":N,"firstLevelHigh":bool,"durationUs":[<first kMaxJsonDurations>]}.
std::string toJson(const Record &record);

// Serializes into caller-owned `out` (capacity bytes) and stores the encoded
// length. Returns false without writing `out`/`length` for a structurally
// invalid record or an undersized buffer. kMaxEncodedBytes is the exact worst
// case, so a kMaxEncodedBytes array is always sufficient.
bool encodeToBytes(const Record &record, uint8_t *out, size_t capacity, size_t &length);

// Parses bytes produced by encodeToBytes(); rejects truncation/corruption and
// leaves `record` untouched on failure.
bool decode(const uint8_t *bytes, size_t length, Record &record);

// --- frequency / sweep / payload helpers ---------------------------------

bool supportedFrequencyHz(uint32_t hz);
bool supportedFrequencyMhz(double mhz);
// True when [startMhz, endMhz] fits inside one calibration band.
bool supportedBandMhz(double startMhz, double endMhz);

// Points in [startMhz, endMhz] stepping by stepKhz, inclusive; 0 when the plan
// is invalid (start >= end, not a single band, step 0, non-finite) or exceeds
// maxPoints.
size_t sweepPointCount(double startMhz, double endMhz, uint32_t stepKhz, size_t maxPoints);
double sweepPointMhz(double startMhz, uint32_t stepKhz, size_t index);

// Strict hex decode for rf_custom_tx payloadHex: non-empty, even length, within
// 2*capacity chars, only [0-9a-fA-F]. Writes outLength only on success.
bool decodeHex(const char *text, size_t textLength, uint8_t *out, size_t capacity, size_t &outLength);

// --- portable RMT transmit conversion -------------------------------------

// One or two GDO0 (level, duration) segments packed into a single RMT item.
// Durations are microseconds at the module's 1 us/tick RMT clock; a recorded
// duration above the 15-bit item field is split into consecutive same-level
// items so a long mark/space stays contiguous (never narrowed or truncated).
constexpr uint32_t kMaxTxChunkUs = 0x7FFFu; // 15-bit RMT duration field

struct TxItem {
    uint16_t duration0 = 0;
    uint8_t level0 = 0;
    uint16_t duration1 = 0;
    uint8_t level1 = 0;
};

// Item cap for one transmit stream. A valid record holds at most kMaxDurations
// durations totalling at most kMaxTotalDurationUs; every chunk is a full
// kMaxTxChunkUs, so the item count is well under this cap. A stream that would
// need more (a hand-built over-cap record) fails closed.
constexpr size_t kMaxTxItems = kMaxDurations + 8;

// Builds the replay item stream from a record: GDO0 alternates level starting at
// record.firstLevelHigh, one recorded duration per segment, exactly as captured.
// Returns the item count, or 0 on a null/empty/over-cap/zero-duration input or
// when the stream would not fit `capacity` (fail closed, never truncate).
size_t replayItems(const Record &record, TxItem *out, size_t capacity);

// Builds the item stream for a raw OOK bitstream: bits MSB-first, each held for
// bitPeriodUs at its own level. Returns the item count or 0 on the same failures.
size_t customItems(const uint8_t *bytes, size_t count, uint32_t bitPeriodUs, TxItem *out, size_t capacity);

// --- chip RSSI + bounded result summaries ---------------------------------

// CC1101 RSSI status byte (reg 0x34) to dBm, matching the vendor driver:
// the byte is a signed 8-bit value, dBm = value / 2 - 74.
int rssiToDbm(uint8_t raw);

// Ceiling on a single rf_scan / rf_spectrum sweep. 128 dBm entries keep every
// summary comfortably below the 1024-byte stream/payload ceiling.
constexpr size_t kMaxScanPoints = 128;

// Bounded sweep summary shared by rf_scan and rf_spectrum:
// {"kind":K,"startHz":S,"stepKhz":T,"count":N,"peakDbm":P,"peakHz":H,
//  "dbm":[N signed levels]}. `dbm` holds `count` entries (count <=
// kMaxScanPoints). startHz is the first point and stepKhz the increment, so
// the whole plan is reconstructible from the summary. Returns "" for a null
// kind/dbm or an empty/over-cap count. The peak is the largest dBm; the first
// occurrence wins a tie.
std::string sweepToJson(const char *kind, uint32_t startHz, uint32_t stepKhz, const int *dbm, size_t count);

// {"kind":"rf_replay","freqHz":..,"pulseCount":..,"totalUs":..,
//  "firstLevelHigh":bool,"repeat":N}
std::string replayToJson(const Record &record, uint32_t repeat);

// {"kind":"rf_custom_tx","freqHz":..,"bytes":N,"bitPeriodUs":B,"repeat":R}
std::string customTxToJson(uint32_t freqHz, size_t bytes, uint32_t bitPeriodUs, uint32_t repeat);

} // namespace rfRecord

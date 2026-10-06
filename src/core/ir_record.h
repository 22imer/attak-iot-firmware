// Portable IR capture record. Converts one library decode (raw ticks, leading
// idle gap included) into fixed microsecond timings plus the numeric
// decode/bits/value and the bounded protocol name, then serializes that
// validated record to JSON for status output and to a compact lossless binary
// form the runtime persists through ModuleRuntime::completeRecord. The module
// owns IRrecv and this file never touches hardware, TX or LittleFS, so the
// repeat/overflow/cap/precision edges stay native-testable and the typed record
// is captured before the receiver buffer is resumed/reused.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/record_buffer.h"

namespace irRecord {

constexpr size_t kMaxTimings = 512;       // spec §7.3 raw timing cap
constexpr size_t kMaxProtocolBytes = 32;  // bounded decode name, NUL-free

// Binary layout for encodeToBytes()/decode(), little-endian:
//   [0]      'I'
//   [1]      'R'
//   [2]      version = 1
//   [3]      flags, bit0 = decoded
//   [4..5]   count (uint16)
//   [6..7]   bits  (uint16)
//   [8..15]  value (uint64)
//   [16]     protocol length (uint8, <= kMaxProtocolBytes, no NUL byte)
//   [17..]   protocol bytes
//   then     count * uint32 timing microseconds
constexpr size_t kEncodedHeaderBytes = 17;
constexpr size_t kMaxEncodedBytes = kEncodedHeaderBytes + kMaxProtocolBytes + 4 * kMaxTimings;
static_assert(kMaxEncodedBytes <= RecordBuffer::capacity, "IR record must fit the 4096-byte record buffer");

// repeat: a repeat code, no new message -> Ignored.
// null raw, zero length, or leading-gap-only length -> Ignored (no message).
// overflow / rawLength-1 > kMaxTimings / raw*tickUs exceeding uint32 -> TooLong.
// zero tickUs, null or over-long protocol -> Invalid.
// On Ready the record is fully replaced; every other result leaves it untouched.
enum class Result { Ignored, TooLong, Invalid, Ready };

struct Record {
    uint16_t count = 0;                   // stored timings; excludes the leading gap
    bool decoded = false;                 // true only for a real numeric decode
    uint16_t bits = 0;                    // meaningful when decoded
    uint64_t value = 0;                   // real decode value; library synthetic otherwise
    char protocol[kMaxProtocolBytes + 1] = {};
    uint32_t timingsUs[kMaxTimings] = {}; // mark/space durations in microseconds
};

// `raw` is the library buffer including raw[0] (leading idle gap); `tickUs` is
// the real library tick factor. Validates the whole conversion before writing,
// so an invalid capture never leaves a partially overwritten record.
Result makeRecord(bool repeat, bool overflow, const volatile uint16_t *raw, size_t rawLength, uint32_t tickUs,
                  const char *protocol, bool decoded, uint16_t bits, uint64_t value, Record &record);

// spec §7.3 payload: {"kind":"ir_capture","protocol":...,"value":"0x…"|null,
// "rawTimingsUs":[…]}. Value is a normalized uppercase hex string, never a
// JSON number, so uint64 precision survives.
std::string toJson(const Record &record);

// Serializes into caller-owned `out` (capacity bytes) and stores the encoded
// length. Returns false without writing `out` or `length` for a structurally
// invalid record or an undersized buffer. kMaxEncodedBytes is the exact worst
// case, so a kMaxEncodedBytes array is always sufficient.
bool encodeToBytes(const Record &record, uint8_t *out, size_t capacity, size_t &length);

// Parses bytes produced by encodeToBytes(); rejects truncation/corruption and
// leaves `record` untouched on failure.
bool decode(const uint8_t *bytes, size_t length, Record &record);

} // namespace irRecord

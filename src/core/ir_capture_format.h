// Pure IR capture normalization. The module owns IRrecv; this converts one
// decode result into the spec payload before the library reuses its raw buffer.
// No Arduino/IRremote dependency, so the repeat/overflow/64-bit edges are
// native-testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace irCapture {

enum class FormatResult { Ignored, TooLong, Ready };

constexpr size_t kMaxTimings = 512;

// repeat: the decode was only a repeat code -> Ignored (keeps waiting).
// overflow / rawLength-1 > kMaxTimings: TooLong (payload must not change).
// Otherwise Ready: raw[0] (leading idle gap) is dropped, remaining ticks are
// converted with tickUs, and `json` is replaced with the payload. `value` is a
// real numeric decode (hex string) or null for UNKNOWN.
FormatResult format(bool repeat, bool overflow, const volatile uint16_t *raw, size_t rawLength, uint32_t tickUs,
                    const std::string &protocol, bool numericValueKnown, uint64_t value, std::string &json);

} // namespace irCapture

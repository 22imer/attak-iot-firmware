#include "ir_capture_format.h"

#include <ArduinoJson.h>

namespace irCapture {

FormatResult format(bool repeat, bool overflow, const volatile uint16_t *raw, size_t rawLength, uint32_t tickUs,
                    const std::string &protocol, bool numericValueKnown, uint64_t value, std::string &json) {
    if (repeat) return FormatResult::Ignored;
    if (rawLength == 0) return FormatResult::Ignored;
    if (overflow || rawLength - 1 > kMaxTimings) return FormatResult::TooLong;

    JsonDocument doc;
    doc["kind"] = "ir_capture";
    doc["protocol"] = protocol;
    if (numericValueKnown) {
        char buffer[19];
        snprintf(buffer, sizeof(buffer), "0x%016llX", static_cast<unsigned long long>(value));
        doc["value"] = buffer;
    } else {
        doc["value"] = nullptr; // UNKNOWN: the library value is a synthetic hash
    }

    JsonArray timings = doc["rawTimingsUs"].to<JsonArray>();
    for (size_t i = 1; i < rawLength; ++i) timings.add(static_cast<uint32_t>(raw[i]) * tickUs);

    std::string encoded;
    serializeJson(doc, encoded);
    json = std::move(encoded);
    return FormatResult::Ready;
}

} // namespace irCapture

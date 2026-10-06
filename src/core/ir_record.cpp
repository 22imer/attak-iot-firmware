#include "core/ir_record.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace irRecord {

namespace {

constexpr uint8_t kMagic0 = 'I';
constexpr uint8_t kMagic1 = 'R';
constexpr uint8_t kVersion = 1;
constexpr uint8_t kFlagDecoded = 0x01;

void put16(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void put32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

void put64(uint8_t *out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

uint16_t get16(const uint8_t *in) { return static_cast<uint16_t>(in[0] | (static_cast<uint16_t>(in[1]) << 8)); }

uint32_t get32(const uint8_t *in) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (8 * i);
    return value;
}

uint64_t get64(const uint8_t *in) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(in[i]) << (8 * i);
    return value;
}

// Bounded length of the NUL-terminated protocol name; returns
// kMaxProtocolBytes + 1 when unterminated within the fixed field.
size_t protocolLength(const Record &record) {
    size_t length = 0;
    while (length <= kMaxProtocolBytes && record.protocol[length] != '\0') ++length;
    return length;
}

} // namespace

Result makeRecord(bool repeat, bool overflow, const volatile uint16_t *raw, size_t rawLength, uint32_t tickUs,
                  const char *protocol, bool decoded, uint16_t bits, uint64_t value, Record &record) {
    if (repeat) return Result::Ignored;
    if (raw == nullptr || rawLength <= 1) return Result::Ignored;
    if (overflow || rawLength - 1 > kMaxTimings) return Result::TooLong;
    if (tickUs == 0) return Result::Invalid;
    if (protocol == nullptr) return Result::Invalid;

    size_t nameLength = 0;
    while (nameLength <= kMaxProtocolBytes && protocol[nameLength] != '\0') ++nameLength;
    if (nameLength > kMaxProtocolBytes) return Result::Invalid;

    // Validate every conversion before writing anything, so a rejected capture
    // can never leave a partially overwritten record behind.
    for (size_t i = 1; i < rawLength; ++i) {
        if (raw[i] > UINT32_MAX / tickUs) return Result::TooLong;
    }

    record.count = static_cast<uint16_t>(rawLength - 1);
    record.decoded = decoded;
    record.bits = bits;
    record.value = value;
    std::memcpy(record.protocol, protocol, nameLength);
    record.protocol[nameLength] = '\0';
    for (size_t i = 1; i < rawLength; ++i) {
        record.timingsUs[i - 1] = static_cast<uint32_t>(raw[i]) * tickUs;
    }
    return Result::Ready;
}

std::string toJson(const Record &record) {
    JsonDocument doc;
    doc["kind"] = "ir_capture";
    doc["protocol"] = record.protocol;
    if (record.decoded) {
        char hex[19];
        std::snprintf(hex, sizeof(hex), "0x%016llX", static_cast<unsigned long long>(record.value));
        doc["value"] = hex;
    } else {
        doc["value"] = nullptr; // no real numeric decode (library value is synthetic)
    }

    JsonArray timings = doc["rawTimingsUs"].to<JsonArray>();
    for (uint16_t i = 0; i < record.count; ++i) timings.add(record.timingsUs[i]);

    std::string json;
    serializeJson(doc, json);
    return json;
}

bool encodeToBytes(const Record &record, uint8_t *out, size_t capacity, size_t &length) {
    if (record.count > kMaxTimings) return false;
    const size_t nameLength = protocolLength(record);
    if (nameLength > kMaxProtocolBytes) return false;

    const size_t total = kEncodedHeaderBytes + nameLength + 4 * static_cast<size_t>(record.count);
    if (out == nullptr || total > capacity) return false;

    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = kVersion;
    out[3] = record.decoded ? kFlagDecoded : 0;
    put16(out + 4, record.count);
    put16(out + 6, record.bits);
    put64(out + 8, record.value);
    out[16] = static_cast<uint8_t>(nameLength);
    std::memcpy(out + kEncodedHeaderBytes, record.protocol, nameLength);

    uint8_t *timings = out + kEncodedHeaderBytes + nameLength;
    for (uint16_t i = 0; i < record.count; ++i) put32(timings + 4 * static_cast<size_t>(i), record.timingsUs[i]);

    length = total;
    return true;
}

bool decode(const uint8_t *bytes, size_t length, Record &record) {
    if (bytes == nullptr || length < kEncodedHeaderBytes) return false;
    if (bytes[0] != kMagic0 || bytes[1] != kMagic1 || bytes[2] != kVersion) return false;
    if ((bytes[3] & ~kFlagDecoded) != 0) return false;

    const uint16_t count = get16(bytes + 4);
    if (count > kMaxTimings) return false;
    const size_t nameLength = bytes[16];
    if (nameLength > kMaxProtocolBytes) return false;

    const size_t expected = kEncodedHeaderBytes + nameLength + 4 * static_cast<size_t>(count);
    if (length != expected) return false;

    const uint8_t *protocol = bytes + kEncodedHeaderBytes;
    for (size_t i = 0; i < nameLength; ++i) {
        if (protocol[i] == 0) return false; // protocol names are NUL-free
    }
    const uint8_t *timings = protocol + nameLength;

    // Fully validated; only now overwrite the destination record.
    record.count = count;
    record.decoded = (bytes[3] & kFlagDecoded) != 0;
    record.bits = get16(bytes + 6);
    record.value = get64(bytes + 8);
    std::memcpy(record.protocol, protocol, nameLength);
    record.protocol[nameLength] = '\0';
    for (uint16_t i = 0; i < count; ++i) record.timingsUs[i] = get32(timings + 4 * static_cast<size_t>(i));
    return true;
}

} // namespace irRecord

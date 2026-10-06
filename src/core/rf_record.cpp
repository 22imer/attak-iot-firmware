#include "core/rf_record.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>

namespace rfRecord {

namespace {

constexpr uint8_t kMagic0 = 'R';
constexpr uint8_t kMagic1 = 'F';
constexpr uint8_t kVersion = 1;
constexpr uint8_t kFlagFirstLevelHigh = 0x01;

void put16(uint8_t *out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void put32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
}

uint16_t get16(const uint8_t *in) { return static_cast<uint16_t>(in[0] | (static_cast<uint16_t>(in[1]) << 8)); }

uint32_t get32(const uint8_t *in) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(in[i]) << (8 * i);
    return value;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Packs a stream of (level, duration) segments into TxItems, splitting any
// duration above the 15-bit field into consecutive same-level chunks. `overflow`
// latches when the caller's item capacity is too small, so the build fails
// closed instead of truncating.
struct TxItemWriter {
    TxItem *out = nullptr;
    size_t capacity = 0;
    size_t count = 0;
    bool overflow = false;
    bool hasPending = false;
    uint16_t pendingDuration = 0;
    uint8_t pendingLevel = 0;

    void add(uint8_t level, uint32_t durationUs) {
        while (durationUs > 0 && !overflow) {
            const uint32_t chunk = durationUs > kMaxTxChunkUs ? kMaxTxChunkUs : durationUs;
            if (!hasPending) {
                pendingLevel = level;
                pendingDuration = static_cast<uint16_t>(chunk);
                hasPending = true;
            } else if (count < capacity) {
                out[count].duration0 = pendingDuration;
                out[count].level0 = pendingLevel;
                out[count].duration1 = static_cast<uint16_t>(chunk);
                out[count].level1 = level;
                ++count;
                hasPending = false;
            } else {
                overflow = true;
                return;
            }
            durationUs -= chunk;
        }
    }

    size_t finish() {
        if (overflow) return 0;
        if (hasPending) {
            if (count >= capacity) return 0;
            out[count].duration0 = pendingDuration;
            out[count].level0 = pendingLevel;
            out[count].duration1 = 0;
            out[count].level1 = 0;
            ++count;
            hasPending = false;
        }
        return count;
    }
};

} // namespace

bool supportedFrequencyHz(uint32_t hz) {
    for (size_t i = 0; i < kBandCount; ++i) {
        if (hz >= kBandLowHz[i] && hz <= kBandHighHz[i]) return true;
    }
    return false;
}

bool supportedFrequencyMhz(double mhz) {
    if (!std::isfinite(mhz)) return false;
    if (mhz < 0.0 || mhz > 4294.0) return false; // stay well inside uint32 Hz
    const double hz = mhz * 1000000.0;
    // Round to the nearest whole Hz before the band test; a value like 433.9199
    // must not fall out of the 433 band because of binary representation.
    if (hz < 0.0) return false;
    return supportedFrequencyHz(static_cast<uint32_t>(hz + 0.5));
}

bool supportedBandMhz(double startMhz, double endMhz) {
    if (!std::isfinite(startMhz) || !std::isfinite(endMhz)) return false;
    if (startMhz >= endMhz) return false;
    const double start = startMhz * 1000000.0;
    const double end = endMhz * 1000000.0;
    if (start < 0.0 || end > 4294967295.0) return false;
    for (size_t i = 0; i < kBandCount; ++i) {
        if (start >= static_cast<double>(kBandLowHz[i]) && end <= static_cast<double>(kBandHighHz[i])) {
            return true;
        }
    }
    return false;
}

size_t sweepPointCount(double startMhz, double endMhz, uint32_t stepKhz, size_t maxPoints) {
    if (!supportedBandMhz(startMhz, endMhz)) return 0;
    if (stepKhz == 0) return 0;
    const double spanKhz = (endMhz - startMhz) * 1000.0;
    const double raw = std::floor(spanKhz / static_cast<double>(stepKhz) + 1e-9) + 1.0;
    if (!(raw >= 1.0)) return 0;
    if (raw > static_cast<double>(maxPoints)) return 0;
    return static_cast<size_t>(raw);
}

double sweepPointMhz(double startMhz, uint32_t stepKhz, size_t index) {
    return startMhz + static_cast<double>(stepKhz) * static_cast<double>(index) / 1000.0;
}

bool decodeHex(const char *text, size_t textLength, uint8_t *out, size_t capacity, size_t &outLength) {
    if (text == nullptr || out == nullptr) return false;
    if (textLength == 0 || (textLength & 1u) != 0) return false;
    const size_t bytes = textLength / 2;
    if (bytes > capacity) return false;

    for (size_t i = 0; i < bytes; ++i) {
        const int high = hexNibble(text[2 * i]);
        const int low = hexNibble(text[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    outLength = bytes;
    return true;
}

Result makeRecord(uint32_t freqHz, bool firstLevelHigh, const uint16_t *durationsUs, size_t count,
                  Record &record) {
    if (count == 0) return Result::Empty;
    if (durationsUs == nullptr) return Result::Invalid;
    if (count > kMaxDurations) return Result::TooLong;
    if (!supportedFrequencyHz(freqHz)) return Result::Invalid;

    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (durationsUs[i] == 0 || durationsUs[i] > kMaxDurationUs) return Result::Invalid;
        total += durationsUs[i];
    }
    if (total > kMaxTotalDurationUs) return Result::TooLong;

    // Fully validated; only now overwrite the destination record.
    record.freqHz = freqHz;
    record.count = static_cast<uint16_t>(count);
    record.firstLevelHigh = firstLevelHigh;
    for (size_t i = 0; i < count; ++i) record.durationsUs[i] = durationsUs[i];
    return Result::Ready;
}

uint32_t totalDurationUs(const Record &record) {
    uint64_t total = 0;
    for (uint16_t i = 0; i < record.count; ++i) total += record.durationsUs[i];
    return total > kMaxTotalDurationUs ? kMaxTotalDurationUs : static_cast<uint32_t>(total);
}

std::string toJson(const Record &record) {
    JsonDocument doc;
    doc["kind"] = "rf_record";
    doc["freqHz"] = record.freqHz;
    doc["pulseCount"] = record.count;
    doc["totalUs"] = totalDurationUs(record);
    doc["firstLevelHigh"] = record.firstLevelHigh;

    JsonArray durations = doc["durationUs"].to<JsonArray>();
    const size_t listed = record.count < kMaxJsonDurations ? record.count : kMaxJsonDurations;
    for (size_t i = 0; i < listed; ++i) durations.add(record.durationsUs[i]);

    std::string json;
    serializeJson(doc, json);
    return json;
}

bool encodeToBytes(const Record &record, uint8_t *out, size_t capacity, size_t &length) {
    if (record.count > kMaxDurations) return false;
    if (!supportedFrequencyHz(record.freqHz)) return false;

    const size_t total = kEncodedHeaderBytes + 2 * static_cast<size_t>(record.count);
    if (out == nullptr || total > capacity) return false;

    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = kVersion;
    out[3] = record.firstLevelHigh ? kFlagFirstLevelHigh : 0;
    put32(out + 4, record.freqHz);
    put16(out + 8, record.count);
    for (uint16_t i = 0; i < record.count; ++i) put16(out + kEncodedHeaderBytes + 2 * static_cast<size_t>(i), record.durationsUs[i]);

    length = total;
    return true;
}

bool decode(const uint8_t *bytes, size_t length, Record &record) {
    if (bytes == nullptr || length < kEncodedHeaderBytes) return false;
    if (bytes[0] != kMagic0 || bytes[1] != kMagic1 || bytes[2] != kVersion) return false;
    if ((bytes[3] & ~kFlagFirstLevelHigh) != 0) return false;

    const uint16_t count = get16(bytes + 8);
    if (count == 0 || count > kMaxDurations) return false;

    const uint32_t freqHz = get32(bytes + 4);
    if (!supportedFrequencyHz(freqHz)) return false;

    const size_t expected = kEncodedHeaderBytes + 2 * static_cast<size_t>(count);
    if (length != expected) return false;

    uint64_t total = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t duration = get16(bytes + kEncodedHeaderBytes + 2 * static_cast<size_t>(i));
        if (duration == 0 || duration > kMaxDurationUs) return false;
        total += duration;
    }
    if (total > kMaxTotalDurationUs) return false;

    // Fully validated; only now overwrite the destination record.
    record.freqHz = freqHz;
    record.count = count;
    record.firstLevelHigh = (bytes[3] & kFlagFirstLevelHigh) != 0;
    for (uint16_t i = 0; i < count; ++i) {
        record.durationsUs[i] = get16(bytes + kEncodedHeaderBytes + 2 * static_cast<size_t>(i));
    }
    return true;
}

size_t replayItems(const Record &record, TxItem *out, size_t capacity) {
    if (out == nullptr || capacity == 0) return 0;
    if (record.count == 0 || record.count > kMaxDurations) return 0;
    for (uint16_t i = 0; i < record.count; ++i) {
        if (record.durationsUs[i] == 0 || record.durationsUs[i] > kMaxDurationUs) return 0;
    }

    TxItemWriter writer{out, capacity};
    bool levelHigh = record.firstLevelHigh;
    for (uint16_t i = 0; i < record.count; ++i) {
        writer.add(levelHigh ? 1 : 0, record.durationsUs[i]);
        levelHigh = !levelHigh;
    }
    return writer.finish();
}

size_t customItems(const uint8_t *bytes, size_t count, uint32_t bitPeriodUs, TxItem *out, size_t capacity) {
    if (bytes == nullptr || count == 0 || bitPeriodUs == 0 || out == nullptr || capacity == 0) return 0;

    TxItemWriter writer{out, capacity};
    for (size_t b = 0; b < count; ++b) {
        const uint8_t byte = bytes[b];
        for (int bit = 7; bit >= 0; --bit) writer.add(((byte >> bit) & 0x01) ? 1 : 0, bitPeriodUs);
    }
    return writer.finish();
}

int rssiToDbm(uint8_t raw) {
    const int value = raw >= 128 ? static_cast<int>(raw) - 256 : static_cast<int>(raw);
    return value / 2 - 74;
}

std::string sweepToJson(const char *kind, uint32_t startHz, uint32_t stepKhz, const int *dbm, size_t count) {
    if (kind == nullptr || dbm == nullptr || count == 0 || count > kMaxScanPoints) return {};

    JsonDocument doc;
    doc["kind"] = kind;
    doc["startHz"] = startHz;
    doc["stepKhz"] = stepKhz;
    doc["count"] = count;

    JsonArray levels = doc["dbm"].to<JsonArray>();
    int peak = dbm[0];
    size_t peakIndex = 0;
    for (size_t i = 0; i < count; ++i) {
        levels.add(dbm[i]);
        if (dbm[i] > peak) {
            peak = dbm[i];
            peakIndex = i;
        }
    }
    doc["peakDbm"] = peak;
    doc["peakHz"] = static_cast<uint32_t>(static_cast<uint64_t>(startHz) +
                                          static_cast<uint64_t>(stepKhz) * 1000ull * peakIndex);

    std::string json;
    serializeJson(doc, json);
    return json;
}

std::string replayToJson(const Record &record, uint32_t repeat) {
    JsonDocument doc;
    doc["kind"] = "rf_replay";
    doc["freqHz"] = record.freqHz;
    doc["pulseCount"] = record.count;
    doc["totalUs"] = totalDurationUs(record);
    doc["firstLevelHigh"] = record.firstLevelHigh;
    doc["repeat"] = repeat;

    std::string json;
    serializeJson(doc, json);
    return json;
}

std::string customTxToJson(uint32_t freqHz, size_t bytes, uint32_t bitPeriodUs, uint32_t repeat) {
    JsonDocument doc;
    doc["kind"] = "rf_custom_tx";
    doc["freqHz"] = freqHz;
    doc["bytes"] = bytes;
    doc["bitPeriodUs"] = bitPeriodUs;
    doc["repeat"] = repeat;

    std::string json;
    serializeJson(doc, json);
    return json;
}

} // namespace rfRecord

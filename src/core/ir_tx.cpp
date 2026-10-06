#include "core/ir_tx.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace irTx {

namespace {

constexpr ProtocolCode kTvbgone[] = {
    {Protocol::Nec, 0x20DF10EFULL, 32, 0},
    {Protocol::Nec, 0x02FD48B7ULL, 32, 0},
    {Protocol::Samsung, 0xE0E040BFULL, 32, 0},
    {Protocol::Sony, 0xA90ULL, 12, 0},
    {Protocol::Rc5, 0x0CULL, 12, 0},
    {Protocol::Panasonic, 0x40040100BCBDULL, 48, 0},
};

const char *const kTvbgoneModel[] = {
    "LG TV (NEC 32-bit)",
    "Toshiba TV (NEC 32-bit)",
    "Samsung TV (Samsung 32-bit)",
    "Sony TV (SIRC 12-bit, command 21)",
    "Philips TV (RC5 12-bit)",
    "Panasonic TV (Panasonic 48-bit)",
};

bool equalsIgnoreCase(std::string_view a, const char *b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string valueHex(uint64_t value) {
    char hex[19];
    std::snprintf(hex, sizeof(hex), "0x%016llX", static_cast<unsigned long long>(value));
    return std::string(hex);
}

// Shared numeric bound check for the optional frequency param.
Error checkFrequency(bool hasFrequency, int64_t frequencyHz) {
    if (!hasFrequency) return Error::None;
    if (frequencyHz < static_cast<int64_t>(kMinFrequencyHz) ||
        frequencyHz > static_cast<int64_t>(kMaxFrequencyHz)) {
        return Error::InvalidFrequency;
    }
    return Error::None;
}

// Protocol timing facts, transcribed from the public IR protocol definitions
// (and cross-checked against IRremoteESP8266's per-protocol senders so a burst
// is decodable by the same strict receivers). Unit: microseconds.
//
// `minCommandLength`/`minGap` reproduce the library's sendGeneric padding: a
// frame is padded with a trailing space so its total length reaches
// minCommandLength, never shorter than minGap. `repeats` is the mandatory
// extra-frame count (Sony minRepeats == 2).
struct GenericProfile {
    uint32_t hdrMark;
    uint32_t hdrSpace;
    uint32_t bitMark;
    uint32_t oneSpace;
    uint32_t zeroSpace;
    uint32_t footerMark; // 0 => none
    uint32_t minGap;
    uint32_t minCommandLength;
    uint16_t repeats;
    Carrier carrier;
};

constexpr GenericProfile kGenericProfiles[] = {
    // NEC: 38 kHz, 33% duty, 32-bit, one frame.
    {8960, 4480, 560, 1680, 560, 560, 22400, 108080, 0, {38000, 33}},
    // Sony SIRC: 40 kHz, 33% duty, header + 2 marks/bit, no footer, 45 ms frame
    // repeated twice (three frames total).
    {2400, 600, 1200, 600, 600, 0, 10000, 45000, 2, {40000, 33}},
    // Samsung: 38 kHz, 33% duty, 32-bit, one frame.
    {4480, 4480, 560, 1680, 560, 560, 26880, 108080, 0, {38000, 33}},
    // Panasonic (Kaseikyo): 36.7 kHz, 50% duty, 48-bit, one frame.
    {3456, 1728, 432, 1296, 432, 432, 74736, 163296, 0, {36700, 50}},
};

const GenericProfile &genericProfile(Protocol protocol) {
    switch (protocol) {
    case Protocol::Nec: return kGenericProfiles[0];
    case Protocol::Sony: return kGenericProfiles[1];
    case Protocol::Samsung: return kGenericProfiles[2];
    case Protocol::Panasonic: return kGenericProfiles[3];
    case Protocol::Rc5:
    case Protocol::Count: break;
    }
    return kGenericProfiles[0];
}

// Appends one mark/space run, merging it with the previous entry when the level
// is unchanged so the burst keeps strict alternation (RC5 and Sony can produce
// two adjacent same-level segments). Fails only if the fixed burst array is
// exhausted, which the size caps make unreachable for the supported set.
bool appendRun(Burst &burst, bool mark, uint32_t us) {
    if (us == 0) return true;
    const bool lastMark = burst.count > 0 && (burst.count % 2) == 1;
    if (burst.count > 0 && lastMark == mark) {
        burst.timings[burst.count - 1] += us;
        return true;
    }
    if (burst.count >= kMaxTimings) return false;
    burst.timings[burst.count++] = us;
    return true;
}

Error encodeGeneric(const ProtocolCode &code, Burst &out) {
    const GenericProfile &p = genericProfile(code.protocol);
    Burst built;
    built.carrierHz = p.carrier.frequencyHz;
    built.dutyPercent = p.carrier.dutyPercent;

    const uint16_t bits = code.bits;
    for (uint16_t frame = 0; frame <= p.repeats; ++frame) {
        uint32_t elapsed = 0;
        if (!appendRun(built, true, p.hdrMark)) return Error::TooLong;
        elapsed += p.hdrMark;
        if (!appendRun(built, false, p.hdrSpace)) return Error::TooLong;
        elapsed += p.hdrSpace;
        for (int i = static_cast<int>(bits) - 1; i >= 0; --i) {
            const uint32_t space = ((code.value >> i) & 1ULL) != 0 ? p.oneSpace : p.zeroSpace;
            if (!appendRun(built, true, p.bitMark)) return Error::TooLong;
            if (!appendRun(built, false, space)) return Error::TooLong;
            elapsed += p.bitMark + space;
        }
        if (p.footerMark != 0) {
            if (!appendRun(built, true, p.footerMark)) return Error::TooLong;
            elapsed += p.footerMark;
        }
        const uint32_t trailing =
            elapsed >= p.minCommandLength ? p.minGap : std::max(p.minGap, p.minCommandLength - elapsed);
        if (!appendRun(built, false, trailing)) return Error::TooLong;
    }
    out = built;
    return Error::None;
}

// RC5 (12 data bits, 36 kHz, 25% duty): Manchester-ish space/mark per bit, no
// header. Mirrors the library's sendRC5: start-bit mark, field-bit 1
// (space+mark), then each data bit MSB-first. Runs of equal bits merge.
Error encodeRc5(const ProtocolCode &code, Burst &out) {
    constexpr uint32_t kT1 = 889;
    constexpr uint32_t kMinCommandLength = 113778; // 14 raw bits * 2 * T1 + min gap
    constexpr uint32_t kMinGap = 88886;            // kMinCommandLength - 14*2*kT1

    Burst built;
    built.carrierHz = 36000;
    built.dutyPercent = 25;

    uint32_t elapsed = 0;
    if (!appendRun(built, true, kT1)) return Error::TooLong; // start bit
    if (!appendRun(built, false, kT1)) return Error::TooLong;
    if (!appendRun(built, true, kT1)) return Error::TooLong; // field bit = 1
    elapsed = 3 * kT1;
    for (int i = static_cast<int>(code.bits) - 1; i >= 0; --i) {
        const bool one = ((code.value >> i) & 1ULL) != 0;
        if (one) {
            if (!appendRun(built, false, kT1)) return Error::TooLong;
            if (!appendRun(built, true, kT1)) return Error::TooLong;
        } else {
            if (!appendRun(built, true, kT1)) return Error::TooLong;
            if (!appendRun(built, false, kT1)) return Error::TooLong;
        }
        elapsed += 2 * kT1;
    }
    const uint32_t trailing =
        elapsed >= kMinCommandLength ? kMinGap : std::max(kMinGap, kMinCommandLength - elapsed);
    if (!appendRun(built, false, trailing)) return Error::TooLong;
    out = built;
    return Error::None;
}

} // namespace

const char *protocolName(Protocol protocol) {
    switch (protocol) {
    case Protocol::Nec: return "NEC";
    case Protocol::Sony: return "SONY";
    case Protocol::Rc5: return "RC5";
    case Protocol::Samsung: return "SAMSUNG";
    case Protocol::Panasonic: return "PANASONIC";
    case Protocol::Count: break;
    }
    return "UNKNOWN";
}

const char *errorName(Error error) {
    switch (error) {
    case Error::None: return "";
    case Error::UnsupportedProtocol: return "unsupported_protocol";
    case Error::MalformedProtocol: return "malformed_protocol";
    case Error::InvalidCode: return "invalid_code";
    case Error::ValueOutOfRange: return "value_out_of_range";
    case Error::InvalidBits: return "invalid_bits";
    case Error::InvalidFrequency: return "invalid_frequency";
    case Error::MissingRequired: return "missing_required";
    case Error::UnexpectedParam: return "unexpected_param";
    case Error::MalformedRaw: return "malformed_raw";
    case Error::TooLong: return "too_long";
    case Error::Empty: return "empty";
    }
    return "invalid";
}

bool parseProtocol(std::string_view name, Protocol &out) {
    struct Entry { const char *name; Protocol protocol; };
    static constexpr Entry kEntries[] = {
        {"NEC", Protocol::Nec},       {"SONY", Protocol::Sony},   {"RC5", Protocol::Rc5},
        {"SAMSUNG", Protocol::Samsung}, {"PANASONIC", Protocol::Panasonic},
    };
    for (const Entry &entry : kEntries) {
        if (equalsIgnoreCase(name, entry.name)) {
            out = entry.protocol;
            return true;
        }
    }
    return false;
}

Carrier protocolCarrier(Protocol protocol) {
    if (protocol == Protocol::Rc5) return Carrier{36000, 25};
    if (protocol == Protocol::Count) return Carrier{kDefaultFrequencyHz, 33};
    return genericProfile(protocol).carrier;
}

bool protocolSupportsBits(Protocol protocol, uint16_t bits) {
    switch (protocol) {
    case Protocol::Nec:
    case Protocol::Samsung: return bits == 32;
    case Protocol::Sony: return bits == 12 || bits == 15 || bits == 20;
    case Protocol::Rc5: return bits == 12;
    case Protocol::Panasonic: return bits == 48;
    case Protocol::Count: break;
    }
    return false;
}

Error buildProtocolBurst(const ProtocolCode &code, Burst &out) {
    if (code.protocol == Protocol::Count) return Error::UnsupportedProtocol;
    if (!protocolSupportsBits(code.protocol, code.bits)) return Error::InvalidBits;
    if (code.bits < kMaxBits && (code.value >> code.bits) != 0) return Error::ValueOutOfRange;
    if (code.protocol == Protocol::Rc5) return encodeRc5(code, out);
    return encodeGeneric(code, out);
}

Error parseHexCode(std::string_view text, uint64_t &value) {
    size_t i = 0;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) i = 2;
    const size_t digitsStart = i;
    uint64_t parsed = 0;
    for (; i < text.size(); ++i) {
        const int digit = hexDigit(text[i]);
        if (digit < 0) return Error::InvalidCode;
        if (i - digitsStart >= 16) return Error::InvalidCode; // more than uint64
        parsed = (parsed << 4) | static_cast<uint64_t>(digit);
    }
    if (i == digitsStart) return Error::InvalidCode; // no digits
    value = parsed;
    return Error::None;
}

Error buildReplayWaveform(const irRecord::Record &record, Waveform &out) {
    if (record.count == 0) return Error::Empty;
    if (record.count > kMaxTimings) return Error::TooLong;
    uint32_t total = 0;
    for (uint16_t i = 0; i < record.count; ++i) {
        const uint32_t timing = record.timingsUs[i];
        if (timing == 0) return Error::MalformedRaw; // 0 is the RMT end marker, not a duration
        if (timing > kMaxTimingUs) return Error::TooLong;
        total += timing;
        if (total > kMaxRawTxDurationUs) return Error::TooLong;
    }

    Waveform built;
    built.count = record.count;
    built.totalUs = total;
    for (uint16_t i = 0; i < record.count; ++i) {
        built.timings[i] = static_cast<uint16_t>(record.timingsUs[i]);
    }
    out = built;
    return Error::None;
}

Error parseRawWaveform(std::string_view text, Waveform &out) {
    if (text.empty()) return Error::Empty;
    if (text.size() > kMaxRawParamBytes) return Error::TooLong;

    Waveform built;
    uint32_t total = 0;
    size_t i = 0;
    while (true) {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) return Error::MalformedRaw; // trailing comma / no value

        uint32_t value = 0;
        size_t digits = 0;
        for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
            value = value * 10 + static_cast<uint32_t>(text[i] - '0');
            if (value > kMaxTimingUs) return Error::ValueOutOfRange;
            ++digits;
        }
        if (digits == 0) return Error::MalformedRaw;
        if (value == 0) return Error::MalformedRaw;
        if (built.count >= kMaxTimings) return Error::TooLong;
        built.timings[built.count++] = static_cast<uint16_t>(value);
        total += value;
        if (total > kMaxRawTxDurationUs) return Error::TooLong;

        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) break;
        if (text[i] != ',') return Error::MalformedRaw;
        ++i; // consume separator; the next iteration requires a value
    }

    built.totalUs = total;
    out = built;
    return Error::None;
}

Error buildCustom(std::string_view protocolNameArg, bool hasCode, std::string_view codeHex, bool hasBits,
                  int64_t bits, bool hasFrequency, int64_t frequencyHz, bool hasRaw, std::string_view rawText,
                  CustomRequest &out) {
    Protocol protocol = Protocol::Nec;
    if (!parseProtocol(protocolNameArg, protocol)) {
        if (equalsIgnoreCase(protocolNameArg, "RAW")) {
            if (!hasRaw) return Error::MissingRequired;
            if (hasCode || hasBits) return Error::UnexpectedParam;
            const Error frequencyError = checkFrequency(hasFrequency, frequencyHz);
            if (frequencyError != Error::None) return frequencyError;

            Waveform waveform;
            const Error rawError = parseRawWaveform(rawText, waveform);
            if (rawError != Error::None) return rawError;

            CustomRequest built;
            built.raw = true;
            built.waveform = waveform;
            built.frequencyHz = hasFrequency ? static_cast<uint32_t>(frequencyHz) : kDefaultFrequencyHz;
            out = built;
            return Error::None;
        }
        return Error::UnsupportedProtocol;
    }

    if (hasRaw) return Error::UnexpectedParam;
    if (!hasCode || !hasBits) return Error::MissingRequired;
    if (bits < kMinBits || bits > kMaxBits) return Error::InvalidBits;
    if (!protocolSupportsBits(protocol, static_cast<uint16_t>(bits))) return Error::InvalidBits;
    const Error frequencyError = checkFrequency(hasFrequency, frequencyHz);
    if (frequencyError != Error::None) return frequencyError;

    uint64_t value = 0;
    const Error codeError = parseHexCode(codeHex, value);
    if (codeError != Error::None) return codeError;
    if (bits < kMaxBits && (value >> static_cast<unsigned>(bits)) != 0) return Error::ValueOutOfRange;

    CustomRequest built;
    built.raw = false;
    built.code.protocol = protocol;
    built.code.value = value;
    built.code.bits = static_cast<uint16_t>(bits);
    built.code.frequencyHz = 0; // protocol carrier is fixed by the library encoder
    out = built;
    return Error::None;
}

const ProtocolCode &tvbgoneCode(size_t index) {
    return kTvbgone[index < kTvbgoneCount ? index : kTvbgoneCount - 1];
}

const char *tvbgoneModel(size_t index) {
    return kTvbgoneModel[index < kTvbgoneCount ? index : kTvbgoneCount - 1];
}

Step nextStep(Plan &plan) {
    Step step;
    switch (plan.kind) {
    case PlanKind::None:
        return step; // StepKind::None
    case PlanKind::Protocol:
        if (plan.cursor != 0) {
            step.kind = StepKind::Done;
            return step;
        }
        plan.cursor = 1;
        step.kind = StepKind::Protocol;
        step.code = plan.code;
        return step;
    case PlanKind::Raw:
        if (plan.cursor != 0) {
            step.kind = StepKind::Done;
            return step;
        }
        plan.cursor = 1;
        step.kind = StepKind::Raw;
        step.timings = plan.waveform.timings;
        step.count = plan.waveform.count;
        step.frequencyHz = plan.frequencyHz;
        return step;
    case PlanKind::Tvbgone:
        if (plan.cursor >= kTvbgoneCount) {
            step.kind = StepKind::Done;
            return step;
        }
        step.kind = StepKind::Protocol;
        step.code = kTvbgone[plan.cursor];
        ++plan.cursor;
        return step;
    }
    return step;
}

std::string replayToJson(const irRecord::Record &record, const Waveform &waveform) {
    JsonDocument doc;
    doc["kind"] = "ir_replay";
    doc["protocol"] = record.protocol;
    if (record.decoded) {
        doc["value"] = valueHex(record.value);
    } else {
        doc["value"] = nullptr;
    }
    doc["timings"] = waveform.count;
    doc["durationUs"] = waveform.totalUs;
    std::string json;
    serializeJson(doc, json);
    return json;
}

std::string customProtocolToJson(const ProtocolCode &code) {
    JsonDocument doc;
    doc["kind"] = "ir_custom_tx";
    doc["protocol"] = protocolName(code.protocol);
    doc["value"] = valueHex(code.value);
    doc["bits"] = code.bits;
    std::string json;
    serializeJson(doc, json);
    return json;
}

std::string customRawToJson(size_t timings, uint32_t durationUs, uint32_t frequencyHz) {
    JsonDocument doc;
    doc["kind"] = "ir_custom_tx";
    doc["protocol"] = "RAW";
    doc["timings"] = timings;
    doc["durationUs"] = durationUs;
    doc["frequencyHz"] = frequencyHz;
    std::string json;
    serializeJson(doc, json);
    return json;
}

std::string tvbgoneToJson(size_t codesSent) {
    JsonDocument doc;
    doc["kind"] = "ir_tvbgone";
    doc["codesSent"] = codesSent;
    JsonArray codes = doc["codes"].to<JsonArray>();
    for (size_t i = 0; i < kTvbgoneCount; ++i) {
        JsonObject entry = codes.add<JsonObject>();
        entry["protocol"] = protocolName(kTvbgone[i].protocol);
        entry["value"] = valueHex(kTvbgone[i].value);
        entry["bits"] = kTvbgone[i].bits;
        entry["model"] = kTvbgoneModel[i];
    }
    std::string json;
    serializeJson(doc, json);
    return json;
}

} // namespace irTx

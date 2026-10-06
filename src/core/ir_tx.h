// Portable IR transmit planning for the ir module (T40 replay, T41 tvbgone,
// T42 custom_tx). No Arduino, no IRremoteESP8266 and no hardware: everything
// here is validation, bounded conversion, protocol timing synthesis and step
// sequencing, so the exact microseconds a burst puts on the wire — and every
// malformed edge — stay native testable. The module owns the RMT backend and
// turns a Step into hardware items.
//
// Protocol timings are synthesized here rather than produced by the blocking
// IRremoteESP8266 software bit-bang, because that library call delays for the
// whole waveform. Each burst is a complete mark/space sequence (including the
// protocol's mandatory repeat frames), which the module streams through the
// ESP32 RMT peripheral so loop() never waits for the waveform duration. A raw
// waveform is sent as ONE RMT burst, so replay timing stays gap-free.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/ir_record.h"

namespace irTx {

// Protocols synthesizable with the IRremoteESP8266 library. Raw replays the
// captured timing buffer instead of a protocol encoder. This is the exact
// supported set: RC5, RC6 and JVC/etc. are not claimed. Sony is supported at all
// three SIRC widths (SIRC-12, SIRC-15, SIRC-20).
enum class Protocol : uint8_t { Nec, Sony, Rc5, Samsung, Panasonic, Count };

// Errors double as synchronous command errors (invalid params) and, when a
// stored replay turns out untransmittable, the module's async failure reason.
enum class Error : uint8_t {
    None,
    UnsupportedProtocol,
    MalformedProtocol,
    InvalidCode,
    ValueOutOfRange,
    InvalidBits,
    InvalidFrequency,
    MissingRequired,
    UnexpectedParam,
    MalformedRaw,
    TooLong,
    Empty,
};

const char *protocolName(Protocol protocol);              // "NEC" | ... | "RAW"
const char *errorName(Error error);                       // stable lowercase wire name
bool parseProtocol(std::string_view name, Protocol &out); // case-insensitive; "RAW" -> false (raw is not a codec)

// Hand-entered code limits (T42).
constexpr uint16_t kMinBits = 1;
constexpr uint16_t kMaxBits = 64;
constexpr uint32_t kMinFrequencyHz = 30000;
constexpr uint32_t kMaxFrequencyHz = 60000;
constexpr uint32_t kDefaultFrequencyHz = 38000;

// One protocol-level frame; frequencyHz == 0 means "protocol default" (the
// library's per-protocol carrier; a hand-entered frequency only affects RAW).
struct ProtocolCode {
    Protocol protocol = Protocol::Nec;
    uint64_t value = 0;
    uint16_t bits = 0;
    uint32_t frequencyHz = 0;
};

// Carrier the RMT backend must generate for a protocol. Values are the
// IRremoteESP8266 defaults the frames were validated against.
struct Carrier {
    uint32_t frequencyHz;
    uint8_t dutyPercent;
};
Carrier protocolCarrier(Protocol protocol);

// Exact data-bit sizes each protocol defines. The WebSocket schema allows any
// 1..64 bits; a protocol frame with any other size is not decodable by a real
// receiver (the library's own strict decoders accept only these), so
// buildCustom() rejects it as invalid_bits instead of emitting a malformed
// frame. NEC/Samsung/Panasonic are fixed-width; Sony supports the three SIRC
// widths (12/15/20); RC5 (not RC5X) is 12 data bits.
bool protocolSupportsBits(Protocol protocol, uint16_t bits);

// Raw timing limits. A recorded/raw timing is stored as uint16_t microseconds
// (the capture record's unit); kMaxRawTxDurationUs bounds one burst so a replay
// cannot tie up the RMT channel for an unbounded time. The RMT backend splits a
// timing above its 15-bit item limit, so kMaxTimingUs stays representable.
constexpr size_t kMaxTimings = irRecord::kMaxTimings; // 512
constexpr uint16_t kMaxTimingUs = 65535;
constexpr uint32_t kMaxRawTxDurationUs = 500000;      // 500 ms per burst
// A RAW param string is bound by the WebSocket schema (kMaxParamStringBytes).
constexpr size_t kMaxRawParamBytes = 64;

// A complete on-air burst: alternating mark (index even) / space (index odd)
// microseconds, starting with a mark. Protocols with mandatory repeats (Sony
// repeats its frame twice, matching the library's minRepeats) already contain
// every repeated frame and the inter-frame gap. Durations are uint32_t because
// a protocol's trailing/gap padding can exceed 16 bits (e.g. Panasonic pads to
// 163296 us); the RMT backend splits anything above its 15-bit item limit.
struct Burst {
    uint32_t timings[kMaxTimings] = {};
    uint16_t count = 0;
    uint32_t carrierHz = kDefaultFrequencyHz;
    uint8_t dutyPercent = 33;
};

// T41/T42: synthesize the exact burst for one protocol code. Rejects an
// unsupported bit size (invalid_bits) and a value wider than `bits`
// (value_out_of_range); leaves `out` untouched on failure.
Error buildProtocolBurst(const ProtocolCode &code, Burst &out);

struct Waveform {
    uint16_t timings[kMaxTimings] = {};
    uint16_t count = 0;    // number of mark/space entries, >= 1
    uint32_t totalUs = 0;  // sum of timings, verified <= kMaxRawTxDurationUs
};

// T40: convert a validated capture record into a transmittable waveform. Rejects
// an empty record, a timing above kMaxTimingUs (not representable by the
// library) and a total duration above kMaxRawTxDurationUs, leaving `out`
// untouched on failure so a stored record is never partially converted.
Error buildReplayWaveform(const irRecord::Record &record, Waveform &out);

// T42 RAW: parse "9000,4500,560,560" (ASCII decimal microseconds, optional
// spaces around values). Bounded by kMaxRawParamBytes, kMaxTimings, kMaxTimingUs
// and kMaxRawTxDurationUs. No leading/trailing/duplicate comma or empty field.
Error parseRawWaveform(std::string_view text, Waveform &out);

// T42 code: parse an optional-0x hex literal (1..16 digits) into `value`.
Error parseHexCode(std::string_view text, uint64_t &value);

// T42 entry point: presence flags come from ActionParams (the schema cannot
// express "required unless protocol == RAW"). Validates the whole request and
// fills `out`; leaves it untouched on failure.
struct CustomRequest {
    bool raw = false;                 // true => use waveform+frequencyHz
    ProtocolCode code;                // !raw
    Waveform waveform;                // raw
    uint32_t frequencyHz = kDefaultFrequencyHz; // raw carrier
};
Error buildCustom(std::string_view protocolNameArg, bool hasCode, std::string_view codeHex, bool hasBits,
                  int64_t bits, bool hasFrequency, int64_t frequencyHz, bool hasRaw, std::string_view rawText,
                  CustomRequest &out);

// T41: TV-B-Gone power codes. Independently authored from public protocol-level
// references (protocol formats + widely published power codes); NOT copied from
// any AGPL database. Exact supported set (not exhaustive):
//   0 LG        NEC       0x20DF10EF       32-bit power toggle
//   1 Toshiba   NEC       0x02FD48B7       32-bit power toggle
//   2 Samsung   SAMSUNG   0xE0E040BF       32-bit power toggle
//   3 Sony      SONY      0x00000A90       12-bit power (command 21)
//   4 Philips   RC5       0x0000000C       12-bit power
//   5 Panasonic PANASONIC 0x40040100BCBD   48-bit power on/off
constexpr size_t kTvbgoneCount = 6;
const ProtocolCode &tvbgoneCode(size_t index);
const char *tvbgoneModel(size_t index); // human-readable target family

// Step sequencing: the module pulls one bounded transmit step per poll, so a
// multi-frame TV-B-Gone run can be stopped between frames without ever entering
// a second module state machine.
enum class PlanKind : uint8_t { None, Protocol, Raw, Tvbgone };
struct Plan {
    PlanKind kind = PlanKind::None;
    ProtocolCode code{};                    // Protocol
    Waveform waveform{};                    // Raw
    uint32_t frequencyHz = kDefaultFrequencyHz; // Raw
    uint16_t cursor = 0;                    // Protocol/Raw/Tvbgone progress
};

enum class StepKind : uint8_t { None, Protocol, Raw, Done };
struct Step {
    StepKind kind = StepKind::None;
    ProtocolCode code{};          // Protocol
    const uint16_t *timings = nullptr; // Raw (points into Plan::waveform)
    uint16_t count = 0;           // Raw
    uint32_t frequencyHz = 0;     // Raw
};

// Advances `plan` and returns the next step. Done/None end the action; a
// Tvbgone plan yields exactly kTvbgoneCount Protocol steps in table order.
Step nextStep(Plan &plan);

// Payload builders (spec §7 style, validated JSON produced with ArduinoJson so
// the uint64 value never becomes a JS number). One result per action.
std::string replayToJson(const irRecord::Record &record, const Waveform &waveform);
std::string customProtocolToJson(const ProtocolCode &code);
std::string customRawToJson(size_t timings, uint32_t durationUs, uint32_t frequencyHz);
std::string tvbgoneToJson(size_t codesSent);

} // namespace irTx

// Pure PN532 host-protocol framing. No Wire/I2C here: the module owns the bus
// and calls these to build commands and validate responses, so the frame edges
// are testable on native.
//
// Normal frame: 00 00 FF LEN LCS | TFI DATA... DCS | 00
//   LEN counts TFI+command+payload (excludes LCS/DCS); total bytes = 7 + LEN.
// UID response (D5 4B): NbTg Tg SENS_RES(2) SEL_RES NFCIDLen NFCID1[]
//   LEN = 8 + uidLen, so total = 15 + uidLen (<= 25).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace nfcFrame {

constexpr size_t kMaxFrameBytes = 32; // 4/7/10-byte UID or 16-byte read payload
constexpr size_t kMaxUidLength = 10;

enum class ParseStatus : uint8_t { NoTarget, Uid, Malformed };

// Response code for InDataExchange (command 0x40 + 1).
constexpr uint8_t kResponseInDataExchange = 0x41;

// Validates the 6-byte header (preamble, LEN/LCS sum, TFI D5) and reports the
// full frame size 7+LEN. Does not require the full body to be present.
bool parseResponseHeader(const uint8_t *frame, size_t available, uint16_t &totalLength);

// Full validation of an InListPassiveTarget response: checksum, postamble,
// target count/number, UID length (4/7/10) and advertised frame size. Writes
// uid/uidLength only for ParseStatus::Uid.
ParseStatus parseUidFrame(const uint8_t *frame, size_t available, std::array<uint8_t, kMaxUidLength> &uid,
                          uint8_t &uidLength);

// 00 00 FF 00 FF 00
bool isAckFrame(const uint8_t *frame, size_t available);

// Builds D4 frames. Returns the byte count, or 0 if capacity is too small.
size_t buildCommandFrame(const uint8_t *command, size_t commandLength, uint8_t *out, size_t capacity);
// InListPassiveTarget(MaxTg=1, BrTy=106kbps Type A).
size_t buildReadUidCommand(uint8_t *out, size_t capacity);

// InDataExchange(card, mifareData...): wraps a raw ISO14443-3A/MIFARE command
// (0x30 READ, 0xA0 WRITE, 0xA2 UL WRITE, 0x60/0x61 auth) into a D4 40 frame.
size_t buildInDataExchange(uint8_t card, const uint8_t *data, size_t dataLength, uint8_t *out, size_t capacity);

// Parses a D5 41 response: validates the header/checksum, writes the PN532
// status byte and the trailing response data (may be empty). Returns false on
// a malformed frame.
bool parseInDataExchange(const uint8_t *frame, size_t available, uint8_t &status, uint8_t *data, uint8_t &dataLength);

} // namespace nfcFrame

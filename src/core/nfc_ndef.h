// Portable NFC Forum Type 2 Tag TLV + NDEF (Text record) logic. No Arduino and
// no I2C here: the PN532 module owns the bus and calls these for layout,
// encoding, bounds and verification checks, so the whole byte layout is
// native-testable.
//
// References studied for technique only (original code, nothing copied):
//   - NFC Forum "NDEF 1.0": record header (MB/ME/CF/SR/IL/TNF), 1- or 4-byte
//     PAYLOAD_LENGTH, well-known Text record (TNF 0x1, type 'T', status byte).
//   - NFC Forum "Type 2 Tag Operation 1.1": NDEF Message TLV (T=0x03),
//     Terminator (T=0xFE), NULL/Lock Control/Memory Control TLVs, and the
//     0xFF long length form for the NDEF TLV.
//   - Adafruit PN532 ntag2xx_WriteNDEFURI(): command byte / TLV layout only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nfcNdef {

// Catalog cap for the nfc_write_ndef "text" parameter (UTF-8 bytes).
constexpr size_t kMaxTextBytes = 64;

// Language code written into the Text record status byte (UTF-8, "en").
constexpr char kLanguage[] = "en";
constexpr size_t kLanguageLength = 2;

// Well-known Text record: TNF 0x1 + type 'T' + UTF-8 status byte. Encodes the
// complete NDEF message into `out`; returns the byte count, or 0 when the text
// is empty/over the cap or the capacity is too small.
size_t encodeTextRecord(std::string_view text, uint8_t *out, size_t capacity);

// Decodes the first record of an NDEF message. Only a well-known Text record
// with a UTF-8 payload (status bit 7 clear) decodes; returns false for a
// malformed message or a non-Text record.
bool decodeTextRecord(const uint8_t *message, size_t length, std::string &language, std::string &text);

// Location of a Type 2 Tag TLV inside the data area.
struct Tlv {
    size_t offset = 0; // offset of the tag (T) byte
    size_t header = 0; // tag + length bytes (2, or 4 for the 0xFF long form)
    size_t length = 0; // declared payload length
};

// Scans the data area for the NDEF Message TLV (0x03), skipping NULL (0x00),
// Lock Control (0x01), Memory Control (0x02) and any other length-prefixed
// TLV. Stops at the Terminator (0xFE). The declared length is returned even
// when the payload itself extends past `length`, so a caller can read more.
// Returns false when no well-formed NDEF TLV exists before the end.
bool findNdefTlv(const uint8_t *data, size_t length, Tlv &out);

// Byte length of the leading TLVs (Lock/Memory Control, NULL, ...) before the
// NDEF Message TLV, or before the Terminator/end when NDEF is absent. Used to
// preserve a tag's fixed layout when rewriting. Malformed input yields 0.
size_t prefixLength(const uint8_t *data, size_t length);

// Verdict for the Type 2 leading control TLVs (the prefix before the NDEF
// Message TLV / Terminator, i.e. the bytes prefixLength() covers).
enum class ControlTlvVerdict : uint8_t {
    Ok,             // no intersecting control TLV: every mapped lock/reserved
                    // region lies outside the planned write range
    ReservedRegion, // a Memory Control TLV reserves bytes that intersect the
                    // planned write range: a blind write could clobber them
    DangerousLock,  // a Lock Control TLV maps lock bytes into the planned write
                    // range (or is malformed): writing could overwrite them
};

// Scans the prefix for control TLVs that make a write/erase unsafe over
// [writeStartByte, writeEndByte) of tag memory. Positions are decoded with the
// NFC Forum page granularity (T2TOP 1.1, RQ_T2T_MEM_023/028):
//   byteAddress = (position >> 4) * 2^(pageControl & 0x0F) + (position & 0x0F)
// where BytesPerPage is the LOW nibble of the page-control byte (a 0 nibble is
// RFU -> reject), and the lock/reserved byte count is ceil(sizeBits / 8) (Size
// 0x00 means 256). A standard NTAG Lock Control TLV pointing at the fixed
// static-lock area or the variant dynamic-lock page is therefore Ok and must be
// preserved by the caller (it is already part of the copied prefix). Malformed
// control TLVs reject.
ControlTlvVerdict scanControlTlvs(const uint8_t *data, size_t length, size_t writeStartByte, size_t writeEndByte);

// Result of composing a full Type 2 data area (prefix + NDEF TLV + Terminator).
struct AreaPlan {
    size_t totalBytes = 0; // bytes to write starting at the user page
    size_t ndefOffset = 0; // offset of the encoded NDEF message inside the area
    size_t ndefLength = 0; // encoded NDEF message length
};

// Composes `prefixBytes` of `prefix` + NDEF Message TLV + Terminator. Returns
// the area size, or 0 when it does not fit `capacity`.
size_t encodeArea(const uint8_t *prefix, size_t prefixBytes, const uint8_t *message, size_t messageBytes,
                  uint8_t *out, size_t capacity, AreaPlan &plan);

// Composes an erase area of `regionBytes` bytes: an empty NDEF Message TLV
// (03 00 FE) followed by zero fill, so a reader sees an empty tag while the
// old payload bytes are overwritten. Requires regionBytes >= 3.
size_t encodeEmptyArea(size_t regionBytes, uint8_t *out, size_t capacity);

// Type 2 pages are 4 bytes; rounds `bytes` up to a page boundary.
size_t pageAligned(size_t bytes);

// Pages needed for `bytes` (0 for 0 bytes).
size_t pagesFor(size_t bytes);

} // namespace nfcNdef

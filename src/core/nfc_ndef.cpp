#include "nfc_ndef.h"

namespace nfcNdef {

namespace {

constexpr uint8_t kTnfWellKnown = 0x01;
constexpr uint8_t kTypeText = 'T';
constexpr uint8_t kTlvLockControl = 0x01;
constexpr uint8_t kTlvMemoryControl = 0x02;
constexpr uint8_t kTlvNdefMessage = 0x03;
constexpr uint8_t kTlvTerminator = 0xFE;
constexpr uint8_t kLongLengthMarker = 0xFF;

// Reads a TLV length. `at` points at the length byte; `header` receives the
// tag+length byte count. Returns false when the length bytes do not fit.
bool readLength(const uint8_t *data, size_t length, size_t at, size_t tagBytes, size_t &payloadLength,
                size_t &header) {
    if (at >= length) return false;
    if (data[at] == kLongLengthMarker) {
        if (at + 2 >= length) return false;
        payloadLength = (static_cast<size_t>(data[at + 1]) << 8) | data[at + 2];
        header = tagBytes + 3;
        return true;
    }
    payloadLength = data[at];
    header = tagBytes + 1;
    return true;
}

// True when [aStart, aStart+aLen) intersects [bStart, bEnd); zero-length ranges
// never intersect.
bool rangesOverlap(size_t aStart, size_t aLen, size_t bStart, size_t bEnd) {
    if (aLen == 0) return false;
    const size_t aEnd = aStart + aLen;
    return aStart < bEnd && aEnd > bStart;
}

} // namespace

size_t encodeTextRecord(std::string_view text, uint8_t *out, size_t capacity) {
    if (out == nullptr || text.empty() || text.size() > kMaxTextBytes) return 0;
    const size_t payloadLength = 1 + kLanguageLength + text.size();
    if (payloadLength > 0xFF) return 0; // short record only (max here is 67)
    const size_t total = 4 + payloadLength;
    if (capacity < total) return 0;

    // MB=1, ME=1, CF=0, SR=1, IL=0, TNF=0x1 -> 0xD1.
    out[0] = 0xD1;
    out[1] = 0x01; // TYPE_LENGTH
    out[2] = static_cast<uint8_t>(payloadLength);
    out[3] = kTypeText;
    out[4] = static_cast<uint8_t>(kLanguageLength); // UTF-8 (bit 7 clear)
    for (size_t i = 0; i < kLanguageLength; ++i) out[5 + i] = static_cast<uint8_t>(kLanguage[i]);
    for (size_t i = 0; i < text.size(); ++i) out[5 + kLanguageLength + i] = static_cast<uint8_t>(text[i]);
    return total;
}

bool decodeTextRecord(const uint8_t *message, size_t length, std::string &language, std::string &text) {
    language.clear();
    text.clear();
    if (message == nullptr || length < 4) return false;

    const uint8_t header = message[0];
    const uint8_t tnf = header & 0x07;
    const bool shortRecord = (header & 0x10) != 0;
    const bool hasId = (header & 0x08) != 0;
    if (tnf != kTnfWellKnown) return false;

    const uint8_t typeLength = message[1];
    size_t cursor = 2;
    size_t payloadLength = 0;
    if (shortRecord) {
        if (cursor >= length) return false;
        payloadLength = message[cursor++];
    } else {
        if (cursor + 4 > length) return false;
        payloadLength = (static_cast<size_t>(message[cursor]) << 24) | (static_cast<size_t>(message[cursor + 1]) << 16) |
                        (static_cast<size_t>(message[cursor + 2]) << 8) | message[cursor + 3];
        cursor += 4;
    }
    size_t idLength = 0;
    if (hasId) {
        if (cursor >= length) return false;
        idLength = message[cursor++];
    }
    if (cursor + typeLength > length) return false;
    if (typeLength != 1 || message[cursor] != kTypeText) return false;
    cursor += typeLength;
    if (cursor + idLength > length) return false;
    cursor += idLength;
    if (cursor + payloadLength > length) return false;
    if (payloadLength < 1) return false;

    const uint8_t status = message[cursor];
    if ((status & 0x80) != 0) return false; // UTF-16 is out of scope for this build
    const size_t languageLength = status & 0x3F;
    if (1 + languageLength > payloadLength) return false;
    language.assign(reinterpret_cast<const char *>(message + cursor + 1), languageLength);
    text.assign(reinterpret_cast<const char *>(message + cursor + 1 + languageLength),
                payloadLength - 1 - languageLength);
    return true;
}

bool findNdefTlv(const uint8_t *data, size_t length, Tlv &out) {
    out = Tlv{};
    if (data == nullptr) return false;
    size_t i = 0;
    while (i < length) {
        const uint8_t tag = data[i];
        if (tag == 0x00) { // NULL TLV: one byte of padding
            ++i;
            continue;
        }
        if (tag == kTlvTerminator) return false;
        if (tag == kTlvNdefMessage) {
            size_t payload = 0, header = 0;
            if (!readLength(data, length, i + 1, 1, payload, header)) return false;
            out.offset = i;
            out.header = header;
            out.length = payload;
            return true;
        }
        size_t payload = 0, header = 0;
        if (!readLength(data, length, i + 1, 1, payload, header)) return false;
        i += header + payload;
    }
    return false;
}

size_t prefixLength(const uint8_t *data, size_t length) {
    if (data == nullptr) return 0;
    size_t i = 0;
    while (i < length) {
        const uint8_t tag = data[i];
        if (tag == kTlvNdefMessage || tag == kTlvTerminator) return i;
        if (tag == 0x00) {
            ++i;
            continue;
        }
        size_t payload = 0, header = 0;
        if (!readLength(data, length, i + 1, 1, payload, header)) return i;
        i += header + payload;
    }
    return length; // no NDEF/terminator found: everything read is fixed prefix
}

ControlTlvVerdict scanControlTlvs(const uint8_t *data, size_t length, size_t writeStartByte, size_t writeEndByte) {
    if (data == nullptr) return ControlTlvVerdict::Ok;
    size_t i = 0;
    while (i < length) {
        const uint8_t tag = data[i];
        if (tag == 0x00) { // NULL TLV: padding
            ++i;
            continue;
        }
        if (tag == kTlvNdefMessage || tag == kTlvTerminator) break;
        size_t payload = 0, header = 0;
        if (!readLength(data, length, i + 1, 1, payload, header)) {
            return ControlTlvVerdict::DangerousLock; // malformed: refuse
        }
        if (i + header + payload > length) {
            // Value runs past what the caller showed us: cannot map it safely.
            return tag == kTlvMemoryControl ? ControlTlvVerdict::ReservedRegion
                                            : ControlTlvVerdict::DangerousLock;
        }
        const uint8_t *value = data + i + header;
        const uint8_t pageExponent = static_cast<uint8_t>(value[2] & 0x0F); // BytesPerPage (low nibble)
        if (pageExponent == 0) {
            // BytesPerPage 0h is RFU: the address cannot be decoded safely.
            return tag == kTlvMemoryControl ? ControlTlvVerdict::ReservedRegion
                                            : ControlTlvVerdict::DangerousLock;
        }
        const size_t pageSize = static_cast<size_t>(1) << pageExponent;
        const size_t address = static_cast<size_t>(value[0] >> 4) * pageSize + (value[0] & 0x0F);
        if (tag == kTlvLockControl) {
            // Value: Position (PageAddr<<4 | ByteOffset), Size (lock bits;
            // 00h = 256), PageControl (BytesPerPage low nibble). byteAddress =
            // PageAddr * 2^BytesPerPage + ByteOffset; lock bytes = ceil(bits/8).
            // Only an overlap with the planned writes is unsafe: a standard NTAG
            // TLV pointing at the static-lock area or the dynamic-lock page lies
            // outside the user area and is benign.
            if (payload < 3) return ControlTlvVerdict::DangerousLock;
            const size_t bits = value[1] == 0 ? 256 : value[1];
            const size_t lockBytes = (bits + 7) / 8;
            if (rangesOverlap(address, lockBytes, writeStartByte, writeEndByte)) {
                return ControlTlvVerdict::DangerousLock;
            }
        } else if (tag == kTlvMemoryControl) {
            // Value: Position (PageAddr<<4 | ByteOffset), Size (reserved bytes;
            // 00h = 256), Partial Page Control (BytesPerPage low nibble). Only
            // an overlap with the planned writes is unsafe.
            if (payload < 3) return ControlTlvVerdict::ReservedRegion;
            const size_t reserved = value[1] == 0 ? 256 : value[1];
            if (rangesOverlap(address, reserved, writeStartByte, writeEndByte)) {
                return ControlTlvVerdict::ReservedRegion;
            }
        }
        i += header + payload;
    }
    return ControlTlvVerdict::Ok;
}

size_t encodeArea(const uint8_t *prefix, size_t prefixBytes, const uint8_t *message, size_t messageBytes,
                  uint8_t *out, size_t capacity, AreaPlan &plan) {
    plan = AreaPlan{};
    if (prefixBytes > 0 && prefix == nullptr) return 0;
    if (messageBytes > 0 && message == nullptr) return 0;
    const size_t header = messageBytes < kLongLengthMarker ? 2 : 4;
    const size_t total = prefixBytes + header + messageBytes + 1; // + terminator
    if (capacity < total) return 0;

    size_t cursor = 0;
    for (size_t i = 0; i < prefixBytes; ++i) out[cursor++] = prefix[i];
    out[cursor++] = kTlvNdefMessage;
    if (header == 2) {
        out[cursor++] = static_cast<uint8_t>(messageBytes);
    } else {
        out[cursor++] = kLongLengthMarker;
        out[cursor++] = static_cast<uint8_t>(messageBytes >> 8);
        out[cursor++] = static_cast<uint8_t>(messageBytes & 0xFF);
    }
    plan.ndefOffset = cursor;
    plan.ndefLength = messageBytes;
    for (size_t i = 0; i < messageBytes; ++i) out[cursor++] = message[i];
    out[cursor++] = kTlvTerminator;
    plan.totalBytes = cursor;
    return cursor;
}

size_t encodeEmptyArea(size_t regionBytes, uint8_t *out, size_t capacity) {
    if (out == nullptr || regionBytes < 3 || capacity < regionBytes) return 0;
    out[0] = kTlvNdefMessage;
    out[1] = 0x00;
    out[2] = kTlvTerminator;
    for (size_t i = 3; i < regionBytes; ++i) out[i] = 0x00;
    return regionBytes;
}

size_t pageAligned(size_t bytes) { return (bytes + 3) & ~static_cast<size_t>(3); }

size_t pagesFor(size_t bytes) { return bytes == 0 ? 0 : pageAligned(bytes) / 4; }

} // namespace nfcNdef

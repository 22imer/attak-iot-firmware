#include "nfc_frame.h"

namespace nfcFrame {

namespace {

constexpr uint8_t kTfiPn532 = 0xD5;
constexpr uint8_t kTfiHost = 0xD4;
constexpr uint8_t kCmdInListPassiveTarget = 0x4A;
constexpr uint8_t kRspInListPassiveTarget = 0x4B; // response code = command + 1
constexpr size_t kHeaderBytes = 6; // 00 00 FF LEN LCS TFI

bool sumIsZero(const uint8_t *data, size_t length) {
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) sum = static_cast<uint8_t>(sum + data[i]);
    return sum == 0;
}

} // namespace

bool parseResponseHeader(const uint8_t *frame, size_t available, uint16_t &totalLength) {
    if (available < kHeaderBytes) return false;
    if (frame[0] != 0x00 || frame[1] != 0x00 || frame[2] != 0xFF) return false;
    const uint8_t len = frame[3];
    if (static_cast<uint8_t>(len + frame[4]) != 0x00) return false;
    if (frame[5] != kTfiPn532) return false;

    totalLength = static_cast<uint16_t>(7 + len); // 00 00 FF LEN LCS body DCS 00
    return true;
}

ParseStatus parseUidFrame(const uint8_t *frame, size_t available, std::array<uint8_t, kMaxUidLength> &uid,
                          uint8_t &uidLength) {
    uint16_t total = 0;
    if (!parseResponseHeader(frame, available, total)) return ParseStatus::Malformed;
    if (available < total || total > kMaxFrameBytes) return ParseStatus::Malformed;

    const uint8_t len = frame[3];
    if (len < 3) return ParseStatus::Malformed;
    if (frame[6] != kRspInListPassiveTarget) return ParseStatus::Malformed;
    if (frame[6 + len] != 0x00) return ParseStatus::Malformed;       // postamble
    if (!sumIsZero(frame + 5, static_cast<size_t>(len) + 1)) return ParseStatus::Malformed; // TFI..DCS

    const uint8_t targetCount = frame[7];
    if (targetCount == 0) return len == 3 ? ParseStatus::NoTarget : ParseStatus::Malformed;
    if (targetCount != 1) return ParseStatus::Malformed;
    if (frame[8] != 0x01) return ParseStatus::Malformed; // logical target number

    const uint8_t uidLen = frame[12];
    if (uidLen != 4 && uidLen != 7 && uidLen != 10) return ParseStatus::Malformed;
    if (len != static_cast<uint8_t>(8 + uidLen)) return ParseStatus::Malformed;
    if (static_cast<size_t>(13) + uidLen > total) return ParseStatus::Malformed;

    for (uint8_t i = 0; i < uidLen; ++i) uid[i] = frame[13 + i];
    uidLength = uidLen;
    return ParseStatus::Uid;
}

bool isAckFrame(const uint8_t *frame, size_t available) {
    if (available < 6) return false;
    return frame[0] == 0x00 && frame[1] == 0x00 && frame[2] == 0xFF && frame[3] == 0x00 && frame[4] == 0xFF &&
           frame[5] == 0x00;
}

size_t buildCommandFrame(const uint8_t *command, size_t commandLength, uint8_t *out, size_t capacity) {
    const size_t total = 7 + 1 + commandLength; // 00 00 FF LEN LCS D4 ... DCS 00
    if (capacity < total) return 0;

    const uint8_t len = static_cast<uint8_t>(1 + commandLength); // TFI + command
    out[0] = 0x00;
    out[1] = 0x00;
    out[2] = 0xFF;
    out[3] = len;
    out[4] = static_cast<uint8_t>(0x100 - len); // LCS
    out[5] = kTfiHost;

    uint8_t sum = kTfiHost;
    for (size_t i = 0; i < commandLength; ++i) {
        out[6 + i] = command[i];
        sum = static_cast<uint8_t>(sum + command[i]);
    }
    out[6 + commandLength] = static_cast<uint8_t>(0x100 - sum); // DCS
    out[7 + commandLength] = 0x00;                              // postamble
    return total;
}

size_t buildReadUidCommand(uint8_t *out, size_t capacity) {
    const uint8_t command[] = {kCmdInListPassiveTarget, 0x01, 0x00}; // MaxTg=1, Type A 106 kbps
    return buildCommandFrame(command, sizeof(command), out, capacity);
}

size_t buildInDataExchange(uint8_t card, const uint8_t *data, size_t dataLength, uint8_t *out, size_t capacity) {
    constexpr uint8_t kCmdInDataExchange = 0x40;
    if (dataLength > 0 && data == nullptr) return 0;
    uint8_t command[1 + 1 + 20]; // D4 payload: 0x40, card, then <=16 data + 4 auth bytes
    if (2 + dataLength > sizeof(command)) return 0;
    command[0] = kCmdInDataExchange;
    command[1] = card;
    for (size_t i = 0; i < dataLength; ++i) command[2 + i] = data[i];
    return buildCommandFrame(command, 2 + dataLength, out, capacity);
}

bool parseInDataExchange(const uint8_t *frame, size_t available, uint8_t &status, uint8_t *data, uint8_t &dataLength) {
    status = 0;
    dataLength = 0;
    uint16_t total = 0;
    if (!parseResponseHeader(frame, available, total)) return false;
    if (available < total || total > kMaxFrameBytes) return false;
    const uint8_t len = frame[3];
    if (len < 3) return false; // TFI + cmd + status
    if (frame[6] != kResponseInDataExchange) return false;
    if (frame[6 + len] != 0x00) return false; // postamble
    if (!sumIsZero(frame + 5, static_cast<size_t>(len) + 1)) return false;
    status = frame[7];
    const uint8_t payload = static_cast<uint8_t>(len - 3); // LEN counts TFI + cmd + status
    if (static_cast<size_t>(8) + payload > total) return false;
    for (uint8_t i = 0; i < payload; ++i) data[i] = frame[8 + i];
    dataLength = payload;
    return true;
}

} // namespace nfcFrame

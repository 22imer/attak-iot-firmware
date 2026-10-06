// PN532 (NFC) module. Owns the I2C bus and implements the host protocol for
// read_uid plus the T30..T33 payloads:
//   nfc_read_dump  — finite memory dump (MIFARE Classic blocks / Type 2 pages)
//                    retained in the ModuleRuntime RecordBuffer.
//   nfc_clone_uid  — writes a recorded dump's UID onto a compatible
//                    writable-UID (CUID/Gen2 magic) card, then verifies it.
//   nfc_write_ndef — writes an NFC Forum Type 2 NDEF Text record and verifies.
//   nfc_erase      — safe user-area erase (Classic data blocks / Type 2 NDEF
//                    region), verified.
//
// Protocol references studied for technique only (original code, nothing
// copied): NXP PN532 User Manual UM0701-02 (frame/checksum, InListPassiveTarget
// §7.3.4, InDataExchange §7.3.8), Adafruit_PN532 (MIFARE command bytes and the
// Type 2 page layout), NFC Forum Type 2 Tag / NDEF specs (see nfc_ndef.h),
// libnfc/Proxmark3 magic-card notes (Gen2/CUID direct block-0 write).
//
// One chip command per poll() at most (strictly bounded by kCommandBudgetMs),
// so the cooperative loop is never blocked. Every action has a finite deadline.

#include "pn532_module.h"

#include <Wire.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "board_pins.h"
#include "core/module_runtime.h"
#include "core/nfc_frame.h"
#include "core/nfc_ndef.h"
#include "core/nfc_tag.h"
#include "core/nfc_uid_format.h"

namespace pn532 {

namespace {

ModuleRuntime runtime("pn532");

constexpr uint8_t kI2cAddress = 0x24; // PN532 I2C address (0x48 >> 1)
constexpr uint8_t kReady = 0x01;      // RDY status byte
constexpr uint8_t kI2cTimeoutMs = 20;
constexpr uint32_t kHealthRecheckMs = 500;
// One host-protocol transact per poll(), strictly bounded so the cooperative
// loop and the WebSocket path are never blocked; never the multi-second window.
constexpr uint32_t kCommandBudgetMs = 20;
constexpr uint32_t kProbeIntervalMs = 100;
constexpr size_t kMaxFrame = nfcFrame::kMaxFrameBytes;

// Action deadlines (finite, wrap-safe; see ModuleRuntime::beginAction).
constexpr uint32_t kUidDeadlineMs = 5000;
constexpr uint32_t kDumpDeadlineMs = 30000;
constexpr uint32_t kCloneDeadlineMs = 15000;
constexpr uint32_t kWriteDeadlineMs = 20000;
constexpr uint32_t kEraseDeadlineMs = 30000;

constexpr uint8_t kAckFrame[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
constexpr uint8_t kGetFirmwareVersion[] = {0x02};
constexpr uint8_t kSamConfiguration[] = {0x14, 0x01, 0x14, 0x00}; // Normal mode, IRQ not driven
constexpr uint8_t kSetParameters[] = {0x12, 0x00};               // fAutomaticRATS off, no RATS/ATS
constexpr uint8_t kSetMaxRetries[] = {0x32, 0x05, 0xFF, 0x01, 0x00}; // MxRtyPassiveActivation = one try
constexpr uint8_t kInListPassiveTarget[] = {0x4A, 0x01, 0x00};

// Raw MIFARE / Type 2 commands carried inside InDataExchange.
constexpr uint8_t kMifareAuthA = 0x60;
constexpr uint8_t kMifareRead = 0x30;
constexpr uint8_t kMifareWrite = 0xA0;
constexpr uint8_t kUltralightWrite = 0xA2;

constexpr uint16_t kType2MaxPages = 231; // NTAG216 upper bound for discovery
constexpr size_t kPrefixReadBytes = 16;   // one READ = 4 pages
constexpr size_t kNdefAreaBytes = 1024;   // NDEF/erase area (<= 888-byte user area)

// Public/documented default MIFARE keys (auth attempts only; nothing is
// cracked here). Multiple keys let a dump read default-formatted tags.
constexpr uint8_t kKeys[][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},
    {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};
constexpr uint8_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);

enum class ReadyState : uint8_t { Ready, Busy, IoError };
enum class Xfer : uint8_t { Ok, Timeout, IoError, ProtocolError };

enum class Phase : uint8_t {
    Idle,
    Select,
    CheckStable,       // re-select and confirm the same tag is still present
    DumpRead,          // Type 2: read 4 pages per step
    DumpAuth,          // Classic: authenticate current sector
    DumpReadBlock,     // Classic: read one block
    DumpFinish,
    NdefCc,            // read page 0/CC (+ static lock)
    NdefDynLock,       // read the variant's dynamic lock page
    NdefPrefix,        // read page 4 (TLV prefix / existing NDEF)
    NdefWrite,         // write one page of the composed area
    NdefVerify,        // read one page back and compare
    EraseClassicWrite, // write one zeroed data block
    EraseClassicVerify,// read one data block back, require zeros
    CloneAuth,         // authenticate sector 0 before touching block 0
    CloneReadClassic,  // read the target's block 0 (preserve manufacturer bytes)
    CloneReadType2,    // read the target's pages 0..2 (preserve lock/internal)
    CloneWriteClassic, // write the spliced block 0
    CloneWriteType2,   // write the spliced pages 0..N
    CloneVerify,       // re-select and compare the UID
};

struct Scratch {
    Phase phase = Phase::Idle;
    ActionId action = ActionId::None;
    uint32_t ticket = 0;
    uint32_t lastStepMs = 0;

    nfcTag::Identity id;
    nfcTag::Family family = nfcTag::Family::Unknown;
    nfcTag::Variant variant = nfcTag::Variant::Unknown;
    bool selected = false;

    uint8_t units[RecordBuffer::capacity] = {};
    uint16_t unitCount = 0;
    uint16_t unitCursor = 0;
    bool holes = false;
    bool truncated = false;

    uint8_t sector = 0;
    uint8_t keyIndex = 0;
    uint16_t blockCursor = 0;
    bool authed = false;

    char text[kMaxParamStringBytes + 1] = {};
    size_t textLen = 0;
    uint8_t prefix[kPrefixReadBytes] = {};
    size_t prefixBytes = 0;
    uint8_t area[kNdefAreaBytes] = {};
    size_t areaBytes = 0;
    uint16_t pageCursor = 0;
    uint16_t pageCount = 0;
    size_t regionBytes = 0;
    bool hasNdef = false;

    nfcTag::Dump source;
    uint8_t cloneUnits[16] = {};
    uint8_t cloneUnitCount = 0;
    uint8_t cloneCursor = 0;

    Phase resumePhase = Phase::Idle; // phase CheckStable returns to
    bool cardChecked = false;        // current operation passed the stability check
};

Scratch g;
uint32_t activeTicket = 0;
uint32_t lastHealthMs = 0;
bool transactionOutstanding = false;

// ---------------------------------------------------------------------------
// Low-level I2C / frame transfer.
// ---------------------------------------------------------------------------

uint8_t i2cRead(uint8_t *buffer, uint8_t capacity) {
    const uint8_t got = static_cast<uint8_t>(Wire.requestFrom(kI2cAddress, capacity));
    uint8_t count = 0;
    while (count < got && Wire.available()) buffer[count++] = static_cast<uint8_t>(Wire.read());
    return count;
}

ReadyState readFrame(uint8_t *frame, uint8_t capacity, uint8_t &frameLength) {
    if (capacity > kMaxFrame) return ReadyState::IoError;
    uint8_t buffer[1 + kMaxFrame];
    const uint8_t count = i2cRead(buffer, static_cast<uint8_t>(capacity + 1));
    if (count == 0) return ReadyState::IoError;
    if (buffer[0] != kReady) return ReadyState::Busy;
    frameLength = static_cast<uint8_t>(count - 1);
    memcpy(frame, buffer + 1, frameLength);
    return ReadyState::Ready;
}

bool writeFrame(const uint8_t *frame, uint8_t length) {
    Wire.beginTransmission(kI2cAddress);
    Wire.write(frame, length);
    return Wire.endTransmission() == 0;
}

// Blocking but strictly bounded: writes one prebuilt frame, then waits for the
// ACK and the response inside `budgetMs`. Never spans the whole action window.
Xfer runCommandFrame(const uint8_t *frame, size_t frameLength, uint8_t *response, uint8_t &responseLength,
                     uint32_t budgetMs) {
    if (frameLength == 0 || frameLength > 0xFF) return Xfer::ProtocolError;
    if (!writeFrame(frame, static_cast<uint8_t>(frameLength))) return Xfer::IoError;

    const uint32_t deadline = millis() + budgetMs;
    responseLength = 0;
    uint8_t scratchFrame[kMaxFrame + 1];

    bool acked = false;
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        uint8_t length = 0;
        const ReadyState state = readFrame(scratchFrame, 6, length);
        if (state == ReadyState::IoError) return Xfer::IoError;
        if (state == ReadyState::Ready) {
            acked = nfcFrame::isAckFrame(scratchFrame, length);
            break;
        }
    }
    if (!acked) return Xfer::Timeout;

    while (static_cast<int32_t>(millis() - deadline) < 0) {
        uint8_t length = 0;
        const ReadyState state = readFrame(scratchFrame, static_cast<uint8_t>(kMaxFrame), length);
        if (state == ReadyState::IoError) return Xfer::IoError;
        if (state == ReadyState::Ready) {
            responseLength = length;
            memcpy(response, scratchFrame, length);
            return Xfer::Ok;
        }
    }
    return Xfer::Timeout;
}

Xfer runCommand(const uint8_t *command, uint8_t commandLength, uint8_t *response, uint8_t &responseLength,
                uint32_t budgetMs) {
    uint8_t frame[kMaxFrame + 1];
    const size_t frameLength = nfcFrame::buildCommandFrame(command, commandLength, frame, sizeof(frame));
    if (frameLength == 0) return Xfer::ProtocolError;
    return runCommandFrame(frame, frameLength, response, responseLength, budgetMs);
}

// Host ACK frame aborts the PN532's current process (UM0701-02 §6.2.2.2-b) and
// leaves it waiting for a new command.
bool abortTransaction() { return writeFrame(kAckFrame, sizeof(kAckFrame)); }

bool responseCode(const uint8_t *response, uint8_t length, uint8_t expectedCommand) {
    uint16_t total = 0;
    if (!nfcFrame::parseResponseHeader(response, length, total) || length < total) return false;
    return response[6] == expectedCommand;
}

// Wraps `payload` in InDataExchange and validates the D5 41 response.
Xfer inDataExchange(const uint8_t *payload, uint8_t payloadLength, uint8_t *data, uint8_t &dataLength,
                    uint8_t &status) {
    status = 0xFF;
    dataLength = 0;
    uint8_t frame[kMaxFrame + 1];
    const size_t frameLength = nfcFrame::buildInDataExchange(0x01, payload, payloadLength, frame, sizeof(frame));
    if (frameLength == 0) return Xfer::ProtocolError;
    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    const Xfer result = runCommandFrame(frame, frameLength, response, length, kCommandBudgetMs);
    if (result != Xfer::Ok) return result;
    if (!nfcFrame::parseInDataExchange(response, length, status, data, dataLength)) return Xfer::ProtocolError;
    return Xfer::Ok;
}

// Reads `pages`*4 bytes starting at page `page` (MIFARE READ, 4 pages/response).
Xfer readPages(uint8_t page, uint8_t *out, uint8_t &outLength, uint8_t &status) {
    const uint8_t payload[] = {kMifareRead, page};
    return inDataExchange(payload, sizeof(payload), out, outLength, status);
}

Xfer readBlock(uint16_t block, uint8_t *out, uint8_t &outLength, uint8_t &status) {
    const uint8_t payload[] = {kMifareRead, static_cast<uint8_t>(block)};
    return inDataExchange(payload, sizeof(payload), out, outLength, status);
}

Xfer writeBlock(uint16_t block, const uint8_t *data, uint8_t &status) {
    uint8_t payload[2 + 16] = {kMifareWrite, static_cast<uint8_t>(block)};
    memcpy(payload + 2, data, 16);
    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    status = 0xFF;
    return inDataExchange(payload, sizeof(payload), response, length, status);
}

Xfer writePage(uint8_t page, const uint8_t *data, uint8_t &status) {
    uint8_t payload[2 + 4] = {kUltralightWrite, page};
    memcpy(payload + 2, data, 4);
    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    status = 0xFF;
    return inDataExchange(payload, sizeof(payload), response, length, status);
}

// ---------------------------------------------------------------------------
// Actions.
// ---------------------------------------------------------------------------

char *hexInto(char *out, const uint8_t *data, size_t length) {
    static const char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        out[i * 2] = kHex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    out[length * 2] = '\0';
    return out;
}

std::string dumpJson(const nfcTag::Dump &dump) {
    char buffer[64];
    std::string json = "{\"kind\":\"nfc_dump\",\"family\":\"";
    json += nfcTag::familyName(dump.family);
    json += "\",\"variant\":\"";
    json += nfcTag::variantName(dump.variant);
    json += "\",\"uid\":\"";
    json += nfcUid::formatHex(dump.id.uid, dump.id.uidLength);
    json += "\",\"atqa\":\"";
    char atqa[8];
    snprintf(atqa, sizeof(atqa), "%02X%02X", dump.id.atqa[0], dump.id.atqa[1]);
    json += atqa;
    json += "\",\"sak\":\"";
    char sak[8];
    snprintf(sak, sizeof(sak), "%02X", dump.id.sak);
    json += sak;
    snprintf(buffer, sizeof(buffer), "\",\"unitSize\":%u,\"units\":%u,\"stored\":%u,\"truncated\":%s,",
             dump.unitSize, dump.storedUnits, dump.storedUnits, dump.truncated ? "true" : "false");
    json += buffer;
    snprintf(buffer, sizeof(buffer), "\"holes\":%s,\"data\":\"", dump.holes ? "true" : "false");
    json += buffer;
    if (dump.data != nullptr && dump.dataLength > 0) {
        // Heap-free bounded hex: chunk to keep peak allocation small.
        char chunk[129];
        for (size_t i = 0; i < dump.dataLength; i += 64) {
            const size_t n = dump.dataLength - i > 64 ? 64 : dump.dataLength - i;
            hexInto(chunk, dump.data + i, n);
            json += chunk;
        }
    }
    json += "\"}";
    return json;
}

std::string writeNdefJson() {
    char buffer[192];
    snprintf(buffer, sizeof(buffer),
             "{\"kind\":\"nfc_write_ndef\",\"family\":\"%s\",\"variant\":\"%s\",\"pages\":%u,\"bytes\":%u,"
             "\"textLen\":%u,\"verified\":true}",
             nfcTag::familyName(g.family), nfcTag::variantName(g.variant), static_cast<unsigned>(g.pageCount),
             static_cast<unsigned>(g.areaBytes), static_cast<unsigned>(g.textLen));
    return std::string(buffer);
}

std::string eraseJson(uint16_t units) {
    char buffer[192];
    snprintf(buffer, sizeof(buffer),
             "{\"kind\":\"nfc_erase\",\"family\":\"%s\",\"variant\":\"%s\",\"units\":%u,\"verified\":true}",
             nfcTag::familyName(g.family), nfcTag::variantName(g.variant), static_cast<unsigned>(units));
    return std::string(buffer);
}

std::string cloneJson() {
    char buffer[192];
    snprintf(buffer, sizeof(buffer),
             "{\"kind\":\"nfc_clone_uid\",\"family\":\"%s\",\"uid\":\"%s\",\"verified\":true}",
             nfcTag::familyName(g.family), nfcUid::formatHex(g.source.id.uid, g.source.id.uidLength).c_str());
    return std::string(buffer);
}

void resetScratch() {
    g = Scratch{};
    activeTicket = 0;
}

void failHardware(uint32_t now) {
    if (activeTicket != 0) {
        runtime.failAction(activeTicket, ActionError::HardwareError, now, transactionOutstanding);
    }
    transactionOutstanding = false;
    runtime.setHealth(false, "pn532 not responding", now);
    resetScratch();
}

void failTag(uint32_t now) {
    if (activeTicket != 0) runtime.failAction(activeTicket, ActionError::NfcTagError, now, false);
    transactionOutstanding = false;
    resetScratch();
}

void complete(std::string payload, uint32_t now) {
    runtime.completeAction(activeTicket, std::move(payload), now);
    transactionOutstanding = false;
    resetScratch();
}

uint8_t classicSectorCount(nfcTag::Variant variant) {
    if (variant == nfcTag::Variant::Classic1K) return 16;
    if (variant == nfcTag::Variant::Classic4K) return 40;
    return 0;
}

uint16_t classicSectorFirst(nfcTag::Variant variant, uint8_t sector) {
    if (variant == nfcTag::Variant::Classic4K && sector >= 32) return static_cast<uint16_t>(128 + (sector - 32) * 16);
    return static_cast<uint16_t>(sector * 4);
}

uint8_t classicSectorBlocks(nfcTag::Variant variant, uint8_t sector) {
    return (variant == nfcTag::Variant::Classic4K && sector >= 32) ? 16 : 4;
}

uint16_t classicSectorOf(nfcTag::Variant variant, uint16_t block) {
    (void)variant;
    if (block < 128) return static_cast<uint16_t>(block / 4);
    return static_cast<uint16_t>(32 + (block - 128) / 16);
}

bool authenticateSector(uint16_t firstBlock, uint8_t keyIndex) {
    if (g.id.uidLength < 4) return false;
    // MIFARE Classic AUTH always carries the LAST four UID bytes, even when the
    // tag presents a 7-byte UID (ISO/IEC 14443-3 / MIFARE).
    const uint8_t *uidTail = g.id.uid + (g.id.uidLength - 4);
    uint8_t payload[2 + 6 + 4] = {kMifareAuthA, static_cast<uint8_t>(firstBlock)};
    memcpy(payload + 2, kKeys[keyIndex], 6);
    memcpy(payload + 8, uidTail, 4);
    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0, status = 0xFF;
    const Xfer result = inDataExchange(payload, sizeof(payload), response, length, status);
    transactionOutstanding = result == Xfer::Timeout;
    return result == Xfer::Ok && status == 0x00;
}

// Multi-write actions run over many poll()s; a badge swapped in midway would
// otherwise receive the rest of another tag's payload. Re-select and compare
// the UID against the tag the action started on. Returns Waiting when the card
// is momentarily absent (retry until the deadline) so a coincidental
// unreadable poll is not reported as a failure.
enum class Stability : uint8_t { Stable, Waiting, Failed };

Stability confirmCardStable(uint32_t now) {
    if (static_cast<uint32_t>(now - g.lastStepMs) < kProbeIntervalMs) return Stability::Waiting;
    g.lastStepMs = now;

    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    const Xfer result = runCommand(kInListPassiveTarget, sizeof(kInListPassiveTarget), response, length, kCommandBudgetMs);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return Stability::Failed;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return Stability::Waiting;
    }
    transactionOutstanding = false;

    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t uidLength = 0;
    switch (nfcFrame::parseUidFrame(response, length, uid, uidLength)) {
    case nfcFrame::ParseStatus::Uid:
        if (uidLength != g.id.uidLength || memcmp(uid.data(), g.id.uid, uidLength) != 0) {
            failTag(now); // the badge was swapped under the operation
            return Stability::Failed;
        }
        return Stability::Stable;
    case nfcFrame::ParseStatus::NoTarget:
        return Stability::Waiting;
    case nfcFrame::ParseStatus::Malformed:
        failHardware(now);
        return Stability::Failed;
    }
    return Stability::Waiting;
}

// ---------------------------------------------------------------------------
// State machine steps. Each performs at most one chip command.
// ---------------------------------------------------------------------------

void beginDump(uint32_t now) {
    if (g.family == nfcTag::Family::Type2) {
        memset(g.units, 0xFF, sizeof(g.units));
        g.unitCount = kType2MaxPages;
        g.unitCursor = 0;
        g.phase = Phase::DumpRead;
        return;
    }
    if (g.family == nfcTag::Family::MifareClassic) {
        g.variant = nfcTag::classicVariantFromSak(g.id.sak);
        if (g.variant == nfcTag::Variant::Unknown) {
            failTag(now);
            return;
        }
        memset(g.units, 0xFF, sizeof(g.units));
        g.unitCount = nfcTag::totalUnits(g.variant);
        g.sector = 0;
        g.keyIndex = 0;
        g.holes = false;
        g.phase = Phase::DumpAuth;
        return;
    }
    failTag(now);
}

void beginNdef(uint32_t now) {
    if (g.family != nfcTag::Family::Type2) {
        failTag(now);
        return;
    }
    g.phase = Phase::NdefCc;
}

void beginErase(uint32_t now) {
    if (g.family == nfcTag::Family::Type2) {
        g.phase = Phase::NdefCc;
        return;
    }
    if (g.family == nfcTag::Family::MifareClassic) {
        g.variant = nfcTag::classicVariantFromSak(g.id.sak);
        if (g.variant == nfcTag::Variant::Unknown) {
            failTag(now);
            return;
        }
        g.sector = 0;
        g.keyIndex = 0;
        g.blockCursor = 0;
        g.authed = false;
        g.cardChecked = false;
        g.phase = Phase::EraseClassicWrite;
        return;
    }
    failTag(now);
}

void beginClone(uint32_t now) {
    const RecordBuffer &buffer = runtime.recordBuffer();
    if (!nfcTag::decodeDump(buffer.data(), buffer.size(), g.source)) {
        failTag(now);
        return;
    }
    if (g.source.family != g.family) {
        failTag(now); // cannot move a UID between different technologies
        return;
    }
    if (g.source.id.uidLength != g.id.uidLength) {
        failTag(now); // target cannot carry this UID length (layout mismatch)
        return;
    }
    if (g.family == nfcTag::Family::MifareClassic) {
        if (g.source.unitSize != 16 || g.source.data == nullptr || g.source.dataLength < 16) {
            failTag(now);
            return;
        }
        g.cloneUnitCount = 0; // filled after reading the target's block 0
        g.sector = 0;
        g.keyIndex = 0;
        g.authed = false;
        g.phase = Phase::CloneAuth;
        return;
    }
    if (g.family == nfcTag::Family::Type2) {
        if (g.source.unitSize != 4 || g.source.data == nullptr || g.source.dataLength < 12) {
            failTag(now);
            return;
        }
        g.cloneUnitCount = 0;
        g.cloneCursor = 0;
        g.phase = Phase::CloneReadType2;
        return;
    }
    failTag(now);
}

void onSelected(uint32_t now) {
    g.selected = true;
    switch (g.action) {
    case ActionId::ReadUid: complete(nfcUid::toJson(g.id.uid, g.id.uidLength), now); return;
    case ActionId::NfcReadDump: beginDump(now); return;
    case ActionId::NfcWriteNdef: beginNdef(now); return;
    case ActionId::NfcErase: beginErase(now); return;
    case ActionId::NfcCloneUid: beginClone(now); return;
    default: failTag(now); return;
    }
}

void stepSelect(uint32_t now) {
    if (g.selected) {
        onSelected(now);
        return;
    }
    if (static_cast<uint32_t>(now - g.lastStepMs) < kProbeIntervalMs) return;
    g.lastStepMs = now;

    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    const Xfer result = runCommand(kInListPassiveTarget, sizeof(kInListPassiveTarget), response, length, kCommandBudgetMs);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;

    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t uidLength = 0;
    switch (nfcFrame::parseUidFrame(response, length, uid, uidLength)) {
    case nfcFrame::ParseStatus::Uid:
        memcpy(g.id.uid, uid.data(), uidLength);
        g.id.uidLength = uidLength;
        // SENS_RES (ATQA) and SEL_RES (SAK) sit at fixed offsets in the frame.
        g.id.atqa[0] = response[9];
        g.id.atqa[1] = response[10];
        g.id.sak = response[11];
        g.family = nfcTag::familyFromSak(g.id.sak);
        onSelected(now);
        break;
    case nfcFrame::ParseStatus::NoTarget:
        break; // keep probing until the deadline
    case nfcFrame::ParseStatus::Malformed:
        failHardware(now);
        break;
    }
}

void stepDumpRead(uint32_t now) {
    // Discover the tag size from the CC page when possible; otherwise read
    // until the tag NAKs past its last page.
    uint16_t limit = g.variant != nfcTag::Variant::Unknown ? nfcTag::totalUnits(g.variant) : kType2MaxPages;
    if (g.unitCursor >= limit) {
        g.unitCount = limit;
        g.phase = Phase::DumpFinish;
        return;
    }

    // A READ always returns four pages. Near the end of the memory the chunk
    // must be aligned down so its last page is the last valid page (a READ that
    // starts past the end NAKs), then only the still-needed pages are stored.
    uint16_t readAt = g.unitCursor;
    if (g.variant != nfcTag::Variant::Unknown && static_cast<uint16_t>(limit - g.unitCursor) < 4) {
        readAt = static_cast<uint16_t>(limit >= 4 ? limit - 4 : 0);
    }

    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readPages(static_cast<uint8_t>(readAt), data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;

    if (status != 0x00 || dataLength < 16) {
        // NAK: the readable area ended. Nothing read at all means the tag is not
        // dumpable; otherwise retain exactly what was read so the dump never
        // advertises 0xFF fill as data.
        if (g.unitCursor == 0) {
            failTag(now);
            return;
        }
        if (g.variant != nfcTag::Variant::Unknown && g.unitCursor < limit) {
            g.truncated = true; // nominal size known but part of it was unreadable
            g.holes = true;
        }
        g.unitCount = g.unitCursor;
        g.phase = Phase::DumpFinish;
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        const uint16_t page = static_cast<uint16_t>(readAt + i);
        if (page >= g.unitCursor && page < limit) {
            memcpy(g.units + static_cast<size_t>(page) * 4, data + i * 4, 4);
        }
    }
    if (g.unitCursor == 0) {
        // Page 3 (offset 12) is the Capability Container: E1 <ver> <size> <acc>.
        const uint8_t *cc = data + 12;
        if (cc[0] == 0xE1) {
            g.variant = nfcTag::type2VariantFromCcSize(cc[2]);
            if (g.variant != nfcTag::Variant::Unknown) limit = nfcTag::totalUnits(g.variant);
        }
    }
    g.unitCursor = static_cast<uint16_t>(readAt + 4 >= limit ? limit : readAt + 4);
    if (g.unitCursor >= limit) {
        g.unitCount = limit;
        g.phase = Phase::DumpFinish;
    }
}

void stepDumpAuth(uint32_t now) {
    if (g.sector >= classicSectorCount(g.variant)) {
        g.phase = Phase::DumpFinish;
        return;
    }
    if (authenticateSector(classicSectorFirst(g.variant, g.sector), g.keyIndex)) {
        g.authed = true;
        g.keyIndex = 0;
        g.blockCursor = classicSectorFirst(g.variant, g.sector);
        g.phase = Phase::DumpReadBlock;
        return;
    }
    ++g.keyIndex;
    if (g.keyIndex >= kKeyCount) {
        // Sector stays 0xFF fill; keep reading later sectors.
        g.holes = true;
        g.keyIndex = 0;
        ++g.sector;
    }
}

void stepDumpReadBlock(uint32_t now) {
    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readBlock(g.blockCursor, data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;

    if (status == 0x00 && dataLength >= 16) {
        memcpy(g.units + g.blockCursor * 16, data, 16);
    } else {
        g.holes = true;
    }

    const uint8_t blocks = classicSectorBlocks(g.variant, g.sector);
    ++g.blockCursor;
    if (static_cast<uint16_t>(g.blockCursor) >=
        static_cast<uint16_t>(classicSectorFirst(g.variant, g.sector) + blocks)) {
        ++g.sector;
        g.authed = false;
        g.phase = g.sector >= classicSectorCount(g.variant) ? Phase::DumpFinish : Phase::DumpAuth;
    }
}

void stepDumpFinish(uint32_t now) {
    nfcTag::Dump dump;
    dump.id = g.id;
    dump.family = g.family;
    dump.variant = g.variant;
    dump.unitSize = g.family == nfcTag::Family::Type2 ? 4 : 16;
    dump.storedUnits = g.unitCount;
    dump.truncated = g.truncated;
    dump.holes = g.holes;
    dump.data = g.units;
    dump.dataLength = static_cast<size_t>(g.unitCount) * dump.unitSize;

    uint8_t encoded[RecordBuffer::capacity];
    size_t encodedLength = 0;
    if (!nfcTag::encodeDump(dump, encoded, sizeof(encoded), encodedLength)) {
        failTag(now);
        return;
    }
    const std::string payload = dumpJson(dump);
    runtime.completeRecord(activeTicket, payload, encoded, encodedLength, now);
    transactionOutstanding = false;
    resetScratch();
}

void stepNdefCc(uint32_t now) {
    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readPages(0, data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;

    if (status != 0x00 || dataLength < 16) {
        failTag(now);
        return;
    }
    const uint8_t *cc = data + 12;
    if (cc[0] != 0xE1) {
        failTag(now); // not an NDEF Type 2 tag
        return;
    }
    g.variant = nfcTag::type2VariantFromCcSize(cc[2]);
    if (g.variant == nfcTag::Variant::Unknown) {
        failTag(now);
        return;
    }
    // CC access byte: 0x00 = read/write, 0x0F = read-only; anything else is a
    // reserved condition. Refuse up front rather than run a write doomed to NAK.
    if (cc[3] != 0x00) {
        failTag(now);
        return;
    }
    // Static lock bytes (page 2, bytes 2..3): a locked tag would NAK writes,
    // so reject up front rather than claim a partial erase.
    if (data[10] != 0x00 || data[11] != 0x00) {
        failTag(now);
        return;
    }
    g.phase = Phase::NdefDynLock;
}

void stepNdefDynLock(uint32_t now) {
    const uint16_t page = nfcTag::type2DynamicLockPage(g.variant);
    if (page == 0) {
        g.phase = Phase::NdefPrefix; // Ultralight: no dynamic lock page
        return;
    }
    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readPages(static_cast<uint8_t>(page), data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00 || dataLength < 4) {
        failTag(now);
        return;
    }
    // Dynamic lock bytes (first two bytes of the page): a set bit locks user
    // pages from page 16 on. Refuse before any write rather than NAK mid-way.
    if (data[0] != 0x00 || data[1] != 0x00) {
        failTag(now);
        return;
    }
    g.phase = Phase::NdefPrefix;
}

void stepNdefPrefix(uint32_t now) {
    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readPages(4, data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00 || dataLength < kPrefixReadBytes) {
        failTag(now);
        return;
    }

    g.prefixBytes = nfcNdef::prefixLength(data, kPrefixReadBytes);
    if (g.prefixBytes >= kPrefixReadBytes) {
        failTag(now); // unexpected layout: no NDEF/terminator in the first pages
        return;
    }
    memcpy(g.prefix, data, g.prefixBytes);

    // Control TLVs: a Memory Control reserved region or a Lock Control TLV that
    // maps lock bytes into the planned write range makes the write unsafe.
    // Benign standard control TLVs (static-lock area / variant dynamic-lock
    // page) stay in the prefix and are preserved on rewrite.
    const size_t writeStart = 4 * 4; // first user page (page 4)
    const size_t writeEnd = writeStart + nfcTag::type2UserCapacityBytes(g.variant);
    const nfcNdef::ControlTlvVerdict control =
        nfcNdef::scanControlTlvs(g.prefix, g.prefixBytes, writeStart, writeEnd);
    if (control != nfcNdef::ControlTlvVerdict::Ok) {
        failTag(now);
        return;
    }

    if (g.action == ActionId::NfcWriteNdef) {
        // Largest record the catalog allows: NDEF header (4) + status byte +
        // language code + kMaxTextBytes.
        uint8_t message[4 + 1 + nfcNdef::kLanguageLength + nfcNdef::kMaxTextBytes];
        const size_t messageBytes = nfcNdef::encodeTextRecord(std::string_view(g.text, g.textLen), message,
                                                              sizeof(message));
        if (messageBytes == 0) {
            failTag(now);
            return;
        }
        nfcNdef::AreaPlan plan;
        if (nfcNdef::encodeArea(g.prefix, g.prefixBytes, message, messageBytes, g.area, sizeof(g.area), plan) == 0) {
            failTag(now);
            return;
        }
        const size_t aligned = nfcNdef::pageAligned(plan.totalBytes);
        if (aligned > nfcTag::type2UserCapacityBytes(g.variant)) {
            failTag(now); // insufficient capacity for this tag
            return;
        }
        g.areaBytes = aligned;
        g.pageCount = static_cast<uint16_t>(nfcNdef::pagesFor(aligned));
        g.pageCursor = 0;
        g.resumePhase = Phase::NdefWrite;
        g.phase = Phase::CheckStable;
        return;
    }

    // Erase: bound the region by the existing NDEF Message TLV.
    nfcNdef::Tlv tlv;
    if (!nfcNdef::findNdefTlv(data, kPrefixReadBytes, tlv)) {
        complete(eraseJson(0), now); // already empty
        return;
    }
    g.hasNdef = true;
    const size_t end = tlv.offset + tlv.header + tlv.length;
    const size_t minimum = tlv.offset + 3; // 03 00 FE
    g.regionBytes = end > minimum ? end : minimum;
    const size_t aligned = nfcNdef::pageAligned(g.regionBytes);
    if (aligned > nfcTag::type2UserCapacityBytes(g.variant)) {
        failTag(now);
        return;
    }
    memset(g.area, 0, sizeof(g.area));
    memcpy(g.area, g.prefix, g.prefixBytes);
    if (nfcNdef::encodeEmptyArea(g.regionBytes - g.prefixBytes, g.area + g.prefixBytes,
                                 sizeof(g.area) - g.prefixBytes) == 0) {
        failTag(now);
        return;
    }
    g.areaBytes = aligned;
    g.pageCount = static_cast<uint16_t>(nfcNdef::pagesFor(aligned));
    g.pageCursor = 0;
    g.resumePhase = Phase::NdefWrite;
    g.phase = Phase::CheckStable;
}

void stepNdefWrite(uint32_t now) {
    const uint8_t page = static_cast<uint8_t>(4 + g.pageCursor);
    uint8_t chunk[4];
    const size_t offset = static_cast<size_t>(g.pageCursor) * 4;
    for (size_t i = 0; i < 4; ++i) chunk[i] = offset + i < g.areaBytes ? g.area[offset + i] : 0x00;

    uint8_t status = 0xFF;
    const Xfer result = writePage(page, chunk, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00) {
        failTag(now); // tag NAK (locked/protected page): stop before more writes
        return;
    }
    ++g.pageCursor;
    if (g.pageCursor >= g.pageCount) {
        g.pageCursor = 0;
        g.resumePhase = Phase::NdefVerify;
        g.phase = Phase::CheckStable; // re-select before attributing the result
    } else {
        g.resumePhase = Phase::NdefWrite;
        g.phase = Phase::CheckStable; // confirm the same tag before the next page
    }
}

void stepNdefVerify(uint32_t now) {
    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readPages(static_cast<uint8_t>(4 + g.pageCursor), data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00 || dataLength < 4) {
        failTag(now);
        return;
    }
    const size_t offset = static_cast<size_t>(g.pageCursor) * 4;
    for (size_t i = 0; i < 4; ++i) {
        const uint8_t expected = offset + i < g.areaBytes ? g.area[offset + i] : 0x00;
        if (data[i] != expected) {
            failTag(now); // write did not stick
            return;
        }
    }
    ++g.pageCursor;
    if (g.pageCursor >= g.pageCount) {
        if (g.action == ActionId::NfcWriteNdef) {
            complete(writeNdefJson(), now);
        } else {
            const uint16_t erased = g.hasNdef ? 3 : 0;
            complete(eraseJson(erased), now);
        }
    } else {
        g.resumePhase = Phase::NdefVerify;
        g.phase = Phase::CheckStable; // re-select before the next verify read
    }
}

enum class BlockReady : uint8_t { Ready, Pending, Failed };

// Shared per-block preamble for erase: skip non-data blocks (block 0 and sector
// trailers), confirm the same tag by re-selecting, and authenticate the block's
// sector. MIFARE auth is sector-local and every re-select drops it, so both the
// write and the verify pass must re-authenticate. Performs at most one command.
BlockReady erasePrepareBlock(uint32_t now, Phase resume) {
    if (!nfcTag::classicIsDataBlock(g.variant, g.blockCursor)) {
        ++g.blockCursor; // block 0 and trailers are never touched
        return BlockReady::Pending;
    }
    if (!g.cardChecked) {
        // Confirm the same tag before acting on this block: a swap mid-erase
        // must not receive the rest of the wipe or a false verification.
        g.cardChecked = true;
        g.resumePhase = resume;
        g.phase = Phase::CheckStable;
        return BlockReady::Pending;
    }
    const uint16_t sector = classicSectorOf(g.variant, g.blockCursor);
    if (sector != g.sector) {
        g.sector = static_cast<uint8_t>(sector);
        g.keyIndex = 0;
        g.authed = false;
    }
    if (!g.authed) {
        if (authenticateSector(classicSectorFirst(g.variant, sector), g.keyIndex)) {
            g.authed = true;
            g.keyIndex = 0;
        } else if (++g.keyIndex >= kKeyCount) {
            failTag(now); // locked/unknown key: refuse rather than pretend success
            return BlockReady::Failed;
        }
        return BlockReady::Pending;
    }
    return BlockReady::Ready;
}

void stepEraseClassicWrite(uint32_t now) {
    if (g.blockCursor >= nfcTag::totalUnits(g.variant)) {
        g.blockCursor = 0;
        g.sector = 0;
        g.authed = false;
        g.cardChecked = false;
        g.phase = Phase::EraseClassicVerify;
        return;
    }
    if (erasePrepareBlock(now, Phase::EraseClassicWrite) != BlockReady::Ready) return;

    const uint8_t zeros[16] = {};
    uint8_t status = 0xFF;
    const Xfer result = writeBlock(g.blockCursor, zeros, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00) {
        failTag(now); // tag NAK (locked block): stop rather than keep writing
        return;
    }
    ++g.blockCursor;
    g.authed = false;      // the next re-select drops this auth
    g.cardChecked = false; // force a fresh stability check before the next block
}

void stepEraseClassicVerify(uint32_t now) {
    if (g.blockCursor >= nfcTag::totalUnits(g.variant)) {
        complete(eraseJson(nfcTag::classicDataBlockCount(g.variant)), now);
        return;
    }
    if (erasePrepareBlock(now, Phase::EraseClassicVerify) != BlockReady::Ready) return;

    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readBlock(g.blockCursor, data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00 || dataLength < 16) {
        failTag(now);
        return;
    }
    for (size_t i = 0; i < 16; ++i) {
        if (data[i] != 0x00) {
            failTag(now);
            return;
        }
    }
    ++g.blockCursor;
    g.authed = false;
    g.cardChecked = false;
}

void stepCloneReadClassic(uint32_t now) {
    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readBlock(0, data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00 || dataLength < 16) {
        failTag(now);
        return;
    }
    // Splice the source UID + recomputed BCC into the target's own block 0 so
    // the target keeps its SAK/ATQA/manufacturer bytes.
    memcpy(g.cloneUnits, data, 16);
    if (!nfcTag::spliceClassicBlock0(g.source.id.uid, g.source.id.uidLength, g.cloneUnits)) {
        failTag(now);
        return;
    }
    g.cloneUnitCount = 1;
    g.phase = Phase::CloneWriteClassic;
}

void stepCloneReadType2(uint32_t now) {
    uint8_t data[kMaxFrame + 1];
    uint8_t dataLength = 0, status = 0xFF;
    const Xfer result = readPages(0, data, dataLength, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00 || dataLength < 12) {
        failTag(now);
        return;
    }
    // Splice UID + BCC into the target's own pages 0..2, preserving its
    // internal/static-lock bytes (blind copying them could brick the tag).
    memcpy(g.cloneUnits, data, 12);
    if (!nfcTag::spliceType2ManufacturerPages(g.source.id.uid, g.source.id.uidLength, g.cloneUnits)) {
        failTag(now);
        return;
    }
    g.cloneUnitCount = 3;
    g.cloneCursor = 0;
    g.phase = Phase::CloneWriteType2;
}

void stepCloneWriteClassic(uint32_t now) {
    uint8_t status = 0xFF;
    const Xfer result = writeBlock(0, g.cloneUnits, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00) {
        failTag(now); // ordinary (read-only) card refused block 0
        return;
    }
    g.phase = Phase::CloneVerify;
}

void stepCloneWriteType2(uint32_t now) {
    uint8_t status = 0xFF;
    const Xfer result = writePage(g.cloneCursor, g.cloneUnits + static_cast<size_t>(g.cloneCursor) * 4, status);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;
    if (status != 0x00) {
        failTag(now); // manufacturer page not writable on this card
        return;
    }
    ++g.cloneCursor;
    if (g.cloneCursor >= g.cloneUnitCount) g.phase = Phase::CloneVerify;
}

void stepCloneVerify(uint32_t now) {
    if (static_cast<uint32_t>(now - g.lastStepMs) < kProbeIntervalMs) return;
    g.lastStepMs = now;

    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    const Xfer result = runCommand(kInListPassiveTarget, sizeof(kInListPassiveTarget), response, length, kCommandBudgetMs);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;

    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t uidLength = 0;
    if (nfcFrame::parseUidFrame(response, length, uid, uidLength) != nfcFrame::ParseStatus::Uid) {
        return; // keep waiting until the deadline
    }
    if (uidLength != g.source.id.uidLength || memcmp(uid.data(), g.source.id.uid, uidLength) != 0) {
        failTag(now); // the badge did not accept the UID (ordinary read-only tag)
        return;
    }
    complete(cloneJson(), now);
}

void step(uint32_t now) {
    if (runtime.expire(now, ActionError::ReadTimeout, transactionOutstanding)) {
        resetScratch();
        return;
    }
    switch (g.phase) {
    case Phase::Idle: return;
    case Phase::Select: stepSelect(now); return;
    case Phase::CheckStable: {
        if (confirmCardStable(now) == Stability::Stable) g.phase = g.resumePhase;
        return;
    }
    case Phase::DumpRead: stepDumpRead(now); return;
    case Phase::DumpAuth: stepDumpAuth(now); return;
    case Phase::DumpReadBlock: stepDumpReadBlock(now); return;
    case Phase::DumpFinish: stepDumpFinish(now); return;
    case Phase::NdefCc: stepNdefCc(now); return;
    case Phase::NdefDynLock: stepNdefDynLock(now); return;
    case Phase::NdefPrefix: stepNdefPrefix(now); return;
    case Phase::NdefWrite: stepNdefWrite(now); return;
    case Phase::NdefVerify: stepNdefVerify(now); return;
    case Phase::EraseClassicWrite: stepEraseClassicWrite(now); return;
    case Phase::EraseClassicVerify: stepEraseClassicVerify(now); return;
    case Phase::CloneAuth: {
        if (authenticateSector(0, g.keyIndex)) {
            g.authed = true;
            g.phase = Phase::CloneReadClassic;
        } else if (++g.keyIndex >= kKeyCount) {
            failTag(now);
        }
        return;
    }
    case Phase::CloneReadClassic: stepCloneReadClassic(now); return;
    case Phase::CloneReadType2: stepCloneReadType2(now); return;
    case Phase::CloneWriteClassic: stepCloneWriteClassic(now); return;
    case Phase::CloneWriteType2: stepCloneWriteType2(now); return;
    case Phase::CloneVerify: stepCloneVerify(now); return;
    }
}

bool probeFirmware(uint8_t &version, uint8_t &revision) {
    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    if (runCommand(kGetFirmwareVersion, sizeof(kGetFirmwareVersion), response, length, kCommandBudgetMs) != Xfer::Ok) {
        return false;
    }
    if (!responseCode(response, length, 0x03) || response[7] != 0x32) return false; // IC must be PN532
    version = response[8];
    revision = response[10];
    return true;
}

bool initChip() {
    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;

    if (runCommand(kSamConfiguration, sizeof(kSamConfiguration), response, length, kCommandBudgetMs) != Xfer::Ok ||
        !responseCode(response, length, 0x15)) {
        return false;
    }
    if (runCommand(kSetParameters, sizeof(kSetParameters), response, length, kCommandBudgetMs) != Xfer::Ok ||
        !responseCode(response, length, 0x13)) {
        return false;
    }
    if (runCommand(kSetMaxRetries, sizeof(kSetMaxRetries), response, length, kCommandBudgetMs) != Xfer::Ok ||
        !responseCode(response, length, 0x33)) {
        return false;
    }

    uint8_t version = 0, revision = 0;
    return probeFirmware(version, revision);
}

void drainCleanup(uint32_t now) {
    if (transactionOutstanding) {
        if (!abortTransaction()) return; // keep the flag; retry next poll
        transactionOutstanding = false;
    }
    runtime.finishCleanup(now);
}

} // namespace

void begin() {
    Wire.begin(static_cast<int>(PIN_PN532_SDA), static_cast<int>(PIN_PN532_SCL));
    Wire.setTimeOut(kI2cTimeoutMs);
}

CommandError setEnabled(bool enabled) {
    const uint32_t now = millis();

    if (enabled) {
        if (runtime.status().enabled) return CommandError::None; // idempotent
        runtime.setEnabled(true, now);
        if (runtime.status().cleanupPending) return CommandError::None;

        lastHealthMs = now;
        uint8_t version = 0, revision = 0;
        if (!initChip() || !probeFirmware(version, revision)) {
            runtime.setHealth(false, "pn532 not responding", now);
            return CommandError::HardwareError;
        }
        char detail[40];
        snprintf(detail, sizeof(detail), "pn532 ok (fw 0x%02X%02X)", version, revision);
        runtime.setHealth(true, detail, now);
        return CommandError::None;
    }

    runtime.setEnabled(false, now, transactionOutstanding);
    resetScratch();
    return CommandError::None;
}

CommandError handleAction(ActionId action, const ActionParams &params) {
    const ActionDescriptor *descriptor = catalog::findAction(ModuleId::Pn532, action);
    if (descriptor == nullptr) return CommandError::UnsupportedAction;
    switch (action) {
    case ActionId::ReadUid:
    case ActionId::NfcReadDump:
    case ActionId::NfcCloneUid:
    case ActionId::NfcWriteNdef:
    case ActionId::NfcErase: break;
    default: return CommandError::UnsupportedAction;
    }

    char text[kMaxParamStringBytes + 1] = {};
    size_t textLen = 0;
    uint32_t deadlineMs = kUidDeadlineMs;
    switch (action) {
    case ActionId::ReadUid: deadlineMs = kUidDeadlineMs; break;
    case ActionId::NfcReadDump: deadlineMs = kDumpDeadlineMs; break;
    case ActionId::NfcCloneUid: deadlineMs = kCloneDeadlineMs; break;
    case ActionId::NfcWriteNdef:
        deadlineMs = kWriteDeadlineMs;
        textLen = params.stringLength(0);
        if (textLen == 0 || textLen > kMaxParamStringBytes) return CommandError::InvalidParams;
        memcpy(text, params.string(0), textLen);
        text[textLen] = '\0';
        break;
    case ActionId::NfcErase: deadlineMs = kEraseDeadlineMs; break;
    default: return CommandError::UnsupportedAction;
    }

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), deadlineMs, ticket, *descriptor);
    if (error != CommandError::None) return error;

    resetScratch();
    activeTicket = ticket;
    g.action = action;
    g.ticket = ticket;
    g.phase = Phase::Select;
    g.selected = false;
    g.lastStepMs = 0;
    if (action == ActionId::NfcWriteNdef) {
        memcpy(g.text, text, textLen);
        g.textLen = textLen;
    }
    return CommandError::None;
}

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) {
        drainCleanup(now);
        if (runtime.status().cleanupPending) return;
    }
    if (!runtime.status().enabled) return;

    if (runtime.status().actionState == ActionState::Running) {
        step(now);
        return;
    }

    if (static_cast<uint32_t>(now - lastHealthMs) < kHealthRecheckMs) return;
    lastHealthMs = now;

    uint8_t version = 0, revision = 0;
    if (!probeFirmware(version, revision)) {
        runtime.setHealth(false, "pn532 not responding", now);
        return;
    }
    char detail[40];
    snprintf(detail, sizeof(detail), "pn532 ok (fw 0x%02X%02X)", version, revision);
    runtime.setHealth(true, detail, now);
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

bool takeActionOutput(ActionOutput &output) { return runtime.takeActionOutput(output); }

} // namespace pn532

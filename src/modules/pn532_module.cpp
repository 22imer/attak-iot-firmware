#include "pn532_module.h"

#include <Wire.h>
#include <cstdio>
#include <cstring>

#include "board_pins.h"
#include "core/module_runtime.h"
#include "core/nfc_frame.h"
#include "core/nfc_uid_format.h"

namespace pn532 {

namespace {

ModuleRuntime runtime("pn532");

constexpr uint8_t kI2cAddress = 0x24; // PN532 I2C address (0x48 >> 1)
constexpr uint8_t kReady = 0x01;      // RDY status byte
constexpr uint8_t kI2cTimeoutMs = 20;
constexpr uint32_t kHealthRecheckMs = 500;
constexpr uint32_t kCommandBudgetMs = 60; // bounded per command; never the 5s window
constexpr uint32_t kUidDeadlineMs = 5000;
constexpr uint32_t kProbeIntervalMs = 100;
constexpr size_t kMaxFrame = nfcFrame::kMaxFrameBytes;

constexpr uint8_t kAckFrame[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
constexpr uint8_t kGetFirmwareVersion[] = {0x02};
constexpr uint8_t kSamConfiguration[] = {0x14, 0x01, 0x14, 0x00}; // Normal mode, IRQ not driven
constexpr uint8_t kSetParameters[] = {0x12, 0x00};               // fAutomaticRATS off, no RATS/ATS
constexpr uint8_t kSetMaxRetries[] = {0x32, 0x05, 0xFF, 0x01, 0x00}; // MxRtyPassiveActivation = one try
constexpr uint8_t kInListPassiveTarget[] = {0x4A, 0x01, 0x00};

uint32_t activeTicket = 0;
uint32_t lastProbeMs = 0;
uint32_t lastHealthMs = 0;
bool transactionOutstanding = false;

enum class ReadyState : uint8_t { Ready, Busy, IoError };
enum class Xfer : uint8_t { Ok, Timeout, IoError, ProtocolError };

uint8_t i2cRead(uint8_t *buffer, uint8_t capacity) {
    const uint8_t got = static_cast<uint8_t>(Wire.requestFrom(kI2cAddress, capacity));
    uint8_t count = 0;
    while (count < got && Wire.available()) buffer[count++] = static_cast<uint8_t>(Wire.read());
    return count;
}

// One read returns status byte + frame; a NOT READY status must not be treated
// as a frame (a missing chip NACKs and yields zero bytes).
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

// Blocking but strictly bounded: writes one command, then waits for the ACK and
// the response inside `budgetMs`. Never used to span the whole action window.
Xfer runCommand(const uint8_t *command, uint8_t commandLength, uint8_t *response, uint8_t &responseLength,
                uint32_t budgetMs) {
    uint8_t frame[kMaxFrame + 1];
    const size_t frameLength = nfcFrame::buildCommandFrame(command, commandLength, frame, sizeof(frame));
    if (frameLength == 0 || frameLength > 0xFF) return Xfer::ProtocolError;
    if (!writeFrame(frame, static_cast<uint8_t>(frameLength))) return Xfer::IoError;

    const uint32_t deadline = millis() + budgetMs;
    responseLength = 0;

    bool acked = false;
    while (static_cast<int32_t>(millis() - deadline) < 0) {
        uint8_t length = 0;
        const ReadyState state = readFrame(frame, 6, length);
        if (state == ReadyState::IoError) return Xfer::IoError;
        if (state == ReadyState::Ready) {
            acked = nfcFrame::isAckFrame(frame, length);
            break;
        }
    }
    if (!acked) return Xfer::Timeout;

    while (static_cast<int32_t>(millis() - deadline) < 0) {
        uint8_t length = 0;
        const ReadyState state = readFrame(frame, static_cast<uint8_t>(kMaxFrame), length);
        if (state == ReadyState::IoError) return Xfer::IoError;
        if (state == ReadyState::Ready) {
            responseLength = length;
            memcpy(response, frame, length);
            return Xfer::Ok;
        }
    }
    return Xfer::Timeout;
}

// Host ACK frame aborts the PN532's current process (UM0701-02 §6.2.2.2-b) and
// leaves it waiting for a new command.
bool abortTransaction() { return writeFrame(kAckFrame, sizeof(kAckFrame)); }

bool responseCode(const uint8_t *response, uint8_t length, uint8_t expectedCommand) {
    uint16_t total = 0;
    if (!nfcFrame::parseResponseHeader(response, length, total) || length < total) return false;
    return response[6] == expectedCommand;
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

void failActionHardware(uint32_t now) {
    if (activeTicket != 0) {
        runtime.failAction(activeTicket, ActionError::HardwareError, now, false);
        activeTicket = 0;
    }
    transactionOutstanding = false;
    runtime.setHealth(false, "pn532 not responding", now);
}

void drainCleanup(uint32_t now) {
    if (transactionOutstanding) {
        if (!abortTransaction()) return; // keep the flag; retry next poll
        transactionOutstanding = false;
    }
    runtime.finishCleanup(now);
}

void stepReadUid(uint32_t now) {
    if (runtime.expire(now, ActionError::ReadTimeout, transactionOutstanding)) {
        activeTicket = 0;
        return;
    }
    if (static_cast<uint32_t>(now - lastProbeMs) < kProbeIntervalMs) return;
    lastProbeMs = now;

    uint8_t response[kMaxFrame + 1];
    uint8_t length = 0;
    const Xfer result = runCommand(kInListPassiveTarget, sizeof(kInListPassiveTarget), response, length, kCommandBudgetMs);
    if (result == Xfer::IoError || result == Xfer::ProtocolError) {
        failActionHardware(now);
        return;
    }
    if (result == Xfer::Timeout) {
        // A new command aborts the previous one per the protocol; keep waiting
        // inside the same deadline instead of blocking.
        transactionOutstanding = true;
        return;
    }
    transactionOutstanding = false;

    std::array<uint8_t, nfcFrame::kMaxUidLength> uid{};
    uint8_t uidLength = 0;
    switch (nfcFrame::parseUidFrame(response, length, uid, uidLength)) {
    case nfcFrame::ParseStatus::Uid:
        runtime.completeAction(activeTicket, nfcUid::toJson(uid.data(), uidLength), now);
        activeTicket = 0;
        break;
    case nfcFrame::ParseStatus::NoTarget:
        break; // keep probing until the deadline
    case nfcFrame::ParseStatus::Malformed:
        runtime.failAction(activeTicket, ActionError::HardwareError, now, false);
        activeTicket = 0;
        break;
    }
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
    activeTicket = 0;
    return CommandError::None;
}

CommandError handleAction(ActionId action) {
    if (action != ActionId::ReadUid) return CommandError::UnsupportedAction;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), kUidDeadlineMs, ticket);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    lastProbeMs = millis() - kProbeIntervalMs; // probe on the next poll
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
        stepReadUid(now);
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

} // namespace pn532

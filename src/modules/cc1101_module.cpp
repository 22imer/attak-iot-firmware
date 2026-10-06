#include "cc1101_module.h"

#include <Arduino.h>
#include <SPI.h>
#include <driver/rmt.h>
#include <esp_err.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "board_pins.h"
#include "core/action_catalog.h"
#include "core/jam_plan.h"
#include "core/module_runtime.h"
#include "core/rf_record.h"
#include "modules/spi_bus.h"

// CC1101 sub-GHz payloads (PLAN.md T10-T13):
//   rf_scan      OneShot RSSI sweep -> bounded peak/plan summary
//   rf_record    real raw OOK edge capture on GDO0 -> RAM record buffer
//   rf_replay    transmit the captured edge timings on the CC1101 carrier
//   rf_spectrum  Continuous repeating RSSI sweep, streamed
//   rf_custom_tx raw OOK bitstream of a hex payload
//
// The chip is driven by a local, fully bounded direct-register transport (see
// the "CC1101 transport" section): every SPI transaction has a hard SO-ready
// timeout and the register config/calibration follows the CC1101 datasheet, so
// no vendor helper can block the loop for up to a second per register after the
// module is unplugged. Transmit is the ESP32 RMT peripheral on RMT_CHANNEL_1
// (RMT_CHANNEL_0 is reserved for FastLED, RMT_CHANNEL_2 is the IR backend):
// rmt_write_items(..., wait_tx_done=false) fills the channel memory and
// returns, poll() waits with rmt_wait_tx_done(...,0) so loop() never sleeps for
// the waveform. The RMT generator's own carrier is disabled; the RF carrier is
// the CC1101's and is gated by the GDO0 level the RMT drives (async serial OOK,
// PKTCTRL0.PKT_FORMAT = 3). No jammer/carrier action is exposed: every TX step
// lasts only for the waveform it replays and the chip is idled between bursts.
//
// MCSM0.FS_AUTOCAL makes every IDLE->RX/TX transition run the synthesizer
// calibration, so RX/TX are only usable once MARCSTATE reports RX (0x0D) / TX
// (0x13); every arm waits for that bounded, cooperatively, and TX never emits a
// bit before MARCSTATE == TX. RSSI additionally needs the AGC/8-symbol average,
// so a sweep waits kRssiSettleUs after RX-ready.

namespace cc1101 {

namespace {

ModuleRuntime runtime("cc1101");

constexpr uint32_t kHealthRecheckMs = 500;
constexpr uint32_t kSoReadyTimeoutUs = 5000;
constexpr uint8_t kPartNum = 0x30;    // status register: 0x00 on a real CC1101
constexpr uint8_t kVersion = 0x31;    // status register: 0x14 on a real CC1101
constexpr uint8_t kRssiReg = 0x34;    // status register: RSSI (raw)
constexpr uint8_t kMarcState = 0x35;  // status register: MARCSTATE
constexpr uint8_t kMarcRx = 0x0D;     // MARCSTATE: RX
constexpr uint8_t kMarcTx = 0x13;     // MARCSTATE: TX
constexpr uint8_t kReadBurst = 0xC0;  // status/config burst read
constexpr uint8_t kReadSingle = 0x80;
constexpr uint8_t kWriteBurst = 0x40;

// Bounded, cooperative ready waits. FS_AUTOCAL settles in ~0.8 ms on the
// datasheet crystal, so 5 ms is a hard ceiling, never a spin.
constexpr uint32_t kReadyTimeoutUs = 5000;
// A sweep reads RSSI only after the AGC/8-symbol average is valid: >= 8 symbols
// at the configured ~5 kBaud is ~1.6 ms, so a conservative 2 ms settle.
constexpr uint32_t kRssiSettleUs = 2000;

// rf_scan / rf_spectrum plan budget: a sweep is accepted only when it fits
// inside one calibrated band AND yields between 1 and rfRecord::kMaxScanPoints
// points. A span that crosses the 348..387 MHz gap or overflows the bounded
// output is an explicit InvalidParams, never a silently truncated sweep.
constexpr uint32_t kScanDeadlineMs = 5000;
constexpr double kSpectrumDefaultStartMhz = 433.0;
constexpr double kSpectrumDefaultEndMhz = 434.0;
constexpr int64_t kSpectrumDefaultStepKhz = 25;

constexpr uint32_t kRecordDefaultFreqHz = 433920000u;
constexpr uint32_t kRecordDefaultWindowMs = 2000;
constexpr uint32_t kRecordMinWindowMs = 200;
constexpr uint32_t kRecordMaxWindowMs = 30000;

constexpr uint32_t kReplayDefaultGapMs = 200;
constexpr uint32_t kTxDeadlineMinMs = 1000;
constexpr uint32_t kTxDeadlineMaxMs = 30000;

// rf_custom_tx emits a protocol-agnostic raw OOK bitstream on GDO0 at a fixed
// 1 ms bit period, so the burst is bounded by bytes*8*bitPeriod (<=256 ms).
constexpr uint32_t kCustomTxBitPeriodUs = 1000; // 1 kbit/s raw OOK
constexpr uint32_t kCustomTxGapMs = 50;
constexpr size_t kCustomTxMaxBytes = kMaxParamStringBytes / 2; // 64 hex chars -> 32 bytes

// RMT backend. 1 us/tick, so the 15-bit item duration field is kMaxTxChunkUs.
constexpr rmt_channel_t kTxChannel = RMT_CHANNEL_1;
constexpr uint8_t kRmtClkDiv = 80; // APB 80 MHz / 80 => 1 us per tick

uint32_t lastHealthMs = 0;

bool configured = false; // radio registers programmed for async ASK this session
bool capturing = false;  // GDO0 change interrupt attached
uint32_t activeTicket = 0;
ActionId activeActionId = ActionId::None;

// --- CC1101 register map (TI CC1101 datasheet, section 28) ----------------
constexpr uint8_t kRegIocfg2 = 0x00;
constexpr uint8_t kRegIocfg0 = 0x02;
constexpr uint8_t kRegPktlen = 0x06;
constexpr uint8_t kRegPktctrl1 = 0x07;
constexpr uint8_t kRegPktctrl0 = 0x08;
constexpr uint8_t kRegAddr = 0x09;
constexpr uint8_t kRegChannr = 0x0A;
constexpr uint8_t kRegFsctrl1 = 0x0B;
constexpr uint8_t kRegFsctrl0 = 0x0C;
constexpr uint8_t kRegFreq2 = 0x0D;
constexpr uint8_t kRegFreq1 = 0x0E;
constexpr uint8_t kRegFreq0 = 0x0F;
constexpr uint8_t kRegMdmcfg4 = 0x10;
constexpr uint8_t kRegMdmcfg3 = 0x11;
constexpr uint8_t kRegMdmcfg2 = 0x12;
constexpr uint8_t kRegMdmcfg1 = 0x13;
constexpr uint8_t kRegMdmcfg0 = 0x14;
constexpr uint8_t kRegDeviatn = 0x15;
constexpr uint8_t kRegMcsm0 = 0x18;
constexpr uint8_t kRegFoccfg = 0x19;
constexpr uint8_t kRegBscfg = 0x1A;
constexpr uint8_t kRegAgcctrl2 = 0x1B;
constexpr uint8_t kRegAgcctrl1 = 0x1C;
constexpr uint8_t kRegAgcctrl0 = 0x1D;
constexpr uint8_t kRegFrend1 = 0x21;
constexpr uint8_t kRegFrend0 = 0x22;
constexpr uint8_t kRegFscal3 = 0x23;
constexpr uint8_t kRegFscal2 = 0x24;
constexpr uint8_t kRegFscal1 = 0x25;
constexpr uint8_t kRegFscal0 = 0x26;
constexpr uint8_t kRegFstest = 0x29;
constexpr uint8_t kRegTest2 = 0x2C;
constexpr uint8_t kRegTest1 = 0x2D;
constexpr uint8_t kRegTest0 = 0x2E;
constexpr uint8_t kRegPatable = 0x3E;
constexpr uint8_t kStrobeSres = 0x30;
constexpr uint8_t kStrobeSrx = 0x34;
constexpr uint8_t kStrobeStx = 0x35;
constexpr uint8_t kStrobeSidle = 0x36;

// --- GDO0 change capture (bounded ISR / cross-core teardown) ---------------
// The GDO0 change ISR can run on either core while poll() snapshots or tears
// down on the other, so every shared field is atomic and every arm/teardown
// bumps a generation counter. A handler that lost the race discards its sample
// instead of writing into a re-armed or half-torn-down capture. Teardown never
// spins: it detaches and reports whether an in-flight handler is still running,
// and the caller retries cooperatively on the next poll before reading a record.
std::atomic<bool> captureArmed{false};
std::atomic<uint32_t> captureGeneration{0};
std::atomic<uint32_t> captureCount{0};
std::atomic<uint32_t> capturePrevUs{0};
std::atomic<uint8_t> captureStarted{0};
std::atomic<uint8_t> captureFirstHigh{1};
std::atomic<uint8_t> captureStopped{0};
std::atomic<uint32_t> captureInFlight{0};

uint16_t captureDurations[rfRecord::kMaxDurations] = {};
uint32_t captureFreqHz = kRecordDefaultFreqHz;

// --- sweep state ----------------------------------------------------------
// A point goes Idle -> WaitReady (MARCSTATE==RX, bounded) -> Settle (AGC/8
// symbols) -> RSSI read -> next point.
enum class SweepPhase : uint8_t { Idle, WaitReady, Settle };
SweepPhase sweepPhase = SweepPhase::Idle;
double sweepStartMhz = 0.0;
uint32_t sweepStepKhz = 0;
size_t sweepCount = 0;
size_t sweepIndex = 0;
bool sweepReady = false; // a completed sweep awaits (re)publish
bool sweepContinuous = false;
uint32_t sweepReadyDeadlineUs = 0;
uint32_t sweepSettleAtUs = 0;
uint32_t lastSweepPublishMs = 0;
int sweepDbm[rfRecord::kMaxScanPoints] = {};

// --- capture lifecycle ----------------------------------------------------
enum class RecordPhase : uint8_t { Idle, WaitReady, Capturing };
enum class CaptureWrap : uint8_t { None, Record, Timeout };
RecordPhase recordPhase = RecordPhase::Idle;
CaptureWrap captureWrap = CaptureWrap::None;
bool captureWrapCleanup = false; // cleanupRequired if the teardown was deferred
bool captureDraining = false;    // ISR detached, waiting for in-flight to finish
uint32_t recordReadyDeadlineUs = 0;
uint32_t captureWindowMs = 0;
uint32_t captureDeadlineMs = 0;

// --- transmit state -------------------------------------------------------
enum class TxKind : uint8_t { None, Replay, Custom };
// One burst at a time: None -> WaitReady (MARCSTATE==TX, bounded) -> Streaming
// (RMT in hardware) -> gap -> next burst, all driven from poll().
enum class TxPhase : uint8_t { None, WaitReady, Streaming, Gap };

TxKind txKind = TxKind::None;
TxPhase txPhase = TxPhase::None;
rfRecord::Record replayRecord;
uint32_t txRepeatsLeft = 0;
uint32_t txRepeatTotal = 0;
uint32_t txGapMs = 0;
uint32_t txGapUntilMs = 0;
uint32_t txReadyDeadlineUs = 0;
uint8_t customBytes[kCustomTxMaxBytes] = {};
size_t customByteCount = 0;
uint32_t customFreqHz = kRecordDefaultFreqHz;

// Static, stable item buffers: the RMT driver streams from these in the TX
// interrupt until rmt_wait_tx_done() reports completion, so they must never be
// overwritten or moved while a burst is in flight.
rfRecord::TxItem txPlan[rfRecord::kMaxTxItems];
rmt_item32_t txItems[rfRecord::kMaxTxItems];
bool rmtReady = false;
uint8_t lastPaBand = 0;

// --- rf_jammer state (disruptive; PLAN §2.4) ------------------------------
// A continuous carrier is produced by holding GDO0 high while the chip is in
// TX with PKTCTRL0.PKT_FORMAT = 3 (async serial ASK) and FREND0.PA_POWER = 1:
// the GDO0 level selects PATABLE[1] (on) vs PATABLE[0] = 0 (off). The
// "intermittent" mode keys that level with a portable DutyGate driven from
// poll(); no burst is ever bit-banged inside loop().
#ifdef ENABLE_DISRUPTIVE
enum class JamMode : uint8_t { Full, Intermittent };
JamMode jamMode = JamMode::Full;
jamPlan::DutyGate jamGate;
uint32_t jamFreqHz = kRecordDefaultFreqHz;
uint32_t jamOnMs = 100;
uint32_t jamOffMs = 100;
uint32_t jamToggles = 0;
uint32_t jamStartedMs = 0;
uint32_t lastJamPublishMs = 0;
constexpr uint32_t kJamDefaultOnMs = 100;
constexpr uint32_t kJamDefaultOffMs = 100;
#endif

// --- bounded SPI transport -------------------------------------------------

bool waitSoReady() {
    const uint32_t start = micros();
    while (digitalRead(PIN_SPI_MISO)) {
        if (static_cast<uint32_t>(micros() - start) >= kSoReadyTimeoutUs) return false;
    }
    return true;
}

// One CS-asserted transaction. buffer[0] is the header; for reads the response
// bytes are written back into the same buffer. Returns false on a stuck SO line
// instead of blocking, so an unplugged chip cannot stall loop().
bool spiTransfer(uint8_t *buffer, size_t length) {
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CC1101_CS, LOW);
    bool ok = waitSoReady();
    if (ok) {
        for (size_t i = 0; i < length; ++i) buffer[i] = SPI.transfer(buffer[i]);
    }
    digitalWrite(PIN_CC1101_CS, HIGH);
    SPI.endTransaction();
    return ok;
}

bool writeReg(uint8_t address, uint8_t value) {
    uint8_t buffer[2] = {address, value};
    return spiTransfer(buffer, 2);
}

bool writeBurst(uint8_t address, const uint8_t *data, size_t length) {
    uint8_t buffer[1 + 8];
    if (length + 1 > sizeof(buffer)) return false;
    buffer[0] = static_cast<uint8_t>(address | kWriteBurst);
    std::memcpy(buffer + 1, data, length);
    return spiTransfer(buffer, length + 1);
}

bool readReg(uint8_t address, uint8_t &value) {
    uint8_t buffer[2] = {static_cast<uint8_t>(address | kReadSingle), 0x00};
    if (!spiTransfer(buffer, 2)) return false;
    value = buffer[1];
    return true;
}

bool strobe(uint8_t command) {
    uint8_t buffer[1] = {command};
    return spiTransfer(buffer, 1);
}

// Bounded status-register read (PARTNUM/VERSION/RSSI/MARCSTATE): a floating
// MISO fails the transaction, and the probe below rejects 0x00/0xFF so a loss
// is never masked.
bool readStatusRegister(uint8_t address, uint8_t &value) {
    uint8_t buffer[2] = {static_cast<uint8_t>(address | kReadBurst), 0x00};
    if (!spiTransfer(buffer, 2)) return false;
    value = buffer[1];
    return true;
}

// Datasheet reset sequence: pulse CSn and issue SRES, all bounded.
bool resetRadio() {
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CC1101_CS, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_CC1101_CS, HIGH);
    digitalWrite(PIN_CC1101_CS, LOW);
    bool ok = waitSoReady();
    if (ok) {
        uint8_t buffer[1] = {kStrobeSres};
        buffer[0] = SPI.transfer(buffer[0]);
    }
    digitalWrite(PIN_CC1101_CS, HIGH);
    SPI.endTransaction();
    delayMicroseconds(50); // SRES needs the chip to finish its internal reset
    return ok;
}

// --- config / calibration --------------------------------------------------

struct RegValue {
    uint8_t addr;
    uint8_t value;
};

// Async serial ASK: GDO0 is the raw edge stream (IOCFG0 = 0x0D), PKTCTRL0
// PKT_FORMAT = 3 (no packet handler), MDMCFG2 MOD_FORMAT = 3 (ASK/OOK) and
// FREND0 PA_POWER = 1 so PATABLE[1] (on) / PATABLE[0] = 0 (off) gate the carrier
// with the GDO0 level. Band PA tables below are the datasheet OOK PATABLE values.
constexpr RegValue kAsyncAskConfig[] = {
    {kRegIocfg2, 0x0D},
    {kRegIocfg0, 0x0D},
    {kRegPktctrl0, 0x32},
    {kRegMdmcfg3, 0x93},
    {kRegMdmcfg4, 0x87},
    {kRegMdmcfg2, 0x32},
    {kRegFrend0, 0x11},
};

constexpr RegValue kBaseConfig[] = {
    {kRegMdmcfg1, 0x02},  {kRegMdmcfg0, 0xF8},  {kRegChannr, 0x00}, {kRegDeviatn, 0x47},
    {kRegFrend1, 0x56},   {kRegMcsm0, 0x18},    {kRegFoccfg, 0x16}, {kRegBscfg, 0x1C},
    {kRegAgcctrl2, 0xC7}, {kRegAgcctrl1, 0x00}, {kRegAgcctrl0, 0xB2},
    {kRegFscal3, 0xE9},   {kRegFscal2, 0x2A},   {kRegFscal1, 0x00}, {kRegFscal0, 0x1F},
    {kRegFstest, 0x59},   {kRegTest2, 0x81},    {kRegTest1, 0x35},  {kRegTest0, 0x09},
    {kRegPktctrl1, 0x04}, {kRegAddr, 0x00},     {kRegPktlen, 0x00},
};

constexpr uint8_t kPaTable315[8] = {0x12, 0x0D, 0x1C, 0x34, 0x51, 0x85, 0xCB, 0xC2};
constexpr uint8_t kPaTable433[8] = {0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0};
constexpr uint8_t kPaTable868[10] = {0x03, 0x17, 0x1D, 0x26, 0x37, 0x50, 0x86, 0xCD, 0xC5, 0xC0};
constexpr uint8_t kPaTable915[10] = {0x03, 0x0E, 0x1E, 0x27, 0x38, 0x8E, 0x84, 0xCC, 0xC3, 0xC0};

// The module transmits at the band's maximum PATABLE entry; PATABLE[0] is the
// OOK off level.
bool setPa(double mhz) {
    uint8_t pa[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t band = 0;
    if (mhz >= 280.0 && mhz <= 348.0) {
        pa[1] = kPaTable315[7];
        band = 1;
    } else if (mhz >= 378.0 && mhz <= 464.0) {
        pa[1] = kPaTable433[7];
        band = 2;
    } else if (mhz >= 779.0 && mhz <= 899.99) {
        pa[1] = kPaTable868[9];
        band = 3;
    } else if (mhz >= 900.0 && mhz <= 928.0) {
        pa[1] = kPaTable915[9];
        band = 4;
    } else {
        return false;
    }
    if (!writeBurst(kRegPatable, pa, sizeof(pa))) return false;
    lastPaBand = band;
    return true;
}

uint8_t mapTrim(double mhz, double inLo, double inHi, int outLo, int outHi) {
    return static_cast<uint8_t>(static_cast<long>((mhz - inLo) * (outHi - outLo) / (inHi - inLo)) + outLo);
}

// Per-band frequency synthesizer trim (FSCTRL0) and TEST0, plus the FSCAL2
// fine-tune read the vendor calibration performs above the band midpoint.
bool calibrate(double mhz) {
    uint8_t fsctrl0 = 0;
    uint8_t test0 = 0x09;
    uint8_t band = 0;
    bool fineTune = false;

    if (mhz >= 280.0 && mhz <= 348.0) {
        fsctrl0 = mapTrim(mhz, 280.0, 348.0, 24, 28);
        band = 1;
        if (mhz < 322.88) {
            test0 = 0x0B;
        } else {
            fineTune = true;
        }
    } else if (mhz >= 378.0 && mhz <= 464.0) {
        fsctrl0 = mapTrim(mhz, 378.0, 464.0, 31, 38);
        band = 2;
        if (mhz < 430.5) {
            test0 = 0x0B;
        } else {
            fineTune = true;
        }
    } else if (mhz >= 779.0 && mhz <= 899.99) {
        fsctrl0 = mapTrim(mhz, 779.0, 899.0, 65, 76);
        band = 3;
        if (mhz < 861.0) {
            test0 = 0x0B;
        } else {
            fineTune = true;
        }
    } else if (mhz >= 900.0 && mhz <= 928.0) {
        fsctrl0 = mapTrim(mhz, 900.0, 928.0, 77, 79);
        band = 4;
        fineTune = true;
    } else {
        return false;
    }

    if (!writeReg(kRegFsctrl0, fsctrl0)) return false;
    if (!writeReg(kRegTest0, test0)) return false;

    if (fineTune) {
        uint8_t fscal2 = 0;
        if (!readReg(kRegFscal2, fscal2)) return false;
        if (fscal2 < 32 && !writeReg(kRegFscal2, static_cast<uint8_t>(fscal2 + 32))) return false;
    }
    if (lastPaBand != band && !setPa(mhz)) return false;
    return true;
}

// FREQ = f_xosc * 2^16 / 26 MHz, the standard CC1101 frequency word.
bool tune(uint32_t freqHz) {
    const uint32_t word = static_cast<uint32_t>((static_cast<uint64_t>(freqHz) * 65536ull) / 26000000ull);
    if (!writeReg(kRegFreq2, static_cast<uint8_t>((word >> 16) & 0xFF))) return false;
    if (!writeReg(kRegFreq1, static_cast<uint8_t>((word >> 8) & 0xFF))) return false;
    if (!writeReg(kRegFreq0, static_cast<uint8_t>(word & 0xFF))) return false;
    return calibrate(static_cast<double>(freqHz) / 1000000.0);
}

bool radioSetTx() { return strobe(kStrobeSidle) && strobe(kStrobeStx); }
bool radioSetRx() { return strobe(kStrobeSidle) && strobe(kStrobeSrx); }
bool radioSetIdle() { return strobe(kStrobeSidle); }

// Presence = PARTNUM == 0x00 and VERSION is a plausible non-floating byte.
bool probeChip(uint8_t &version) {
    uint8_t partNum = 0xFF;
    version = 0x00;
    if (!readStatusRegister(kPartNum, partNum) || partNum != 0x00) return false;
    if (!readStatusRegister(kVersion, version)) return false;
    return version != 0x00 && version != 0xFF;
}

void describe(uint8_t version, char *buffer, size_t size) {
    snprintf(buffer, size, "cc1101 ok (ver 0x%02X)", version);
}

uint32_t hzFromMhz(double mhz) { return static_cast<uint32_t>(mhz * 1000000.0 + 0.5); }

// Programs the chip for async-serial ASK on the shared bus (no repin: the bus
// was configured once by spiBus::begin()). Idempotent for the session; every
// call re-probes liveness so a recovered chip is admitted without waiting for
// the next poll's ~500 ms health recheck.
bool initRadio() {
    if (!configured) {
        if (!resetRadio()) return false;
        if (!writeReg(kRegFsctrl1, 0x06)) return false;
        for (const RegValue &rv : kAsyncAskConfig) {
            if (!writeReg(rv.addr, rv.value)) return false;
        }
        const double defaultMhz = static_cast<double>(kRecordDefaultFreqHz) / 1000000.0;
        if (!setPa(defaultMhz)) return false;
        if (!tune(kRecordDefaultFreqHz)) return false;
        for (const RegValue &rv : kBaseConfig) {
            if (!writeReg(rv.addr, rv.value)) return false;
        }
        configured = true;
    }

    uint8_t version = 0;
    if (!probeChip(version)) return false;
    char detail[32];
    describe(version, detail, sizeof(detail));
    runtime.setHealth(true, detail, millis());
    return true;
}

// --- raw OOK edge capture --------------------------------------------------

void IRAM_ATTR onGdoEdge() {
    if (!captureArmed.load(std::memory_order_acquire)) return;
    const uint32_t generation = captureGeneration.load(std::memory_order_acquire);
    captureInFlight.fetch_add(1, std::memory_order_acq_rel);
    if (!captureArmed.load(std::memory_order_acquire) ||
        captureGeneration.load(std::memory_order_acquire) != generation) {
        captureInFlight.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    const uint32_t now = micros();
    if (captureStarted.load(std::memory_order_relaxed) == 0) {
        if (captureGeneration.load(std::memory_order_acquire) != generation) {
            captureInFlight.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        captureStarted.store(1, std::memory_order_relaxed);
        capturePrevUs.store(now, std::memory_order_relaxed);
        captureFirstHigh.store(digitalRead(PIN_CC1101_GDO0) ? 1 : 0, std::memory_order_relaxed);
    } else {
        const uint32_t previous = capturePrevUs.load(std::memory_order_relaxed);
        const uint32_t count = captureCount.load(std::memory_order_relaxed);
        if (captureGeneration.load(std::memory_order_acquire) != generation) {
            captureInFlight.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        capturePrevUs.store(now, std::memory_order_relaxed);
        const uint32_t delta = now - previous;
        if (delta > rfRecord::kMaxDurationUs || count >= rfRecord::kMaxDurations) {
            captureStopped.store(1, std::memory_order_release);
        } else {
            captureDurations[count] = static_cast<uint16_t>(delta);
            captureCount.store(count + 1, std::memory_order_release);
        }
    }
    captureInFlight.fetch_sub(1, std::memory_order_acq_rel);
}

// Invalidates any in-flight handler and clears the sample fields. Called before
// arming a new capture and while tearing one down.
void resetCaptureState() {
    captureGeneration.fetch_add(1, std::memory_order_acq_rel);
    captureArmed.store(false, std::memory_order_release);
    captureCount.store(0, std::memory_order_relaxed);
    capturePrevUs.store(0, std::memory_order_relaxed);
    captureStarted.store(0, std::memory_order_relaxed);
    captureStopped.store(0, std::memory_order_relaxed);
    captureFirstHigh.store(1, std::memory_order_relaxed);
}

// Detaches the ISR, invalidates any in-flight sample, and reports whether a
// handler is still running. Never spins: the caller retries on the next poll,
// so a slow/other-core handler can never stall loop() or the cleanup lease.
bool stopCaptureIsr() {
    captureGeneration.fetch_add(1, std::memory_order_acq_rel);
    captureArmed.store(false, std::memory_order_release);
    if (capturing) {
        detachInterrupt(digitalPinToInterrupt(PIN_CC1101_GDO0));
        capturing = false;
    }
    return captureInFlight.load(std::memory_order_acquire) == 0;
}

// --- RMT transmit backend --------------------------------------------------

bool ensureRmtChannel() {
    if (rmtReady) return true;
    rmt_config_t config = {};
    config.rmt_mode = RMT_MODE_TX;
    config.channel = kTxChannel;
    config.gpio_num = static_cast<gpio_num_t>(PIN_CC1101_GDO0);
    config.clk_div = kRmtClkDiv;
    config.mem_block_num = 1;
    config.flags = 0;
    config.tx_config.carrier_en = false; // CC1101 OOK carrier is gated by GDO0
    config.tx_config.loop_en = false;
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    config.tx_config.idle_output_en = true;
    if (rmt_config(&config) != ESP_OK) return false;
    if (rmt_driver_install(kTxChannel, 0, 0) != ESP_OK) return false;
    rmtReady = true;
    return true;
}

void releaseRmt() {
    if (!rmtReady) return;
    rmt_tx_stop(kTxChannel);
    rmt_driver_uninstall(kTxChannel);
    rmtReady = false;
}

// Converts the pending waveform to RMT items and hands it to the channel. The
// item buffers are static and stay valid until rmt_wait_tx_done() completes.
bool rmtStart() {
    size_t itemCount = 0;
    if (txKind == TxKind::Replay) {
        itemCount = rfRecord::replayItems(replayRecord, txPlan, rfRecord::kMaxTxItems);
    } else {
        itemCount = rfRecord::customItems(customBytes, customByteCount, kCustomTxBitPeriodUs, txPlan,
                                          rfRecord::kMaxTxItems);
    }
    if (itemCount == 0 || itemCount > rfRecord::kMaxTxItems) return false;
    if (!ensureRmtChannel()) return false;

    for (size_t i = 0; i < itemCount; ++i) {
        txItems[i].duration0 = txPlan[i].duration0;
        txItems[i].level0 = txPlan[i].level0;
        txItems[i].duration1 = txPlan[i].duration1;
        txItems[i].level1 = txPlan[i].level1;
    }
    return rmt_write_items(kTxChannel, txItems, static_cast<int>(itemCount), false) == ESP_OK;
}

// --- action lifecycle helpers ---------------------------------------------

// Fast teardown for non-capture paths and disable: detach + invalidate but do
// not read the capture buffer, so no drain wait is needed here.
void clearActive() {
    stopCaptureIsr();
    captureDraining = false;
    captureWrap = CaptureWrap::None;
    captureWrapCleanup = false;
    recordPhase = RecordPhase::Idle;
    releaseRmt();
    if (configured) radioSetIdle();
#ifdef ENABLE_DISRUPTIVE
    if (activeActionId == ActionId::RfJammer) {
        // A jammer holds GDO0 high; force the carrier off and return the pin to
        // input so a later capture / RMT transmit starts from a neutral level.
        digitalWrite(PIN_CC1101_GDO0, LOW);
        pinMode(PIN_CC1101_GDO0, INPUT);
    }
#endif
    txPhase = TxPhase::None;
    txKind = TxKind::None;
    activeTicket = 0;
    activeActionId = ActionId::None;
    sweepPhase = SweepPhase::Idle;
    sweepReady = false;
}

void failActive(ActionError error, const char *detail, uint32_t now, bool cleanupRequired) {
    const uint32_t ticket = activeTicket != 0 ? activeTicket : runtime.status().actionTicket;
    clearActive();
    runtime.failAction(ticket, error, now, cleanupRequired);
    runtime.setHealth(false, detail, now);
}

uint32_t pointHz(size_t index) {
    return hzFromMhz(rfRecord::sweepPointMhz(sweepStartMhz, sweepStepKhz, index));
}

bool armSweepPoint() {
    if (!tune(pointHz(sweepIndex))) return false;
    if (!radioSetRx()) return false;
    sweepReadyDeadlineUs = micros() + kReadyTimeoutUs;
    sweepPhase = SweepPhase::WaitReady;
    return true;
}

void finishSweep(uint32_t now) {
    const uint32_t startHz = hzFromMhz(sweepStartMhz);
    sweepPhase = SweepPhase::Idle;
    if (!sweepContinuous) {
        std::string payload = rfRecord::sweepToJson("rf_scan", startHz, sweepStepKhz, sweepDbm, sweepCount);
        if (configured) radioSetIdle();
        sweepReady = false;
        if (runtime.completeAction(activeTicket, std::move(payload), now)) {
            activeTicket = 0;
            activeActionId = ActionId::None;
        }
        return;
    }

    std::string payload = rfRecord::sweepToJson("rf_spectrum", startHz, sweepStepKhz, sweepDbm, sweepCount);
    if (runtime.publishOutput(activeTicket, std::move(payload), now)) {
        lastSweepPublishMs = now;
        sweepReady = false;
        sweepIndex = 0;
    }
    // Otherwise the completed sweep is kept and retried next poll; the runtime
    // enforces the 100 ms stream cadence.
}

void stepSweep(uint32_t now) {
    if (runtime.expire(now, ActionError::ScanTimeout, false)) {
        if (configured) radioSetIdle();
        activeTicket = 0;
        activeActionId = ActionId::None;
        sweepPhase = SweepPhase::Idle;
        sweepReady = false;
        return;
    }
    if (sweepReady) {
        finishSweep(now);
        return;
    }
    if (sweepPhase == SweepPhase::Idle) {
        if (sweepContinuous && static_cast<uint32_t>(now - lastSweepPublishMs) < kMinActionOutputIntervalMs) return;
        if (!armSweepPoint()) failActive(ActionError::HardwareError, "cc1101 tune failed", now, true);
        return;
    }
    if (sweepPhase == SweepPhase::WaitReady) {
        if (static_cast<int32_t>(micros() - sweepReadyDeadlineUs) >= 0) {
            failActive(ActionError::HardwareError, "cc1101 rx ready timeout", now, true);
            return;
        }
        uint8_t marc = 0;
        if (!readStatusRegister(kMarcState, marc)) {
            failActive(ActionError::HardwareError, "cc1101 rx ready read failed", now, true);
            return;
        }
        if (marc != kMarcRx) return; // calibration still running; retry next poll
        sweepSettleAtUs = micros() + kRssiSettleUs;
        sweepPhase = SweepPhase::Settle;
        return;
    }

    // Settle: the AGC/8-symbol average must be valid before RSSI is meaningful.
    if (static_cast<int32_t>(micros() - sweepSettleAtUs) < 0) return;
    uint8_t raw = 0;
    if (!readStatusRegister(kRssiReg, raw)) {
        // A failed RSSI read must not be reported as a real level: fail the
        // sweep and the health instead of recording a fabricated point.
        failActive(ActionError::HardwareError, "cc1101 rssi read failed", now, true);
        return;
    }
    sweepDbm[sweepIndex] = rfRecord::rssiToDbm(raw);
    ++sweepIndex;
    if (sweepIndex < sweepCount) {
        if (!armSweepPoint()) failActive(ActionError::HardwareError, "cc1101 tune failed", now, true);
        return;
    }
    sweepReady = true;
    finishSweep(now);
}

// --- capture finalize ------------------------------------------------------

void finishCaptureWrap(uint32_t now) {
    const CaptureWrap wrap = captureWrap;
    captureWrap = CaptureWrap::None;
    recordPhase = RecordPhase::Idle;

    const uint32_t ticket = activeTicket;
    activeTicket = 0;
    activeActionId = ActionId::None;
    if (configured) radioSetIdle();

    if (wrap == CaptureWrap::Record) {
        const uint32_t count = captureCount.load(std::memory_order_acquire);
        const bool firstHigh = captureFirstHigh.load(std::memory_order_acquire) != 0;
        // The ISR is detached and drained, so captureDurations is stable here.
        rfRecord::Record record;
        const rfRecord::Result result =
            rfRecord::makeRecord(captureFreqHz, firstHigh, captureDurations, count, record);
        if (result == rfRecord::Result::Ready) {
            uint8_t encoded[rfRecord::kMaxEncodedBytes];
            size_t length = 0;
            if (rfRecord::encodeToBytes(record, encoded, sizeof(encoded), length)) {
                runtime.completeRecord(ticket, rfRecord::toJson(record), encoded, length, now);
                return;
            }
        }
        runtime.failAction(ticket, ActionError::CaptureTooLong, now, false);
        return;
    }

    // Timeout: if the teardown could not drain on the spot the runtime lease is
    // kept so cleanup() retries the drain before releasing the backend.
    runtime.failAction(ticket, ActionError::CaptureTimeout, now, captureWrapCleanup);
}

// Ends the capture (record or timeout). Detaches without spinning; if a handler
// is still in flight the wrap is remembered and finished on a later poll.
void beginCaptureWrap(CaptureWrap wrap, bool cleanupIfDraining, uint32_t now) {
    captureWrap = wrap;
    if (stopCaptureIsr()) {
        captureDraining = false;
        captureWrapCleanup = false;
        finishCaptureWrap(now);
        return;
    }
    captureDraining = true;
    captureWrapCleanup = cleanupIfDraining;
}

void stepRecord(uint32_t now) {
    if (captureDraining) {
        if (!stopCaptureIsr()) return; // still draining; retry next poll
        captureDraining = false;
        finishCaptureWrap(now);
        return;
    }
    if (recordPhase == RecordPhase::WaitReady) {
        if (static_cast<int32_t>(micros() - recordReadyDeadlineUs) >= 0) {
            failActive(ActionError::HardwareError, "cc1101 rx ready timeout", now, true);
            return;
        }
        uint8_t marc = 0;
        if (!readStatusRegister(kMarcState, marc)) {
            failActive(ActionError::HardwareError, "cc1101 rx ready read failed", now, true);
            return;
        }
        if (marc != kMarcRx) return; // calibration still running; retry next poll

        pinMode(PIN_CC1101_GDO0, INPUT);
        resetCaptureState();
        captureArmed.store(true, std::memory_order_release);
        attachInterrupt(digitalPinToInterrupt(PIN_CC1101_GDO0), onGdoEdge, CHANGE);
        capturing = true;
        captureDeadlineMs = millis() + captureWindowMs;
        recordPhase = RecordPhase::Capturing;
        return;
    }
    if (recordPhase != RecordPhase::Capturing) return;

    const bool started = captureStarted.load(std::memory_order_acquire) != 0;
    const uint32_t count = captureCount.load(std::memory_order_acquire);
    const uint32_t previous = capturePrevUs.load(std::memory_order_acquire);
    const bool stopped = captureStopped.load(std::memory_order_acquire) != 0;
    const bool burstEnded =
        started && count > 0 && (stopped || static_cast<uint32_t>(micros() - previous) > rfRecord::kMaxDurationUs);
    if (burstEnded) {
        beginCaptureWrap(CaptureWrap::Record, false, now);
        return;
    }
    if (static_cast<int32_t>(now - captureDeadlineMs) >= 0) {
        beginCaptureWrap(CaptureWrap::Timeout, true, now);
    }
}

bool beginBurst() {
    const uint32_t freqHz = (txKind == TxKind::Replay) ? replayRecord.freqHz : customFreqHz;
    if (!tune(freqHz)) return false;
    if (!radioSetTx()) return false;
    txReadyDeadlineUs = micros() + kReadyTimeoutUs;
    txPhase = TxPhase::WaitReady;
    return true;
}

void finishTx(uint32_t now) {
    std::string payload;
    if (txKind == TxKind::Replay) {
        payload = rfRecord::replayToJson(replayRecord, txRepeatTotal);
    } else {
        payload = rfRecord::customTxToJson(customFreqHz, customByteCount, kCustomTxBitPeriodUs, txRepeatTotal);
    }
    const uint32_t ticket = activeTicket;
    activeTicket = 0;
    activeActionId = ActionId::None;
    txKind = TxKind::None;
    txPhase = TxPhase::None;
    releaseRmt();
    if (configured) radioSetIdle();
    runtime.completeAction(ticket, std::move(payload), now);
}

// One burst per poll, then a cooperative gap; never blocks for a gap, a
// calibration, or the waveform (RMT streams it in hardware). A burst only
// starts once MARCSTATE reports TX, so GDO0 is never driven before the PA is up.
void stepTx(uint32_t now) {
    if (runtime.expire(now, ActionError::HardwareError, true)) {
        clearActive();
        return;
    }
    switch (txPhase) {
    case TxPhase::None:
        if (!beginBurst()) failActive(ActionError::HardwareError, "cc1101 tx start failed", now, true);
        return;
    case TxPhase::WaitReady: {
        if (static_cast<int32_t>(micros() - txReadyDeadlineUs) >= 0) {
            failActive(ActionError::HardwareError, "cc1101 tx ready timeout", now, true);
            return;
        }
        uint8_t marc = 0;
        if (!readStatusRegister(kMarcState, marc)) {
            failActive(ActionError::HardwareError, "cc1101 tx ready read failed", now, true);
            return;
        }
        if (marc != kMarcTx) return; // calibration still running; retry next poll
        if (!rmtStart()) {
            failActive(ActionError::HardwareError, "cc1101 rmt write failed", now, true);
            return;
        }
        txPhase = TxPhase::Streaming;
        return;
    }
    case TxPhase::Streaming:
        if (rmt_wait_tx_done(kTxChannel, 0) != ESP_OK) return; // still in hardware
        releaseRmt();
        if (configured) radioSetIdle();
        if (txRepeatsLeft > 0) --txRepeatsLeft;
        if (txRepeatsLeft == 0) {
            finishTx(now);
            return;
        }
        txPhase = TxPhase::Gap;
        txGapUntilMs = millis() + txGapMs;
        return;
    case TxPhase::Gap:
        if (static_cast<int32_t>(now - txGapUntilMs) < 0) return;
        txPhase = TxPhase::None;
        return;
    }
}

#ifdef ENABLE_DISRUPTIVE
void setCarrier(bool on) { digitalWrite(PIN_CC1101_GDO0, on ? HIGH : LOW); }

// Drives the OOK carrier level directly on GDO0 (the CC1101's async data input
// gates PATABLE[1] vs PATABLE[0]). No burst is generated here; full mode just
// holds the level, intermittent toggles it from poll().
CommandError startJammer(const ActionParams &params, const ActionDescriptor &descriptor) {
    uint32_t freqHz = kRecordDefaultFreqHz;
    if (params.present(0)) {
        if (!rfRecord::supportedFrequencyMhz(params.number(0))) return CommandError::InvalidParams;
        freqHz = hzFromMhz(params.number(0));
        if (!rfRecord::supportedFrequencyHz(freqHz)) return CommandError::InvalidParams;
    }

    JamMode mode = JamMode::Full;
    if (params.present(1)) {
        const std::string_view text(params.string(1), params.stringLength(1));
        if (text == "full") mode = JamMode::Full;
        else if (text == "intermittent") mode = JamMode::Intermittent;
        else return CommandError::InvalidParams;
    }

    uint32_t onMs = kJamDefaultOnMs;
    uint32_t offMs = kJamDefaultOffMs;
    if (params.present(2)) {
        int64_t value = params.integer(2);
        if (value < 10) value = 10;
        if (value > 5000) value = 5000;
        onMs = static_cast<uint32_t>(value);
    }
    if (params.present(3)) {
        int64_t value = params.integer(3);
        if (value < 10) value = 10;
        if (value > 5000) value = 5000;
        offMs = static_cast<uint32_t>(value);
    }

    if (!initRadio()) {
        runtime.setHealth(false, "cc1101 init failed", millis());
        return CommandError::HardwareError;
    }

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), 0, ticket, descriptor); // Continuous, no deadline
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    activeActionId = ActionId::RfJammer;
    jamFreqHz = freqHz;
    jamMode = mode;
    jamOnMs = onMs;
    jamOffMs = offMs;
    jamToggles = 0;
    jamStartedMs = millis();
    lastJamPublishMs = jamStartedMs - kMinActionOutputIntervalMs; // first sample may publish immediately

    pinMode(PIN_CC1101_GDO0, OUTPUT);
    if (!tune(freqHz) || !radioSetTx()) {
        failActive(ActionError::HardwareError, "cc1101 jam start failed", millis(), true);
        return CommandError::None;
    }
    jamGate.begin(onMs, offMs, millis());
    setCarrier(true); // full: stays high; intermittent: keyed from stepJammer()
    return CommandError::None;
}

void stepJammer(uint32_t now) {
    if (jamMode == JamMode::Intermittent && jamGate.update(now)) {
        setCarrier(jamGate.carrierOn());
        ++jamToggles;
    }

    if (static_cast<uint32_t>(now - lastJamPublishMs) < kMinActionOutputIntervalMs) return;
    char buf[192];
    const char *modeName = (jamMode == JamMode::Full) ? "full" : "intermittent";
    const int written = snprintf(buf, sizeof(buf),
        "{\"kind\":\"rf_jammer\",\"mode\":\"%s\",\"freqMhz\":%.3f,\"onMs\":%lu,\"offMs\":%lu,"
        "\"toggles\":%lu,\"elapsedMs\":%lu}",
        modeName, static_cast<double>(jamFreqHz) / 1000000.0, static_cast<unsigned long>(jamOnMs),
        static_cast<unsigned long>(jamOffMs), static_cast<unsigned long>(jamToggles),
        static_cast<unsigned long>(now - jamStartedMs));
    if (written <= 0) return;
    const size_t length = (static_cast<size_t>(written) < sizeof(buf)) ? static_cast<size_t>(written) : sizeof(buf) - 1;
    std::string payload(buf, length);
    if (runtime.publishOutput(activeTicket, std::move(payload), now)) lastJamPublishMs = now;
}
#endif

void cleanup(uint32_t now) {
    // Keep the runtime lease until the ISR drain completes; poll() retries.
    if (!stopCaptureIsr()) {
        captureDraining = true;
        return;
    }
    captureDraining = false;
    captureWrap = CaptureWrap::None;
    captureWrapCleanup = false;
    recordPhase = RecordPhase::Idle;
    releaseRmt();
    if (configured) {
        radioSetIdle();
        configured = false;
    }
    txPhase = TxPhase::None;
    txKind = TxKind::None;
    activeTicket = 0;
    activeActionId = ActionId::None;
    sweepPhase = SweepPhase::Idle;
    sweepReady = false;
    runtime.finishCleanup(now);
}

CommandError startScan(const ActionParams &params, const ActionDescriptor &descriptor) {
    if (params.size() < 3 || !params.present(0) || !params.present(1) || !params.present(2)) {
        return CommandError::InvalidParams;
    }
    const double start = params.number(0);
    const double end = params.number(1);
    const int64_t stepKhz = params.integer(2);
    if (stepKhz <= 0) return CommandError::InvalidParams;
    const size_t count =
        rfRecord::sweepPointCount(start, end, static_cast<uint32_t>(stepKhz), rfRecord::kMaxScanPoints);
    if (count == 0) return CommandError::InvalidParams;
    if (!initRadio()) {
        runtime.setHealth(false, "cc1101 init failed", millis());
        return CommandError::HardwareError;
    }

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), kScanDeadlineMs, ticket, descriptor);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    activeActionId = ActionId::RfScan;
    sweepStartMhz = start;
    sweepStepKhz = static_cast<uint32_t>(stepKhz);
    sweepCount = count;
    sweepIndex = 0;
    sweepPhase = SweepPhase::Idle;
    sweepReady = false;
    sweepContinuous = false;
    return CommandError::None;
}

CommandError startSpectrum(const ActionParams &params, const ActionDescriptor &descriptor) {
    const double start = params.present(0) ? params.number(0) : static_cast<double>(kSpectrumDefaultStartMhz);
    const double end = params.present(1) ? params.number(1) : static_cast<double>(kSpectrumDefaultEndMhz);
    const int64_t stepKhz = params.present(2) ? params.integer(2) : kSpectrumDefaultStepKhz;
    if (stepKhz <= 0) return CommandError::InvalidParams;
    const size_t count =
        rfRecord::sweepPointCount(start, end, static_cast<uint32_t>(stepKhz), rfRecord::kMaxScanPoints);
    if (count == 0) return CommandError::InvalidParams;
    if (!initRadio()) {
        runtime.setHealth(false, "cc1101 init failed", millis());
        return CommandError::HardwareError;
    }

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), 0, ticket, descriptor); // Continuous, no deadline
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    activeActionId = ActionId::RfSpectrum;
    sweepStartMhz = start;
    sweepStepKhz = static_cast<uint32_t>(stepKhz);
    sweepCount = count;
    sweepIndex = 0;
    sweepPhase = SweepPhase::Idle;
    sweepReady = false;
    sweepContinuous = true;
    lastSweepPublishMs = millis() - kMinActionOutputIntervalMs; // first sweep may publish immediately
    return CommandError::None;
}

CommandError startRecord(const ActionParams &params, const ActionDescriptor &descriptor) {
    uint32_t freqHz = kRecordDefaultFreqHz;
    if (params.present(0)) {
        if (!rfRecord::supportedFrequencyMhz(params.number(0))) return CommandError::InvalidParams;
        freqHz = hzFromMhz(params.number(0));
        if (!rfRecord::supportedFrequencyHz(freqHz)) return CommandError::InvalidParams;
    }

    uint32_t windowMs = kRecordDefaultWindowMs;
    if (params.present(1)) {
        int64_t value = params.integer(1);
        if (value < static_cast<int64_t>(kRecordMinWindowMs)) value = kRecordMinWindowMs;
        if (value > static_cast<int64_t>(kRecordMaxWindowMs)) value = kRecordMaxWindowMs;
        windowMs = static_cast<uint32_t>(value);
    }

    if (!initRadio()) {
        runtime.setHealth(false, "cc1101 init failed", millis());
        return CommandError::HardwareError;
    }

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), windowMs, ticket, descriptor);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    activeActionId = ActionId::RfRecord;
    captureFreqHz = freqHz;
    captureWindowMs = windowMs;
    captureWrap = CaptureWrap::None;
    captureDraining = false;
    recordPhase = RecordPhase::WaitReady;
    recordReadyDeadlineUs = micros() + kReadyTimeoutUs;
    if (!tune(freqHz) || !radioSetRx()) {
        failActive(ActionError::HardwareError, "cc1101 record start failed", millis(), true);
        return CommandError::None;
    }
    return CommandError::None;
}

CommandError startReplay(const ActionParams &params, const ActionDescriptor &descriptor) {
    const RecordBuffer &buffer = runtime.recordBuffer();
    rfRecord::Record record;
    if (!rfRecord::decode(buffer.data(), buffer.size(), record)) return CommandError::InvalidParams;

    int64_t repeat = params.present(0) ? params.integer(0) : 1;
    if (repeat < 1) repeat = 1;
    if (repeat > 10) repeat = 10;
    int64_t gapMs = params.present(1) ? params.integer(1) : static_cast<int64_t>(kReplayDefaultGapMs);
    if (gapMs < 20) gapMs = 20;
    if (gapMs > 2000) gapMs = 2000;

    if (!initRadio()) {
        runtime.setHealth(false, "cc1101 init failed", millis());
        return CommandError::HardwareError;
    }

    const uint64_t totalMs = rfRecord::totalDurationUs(record) / 1000 + 1;
    uint64_t deadline = static_cast<uint64_t>(repeat) * (totalMs + static_cast<uint64_t>(gapMs)) + 2000;
    if (deadline > kTxDeadlineMaxMs) deadline = kTxDeadlineMaxMs;
    if (deadline < kTxDeadlineMinMs) deadline = kTxDeadlineMinMs;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), static_cast<uint32_t>(deadline), ticket, descriptor);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    activeActionId = ActionId::RfReplay;
    replayRecord = record;
    txKind = TxKind::Replay;
    txPhase = TxPhase::None;
    txRepeatTotal = static_cast<uint32_t>(repeat);
    txRepeatsLeft = static_cast<uint32_t>(repeat);
    txGapMs = static_cast<uint32_t>(gapMs);
    return CommandError::None;
}

CommandError startCustomTx(const ActionParams &params, const ActionDescriptor &descriptor) {
    if (params.size() < 3 || !params.present(0) || !params.present(1)) return CommandError::InvalidParams;
    if (!rfRecord::supportedFrequencyMhz(params.number(0))) return CommandError::InvalidParams;
    const uint32_t freqHz = hzFromMhz(params.number(0));
    if (!rfRecord::supportedFrequencyHz(freqHz)) return CommandError::InvalidParams;

    uint8_t bytes[kCustomTxMaxBytes];
    size_t byteCount = 0;
    if (!rfRecord::decodeHex(params.string(1), params.stringLength(1), bytes, sizeof(bytes), byteCount) ||
        byteCount == 0) {
        return CommandError::InvalidParams;
    }

    int64_t repeat = params.present(2) ? params.integer(2) : 1;
    if (repeat < 1) repeat = 1;
    if (repeat > 10) repeat = 10;

    if (!initRadio()) {
        runtime.setHealth(false, "cc1101 init failed", millis());
        return CommandError::HardwareError;
    }

    const uint64_t perRepeatMs = (static_cast<uint64_t>(byteCount) * 8 * kCustomTxBitPeriodUs) / 1000 + 1;
    uint64_t deadline = static_cast<uint64_t>(repeat) * (perRepeatMs + kCustomTxGapMs) + 2000;
    if (deadline > kTxDeadlineMaxMs) deadline = kTxDeadlineMaxMs;
    if (deadline < kTxDeadlineMinMs) deadline = kTxDeadlineMinMs;

    uint32_t ticket = 0;
    const CommandError error = runtime.beginAction(millis(), static_cast<uint32_t>(deadline), ticket, descriptor);
    if (error != CommandError::None) return error;

    activeTicket = ticket;
    activeActionId = ActionId::RfCustomTx;
    customFreqHz = freqHz;
    std::memcpy(customBytes, bytes, byteCount);
    customByteCount = byteCount;
    txKind = TxKind::Custom;
    txPhase = TxPhase::None;
    txRepeatTotal = static_cast<uint32_t>(repeat);
    txRepeatsLeft = static_cast<uint32_t>(repeat);
    txGapMs = kCustomTxGapMs;
    return CommandError::None;
}

} // namespace

void begin() { spiBus::begin(); }

CommandError setEnabled(bool enabled) {
    const uint32_t now = millis();

    if (enabled) {
        if (runtime.status().enabled) return CommandError::None; // idempotent
        runtime.setEnabled(true, now);

        uint8_t version = 0;
        char detail[32];
        lastHealthMs = now;
        if (!probeChip(version)) {
            runtime.setHealth(false, "cc1101 not responding", now);
            return CommandError::HardwareError;
        }
        describe(version, detail, sizeof(detail));
        runtime.setHealth(true, detail, now);
        return CommandError::None;
    }

    // Stop cancels and idles the chip immediately and releases the RMT channel;
    // the runtime still gets its cleanup lease so cleanup() runs to completion.
    const bool needsCleanup = configured || capturing;
    clearActive();
    runtime.setEnabled(false, now, needsCleanup);
    configured = false;
    return CommandError::None;
}

CommandError handleAction(ActionId action, const ActionParams &params) {
    const ActionDescriptor *descriptor = catalog::findAction(ModuleId::Cc1101, action);
    if (descriptor == nullptr) return CommandError::UnsupportedAction;

    switch (action) {
    case ActionId::RfScan: return startScan(params, *descriptor);
    case ActionId::RfRecord: return startRecord(params, *descriptor);
    case ActionId::RfReplay: return startReplay(params, *descriptor);
    case ActionId::RfSpectrum: return startSpectrum(params, *descriptor);
    case ActionId::RfCustomTx: return startCustomTx(params, *descriptor);
#ifdef ENABLE_DISRUPTIVE
    case ActionId::RfJammer: return startJammer(params, *descriptor);
#endif
    default: return CommandError::UnsupportedAction;
    }
}

void poll() {
    const uint32_t now = millis();

    if (runtime.status().cleanupPending) cleanup(now);
    if (!runtime.status().enabled) return;

    if (runtime.status().actionState == ActionState::Running) {
        switch (activeActionId) {
        case ActionId::RfScan:
        case ActionId::RfSpectrum: stepSweep(now); break;
        case ActionId::RfRecord: stepRecord(now); break;
        case ActionId::RfReplay:
        case ActionId::RfCustomTx: stepTx(now); break;
#ifdef ENABLE_DISRUPTIVE
        case ActionId::RfJammer: stepJammer(now); break;
#endif
        default:
            // Running without a live ticket cannot happen through the admission
            // gate; fail closed so the module can never stall in Running.
            failActive(ActionError::HardwareError, "cc1101 lost action", now, true);
            return;
        }
    }

    // Liveness is independent of the action: a Continuous rf_spectrum stream
    // must still report a chip loss within kHealthRecheckMs (~0.5 s), far below
    // the 2 s target. The probe only touches SPI, never GDO0, so it cannot
    // disturb a capture or a transmit.
    const uint32_t after = millis();
    if (static_cast<uint32_t>(after - lastHealthMs) < kHealthRecheckMs) return;
    lastHealthMs = after;

    uint8_t version = 0;
    char detail[32];
    if (!probeChip(version)) {
        if (runtime.status().actionState == ActionState::Running) {
            failActive(ActionError::HardwareError, "cc1101 not responding", after, true);
        } else {
            runtime.setHealth(false, "cc1101 not responding", after);
        }
        return;
    }
    describe(version, detail, sizeof(detail));
    runtime.setHealth(true, detail, after);
}

const ModuleStatus &status() { return runtime.status(); }

uint32_t revision() { return runtime.revision(); }

bool takeActionOutput(ActionOutput &output) { return runtime.takeActionOutput(output); }

} // namespace cc1101

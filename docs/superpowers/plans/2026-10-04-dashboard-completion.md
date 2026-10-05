# Dashboard Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dashboard dữ liệu thật điều khiển đủ 5 category, WiFi scan/NFC UID/IR capture đầu-cuối, Stop có dung lượng riêng và cleanup quan sát được, nghiệm thu trên ESP32-S3 thật.

**Architecture:** HTML/CSS/JS thuần trên LittleFS, WebSocket `/ws`; callback chỉ parse/enqueue, loop sở hữu driver/state/dispatch/publish. Một pure `ModuleRuntime` dùng chung giữ lifecycle/generation/payload; một ring FIFO 13 phần tử bảo đảm 8 lệnh thường + một Stop/category, không có priority vượt hàng. Firmware và UI nâng cùng contract; không giữ shims, mock fallback hoặc API cũ.

**Tech Stack:** PlatformIO, Arduino ESP32, C++17 application, ESPAsyncWebServer/AsyncTCP, ArduinoJson, LittleFS, JavaScript thuần, Unity native.

**Spec:** [docs/planning/spec.md](../../planning/spec.md), revision 2026-10-04, Q1–Q21 đã duyệt. Đây là plan duy nhất cho đợt này; intent/map là lịch sử, không mở rộng phạm vi.

**Status:** implemented-in-source; hardware acceptance pending. Đã triển khai T1–T10 trong `src/`/`data/` và cập nhật `platformio.ini`. Bằng chứng host đã chạy: `pio test -e native` 50/50 PASS; `pio run -e attak-iot-firmware` SUCCESS; `pio run -e attak-iot-firmware -t buildfs` SUCCESS (`/index.html`, `/dashboard.js`); browser smoke Chromium thật (inject transport) 0 page error. Chưa flash, chưa đo trên board, chưa commit. Các con số 15 giây/2 giây và toàn bộ AC01–AC12 phần cứng còn pending: giữ nguyên spec, nếu không đạt thì trình bằng chứng và xin duyệt sửa spec, không tự nới hoặc pass giả.

### Trạng thái từng task

| Task | Trạng thái | Bằng chứng host |
|---|---|---|
| T1 runtime/status | Done | `test_module_runtime`, `test_status_json` |
| T2 parser/queue | Done | `test_ws_command_json`, `test_command_queue` |
| T3 health/lifecycle 4 peripheral | Done (code) | build ESP32; board smoke pending |
| T4 WebSocket/loop/AP | Done (code) | build ESP32; two-tab board test pending |
| T5 WiFi scan top32/cancel | Done (code) | `test_wifi_scan_result`; scan/15 s measurement pending |
| T6 PN532 UID bounded reader | Done (code) | `test_nfc_frame`, `test_nfc_uid_format`; card read pending |
| T7 IR capture | Done (code) | `test_ir_capture_format`; remote capture pending |
| T8/T9 dashboard transport/log/export | Done | browser smoke (30+ assertion, 0 error, 360 px no overflow) |
| T10 RGB + integration | Done (code) | `test_status_health`; on-board LED + 10-minute heap run pending |

## Global Constraints

- Board: ESP32-S3-N16R8, flash 16MB, octal PSRAM 8MB; giữ `board = esp32-s3-devkitc-1`, `board_build.flash_size = 16MB`, `board_build.arduino.memory_type = qio_opi`, `-DBOARD_HAS_PSRAM`.
- PlatformIO + Arduino; dashboard HTML/CSS/JavaScript thuần trên LittleFS; WebSocket `/ws`; không thêm SPA/framework hoặc backend ngoài ESP32.
- Application C++17 cho firmware và native: parser dùng `std::string_view`; khi triển khai thêm `build_unflags = -std=gnu++11` và `-std=gnu++17` trong env firmware, giữ nguyên core/platform và các flag N16R8. Đây là lựa chọn implementation, không mở rộng tính năng.
- Pin mapping: SPI SCK12/MOSI11/MISO13; CC1101 CS10/GDO8; NRF24 CS14/CE9; PN532 SDA4/SCL5; IR RX6/TX7; RGB48. `include/board_pins.h` là nguồn pin.
- CC1101/NRF24 dùng chung một SPI bus; PN532 dùng I2C. Không copy code AGPL/Bruce; dependency phải tương thích license dự án.
- AP riêng, một người vận hành, nhiều tab được quyền điều khiển; không đăng nhập, không public lên Internet. Người kết nối AP được xem là tin cậy.
- Config AP/deviceName tiếp tục từ `DeviceConfig` và LittleFS `/config.json`; không thêm UI đổi mật khẩu hoặc reset cấu hình.
- Module boot off; off không chạy action/liveness I/O mới, chỉ được cleanup có giới hạn của tác vụ đã hủy. Payload chỉ ở RAM, mất khi disable/reboot; export là tải xuống browser, không lưu payload vào LittleFS.
- Mất browser cuối cùng không dừng module, không xóa payload, không chạy lại action khi reconnect.
- Chỉ thao tác trên thiết bị/mạng/thẻ được phép sử dụng; không bổ sung chức năng gây nhiễu hoặc phát lại trong đợt này.
- Stop hủy logic và xóa payload khi được xử lý; backend có thể cleanup sau. Status có `cleanupPending`, mọi browser hiển thị “Đang giải phóng tài nguyên” và khóa action đến khi cleanup xong; enable không xóa nhãn cleanup.
- Queue bảo đảm 8 lệnh thường và một Stop đang chờ cho mỗi category (5 slot riêng), tổng tối đa 13. FIFO theo thứ tự tiếp nhận chung; Stop không vượt hàng, Stop lặp không chiếm slot của category khác.
- WiFi 15 giây và chip liveness 2 giây là mục tiêu kỹ thuật ban đầu, cần đo trên board; không tự nới hoặc nghiệm thu pass nếu chưa đạt/được duyệt sửa spec. NFC 5 giây và IR 10 giây là timeout đã chốt.

---

## 1. Baseline và điều kiện bắt đầu

- Baseline được đọc: `data/index.html:86-208` chỉ 4 module, mock fallback, không lệnh/close/reconnect; `src/main.cpp:15-42` chưa WiFi/dispatcher, broadcast mọi loop; 4 initializer module còn theo struct cũ. Chưa khẳng định firmware hiện tại build được.
- `pio test -e native` 12/12 pass là bằng chứng từ lần khảo sát trước, không phải pass của plan mới.
- Callsite map: `ModuleStatus` ở module/status codec/web transport/main/native status tests; `parseWsCommand` ở web transport và command tests; WiFi void setEnabled/handleAction chỉ tồn tại trong WiFi hiện tại; onCommand chưa được gọi từ main. LSP references đã thử nhưng môi trường báo không có server, nên dùng literal references trong src/test. Khi triển khai, nếu LSP có sẵn phải chạy references/blast-radius trước đổi public API.
- Read-before-edit; sử dụng lại env/native Unity và test runner hiện có. Không thêm permanent UI tests, framework/bundler hoặc mocking framework.
- Trước task driver: đọc dependency **thực cài** trong `.pio/libdeps/attak-iot-firmware/` và core ESP32; ghi API/timeout/backend đã xác minh. Không dựa comments WiFi hay giả định IR dùng RMT.
- Phần cứng: board đúng N16R8, 4 module đúng pin/jumper, 2 thẻ khác UID, remote, AP thử có BSSID phân biệt, laptop/browser và serial. Thiếu board không ngăn native/plan nhưng chặn đánh dấu hardware acceptance hoàn thành.

## 2. Bản đồ file và ownership

| File | Trách nhiệm / task |
|---|---|
| `src/core/command_types.h` (new) | Typed ModuleId/CommandKind/ActionId/CommandError, không Arduino; T1/T2. |
| `src/core/module_status.h`, `module_status_json.cpp` | Status wire mở rộng, enum action/error + cleanup/result metadata; T1. |
| `src/core/module_runtime.{h,cpp}` (new) | Pure transition/deadline/generation, payload cũ và cleanup; T1. |
| `src/core/ws_command.{h}`, `ws_command_json.cpp` | Strict JSON parser, correlation và result codec; T2. |
| `src/core/command_queue.{h,cpp}` (new) | Ring FIFO 13, admission 8+5, không heap; T2. |
| `src/modules/{cc1101,nrf24,pn532,ir}_module.{h,cpp}` | Real health/enable/disable và API mới; T3; PN532 payload T6, IR payload T7. |
| `src/modules/wifi_module.{h,cpp}` | API/lifecycle và scan/cleanup; T4 chuẩn bị migration, T5 hoàn tất. |
| `src/core/web_dashboard.{h,cpp}`, `src/main.cpp` | Sync queue bridge, client/session correlation, loop dispatch và paced snapshots; T4. Main chỉ một integration owner. |
| `src/core/wifi_scan_result.{h,cpp}` (new) | Bound/order top-32 AP, serialize output một lần; T5. |
| `src/core/nfc_uid_format.{h,cpp}` (new) | UID normalization/output; thêm `src/core/nfc_frame.{h,cpp}` cho bounded PN532 protocol parser; T6. |
| `src/core/ir_capture_format.{h,cpp}` (new) | Raw timing normalization/repeat/boundary và output; T7. |
| `data/index.html`, `data/dashboard.js` (new JS) | Shell/style + một JS state/transport/render/export; T8/T9. |
| `src/core/status_led.{h,cpp}` (new) | ESP32 RGB wrapper; pure aggregate nằm trong `src/core/status_health.{h,cpp}`; T10. |
| `test/test_module_runtime/`, `test/test_command_queue/`, existing command/status tests | Transition/precedence/reserved Stop/parser tests; T1/T2. |
| `test/test_wifi_scan_result/`, `test/test_nfc_frame/`, `test/test_nfc_uid_format/`, `test/test_ir_capture_format/`, `test/test_status_health/` | Top-N, UID padding, timing bounds, enabled-only aggregate; task tương ứng. |
| `platformio.ini` | C++17 app và native whitelist chính xác, không đưa driver/network vào native; từng task owner nối sau khi hợp nhất. |
| `README.md`, `docs/planning/README.md`, tickets 05–09 | Cách vận hành/chức năng đã có + criteria hiện hành; task tương ứng sau smoke. |

Đường dẫn từ đây tính từ `attak-iot-firmware/`. File new là dự kiến của implementation, chưa được tạo bởi lần viết plan.

## 3. Hợp đồng giữa các task

### 3.1 Typed commands và status (T1/T2)

```cpp
// core/command_types.h, application C++17
#pragma once
#include <cstddef>
#include <cstdint>
enum class ModuleId : uint8_t { Cc1101, Nrf24, Pn532, Ir, Wifi, Unknown };
enum class CommandKind : uint8_t { Enable, Disable, Action };
enum class ActionId : uint8_t { None, Scan, ReadUid, Capture };
enum class CommandError : uint8_t {
    None, InvalidCommand, UnsupportedAction, QueueFull, ModuleOff, Busy, HardwareError
};
const char* moduleName(ModuleId id); // Unknown -> ""
const char* commandErrorName(CommandError error); // None -> ""
```

Giữ sáu trường `ModuleStatus` hiện có, bổ sung defaults:

```cpp
enum class ActionState : uint8_t { Idle, Running, Succeeded, Timeout, Error };
enum class ActionError : uint8_t {
    None, ScanFailed, ScanTimeout, ReadTimeout, CaptureTimeout, CaptureTooLong, HardwareError
};
// trailing fields in ModuleStatus
ActionState actionState = ActionState::Idle;
ActionError actionError = ActionError::None;
bool cleanupPending = false;
uint32_t resultSequence = 0;
uint32_t resultUpdateMs = 0;
```

Codec giữ tên trường wire của spec, enum -> đúng string, never cast enum number lên wire. `moduleName` và error enum mapping là switch cố định; không registry/string map.

```cpp
// core/ws_command.h
struct WsCommand {
    uint32_t id = 0;
    ModuleId module = ModuleId::Unknown;
    CommandKind cmd = CommandKind::Enable;
    ActionId action = ActionId::None;
    CommandError error = CommandError::InvalidCommand;
    bool correlationValid = false; // schema-valid id + module; not command validity
};
WsCommand parseWsCommand(std::string_view json);
std::string commandResultToJson(uint32_t id, ModuleId module, CommandError error);
```

Invalid parse không thực thi; unknown action đúng kiểu => UnsupportedAction, không InvalidCommand. `error==None` là admission hợp lệ; bỏ field `valid` cũ và migrate hết callers/tests. Nếu correlationValid=false, reply id=0/module=""; không resolve pending nào.

### 3.2 Lifecycle pure và module API (T1/T3/T5–T7)

```cpp
// core/module_runtime.h
class ModuleRuntime {
public:
    explicit ModuleRuntime(const char* name);
    const ModuleStatus& status() const;
    uint32_t revision() const; // changes observable fields; heartbeat-only timestamp need not dirty
    void setEnabled(bool enabled, uint32_t now, bool cleanupRequired = false);
    void setHealth(bool connected, const std::string& detail, uint32_t now);
    CommandError beginAction(uint32_t now, uint32_t deadlineMs, uint32_t& ticket);
    bool completeAction(uint32_t ticket, std::string payload, uint32_t now);
    bool failAction(uint32_t ticket, ActionError error, uint32_t now, bool cleanupRequired);
    bool expire(uint32_t now, ActionError timeoutError, bool cleanupRequired);
    void finishCleanup(uint32_t now); // only called after backend really released
private:
    ModuleStatus current_;
    uint32_t epoch_ = 0, startedMs_ = 0, deadlineMs_ = 0, revision_ = 0;
};
```

- `setEnabled(false)` increments epoch (invalidates ticket), off/idle, clears output/resultUpdateMs, preserves resultSequence; OR cleanupRequired với cleanup đang có. Enable lại không clear cleanup; idempotent enable không reset action.
- `setHealth` chỉ cập nhật health/detail; module mất chip phải failAction(activeTicket,HardwareError,..) trước khi setHealth(false).
- beginAction precedence off → running/cleanup → health; success increments epoch, returns ticket, records injected clock/deadline, clears actionError, keeps old output.
- completeAction only current epoch + Running; move payload, increment sequence, update result time, mark Succeeded. Stale completion has no effect.
- failAction same ticket guard, preserves output/sequence/result time, invalidates epoch, maps timeout codes to Timeout and other errors to Error, OR cleanup flag.
- expire elapsed wrap-safe: `static_cast<uint32_t>(now - startedMs_) >= deadlineMs_`; không Date/absolute addition.
- finishCleanup only removes flag after driver confirmation; không enable module hoặc restart action.

Mỗi namespace `cc1101`, `nrf24`, `pn532`, `ir`, `wifiModule` expose:

```cpp
void begin();
CommandError setEnabled(bool enabled);
CommandError handleAction(ActionId action);
void poll();
const ModuleStatus& status();
uint32_t revision();
```

C++ return const reference, không copy payload mỗi loop. begin chỉ hạ tầng/off; enable mới health I/O; poll chạy cleanup khi off nhưng không liveness/action mới. Driver giữ ticket đang active để kiểm tra completion/cancel. Không giữ void/string action API cũ hoặc shims.

### 3.3 Queue và transport (T2/T4)

```cpp
struct EnqueuedCommand {
    uint32_t clientId = 0;
    uint64_t sessionToken = 0;
    WsCommand command;
};
class CommandQueue {
public:
    CommandError push(const EnqueuedCommand& request);
    bool pop(EnqueuedCommand& request);
private:
    std::array<EnqueuedCommand, 13> items_{};
    std::array<bool, 5> stopQueued_{};
    size_t head_ = 0, size_ = 0, normalCount_ = 0;
};
```

Chỉ valid enum/scalar trong queue, không JSON/strings/driver pointers. Callback/loop sync ngoài pure queue. Queued Stop slot tự giải phóng khi dequeue, không chờ backend cleanup; duplicate Stop cùng module đang queued bị từ chối, không dùng slot khác.

Transport API thay callback-mutating cũ bằng explicit dequeue:

```cpp
namespace webDashboard {
void begin();
bool nextCommand(EnqueuedCommand& request);
void reply(const EnqueuedCommand& request, CommandError error);
void publishStatus(const ModuleStatus& status);
void loop();
}
```

Migrate mọi caller rồi xóa onCommand/CommandHandler; main dispatch switch ModuleId, không callback vào driver. sessionToken nhận tại connection, scope ack tới đúng session; client rời vẫn thực thi command queued nhưng không gửi ack cho session mới.

## 4. Thứ tự, review gate và chứng cứ

T1 → T2 → T3 → T4 → T5 → T6 → T7 → T8 → T9 → T10 là đường tuần tự mặc định. Sau T1–T4 có thể tách T5/T6/T7 và T8/T9 theo file ownership; main/platformio do một owner tích hợp, không nhiều người sửa chung. Không chạy build/lint/tests giữa chừng của wave; hợp nhất rồi chạy một lượt.

Mỗi task: đọc files/callers, viết regression cho uncertain behavior trước, run fail, implement minimal, run covering native suite một lần sau edits, smoke bề mặt thật và lưu chứng cứ. UI chỉ browser smoke + throwaway injections. Không tự commit/flash từ yêu cầu viết plan; khi được giao implementation thì checkpoint commit chỉ sau smoke và phải có quyền tương ứng.

**Gate target phần cứng:** T3 đo mất/phục hồi chip so với 2 giây; T5 đo scan/deadline so với 15 giây. Ghi samples ở Serial + browser, cấu hình board/core/driver và time start/end. Nếu không đạt: đánh dấu acceptance đang blocked, giữ limit ban đầu trong code/doc, trình bằng chứng và xin duyệt thay spec; không tự sửa timeout để làm test pass. Hoàn thành các phần độc lập có thể kiểm tra nhưng không gọi đợt đã nghiệm thu.

## 5. Task triển khai

### Task 1 — State/result lifecycle portable và schema status

**Files:** Create `src/core/command_types.h`, `src/core/module_runtime.h`, `src/core/module_runtime.cpp`, `test/test_module_runtime/test_module_runtime.cpp`; Modify `src/core/module_status.h`, `src/core/module_status_json.cpp`, `test/test_status_json/test_status_json.cpp`, `platformio.ini`.

**Dependencies:** Không phụ thuộc driver. **Consumes:** baseline ModuleStatus. **Produces:** typed status/ModuleRuntime §3.1–3.2; clocks injected, no Arduino. **Coverage:** R08/R09/R13/R17/R21; AC09 và pure portion AC03.

- [ ] **Ghi baseline và compiler contract.** Đọc platformio.ini; native đã C++17. Thêm firmware `build_unflags = -std=gnu++11` và `-std=gnu++17` vào build_flags, giữ mọi flag N16R8. Đây chỉ application standard, không thay core/platform hoặc license. Khi implementation bắt đầu, ghi baseline `pio run -e attak-iot-firmware` nếu chưa có user-reported build failure; không rerun chỉ để xác nhận lỗi người dùng đã báo.
- [ ] **Viết regression trước implementation.** Native suite có setUp/tearDown và Unity main đăng ký các hàm dưới. Các enum/classes là contract §3; assertions bắt consumer-visible retention/cancellation, không field-copy codec.

```cpp
#include <unity.h>
#include "core/module_runtime.h"
void setUp() {}
void tearDown() {}
void test_timeout_preserves_previous_result() {
    ModuleRuntime runtime("pn532");
    runtime.setEnabled(true, 1);
    runtime.setHealth(true, "ready", 2);
    uint32_t first = 0, second = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
        static_cast<int>(runtime.beginAction(10, 5000, first)));
    TEST_ASSERT_TRUE(runtime.completeAction(first, R"({"kind":"nfc_uid","uid":"04:AB:01:02"})", 20));
    runtime.beginAction(30, 5000, second);
    TEST_ASSERT_FALSE(runtime.expire(5029, ActionError::ReadTimeout, false));
    TEST_ASSERT_TRUE(runtime.expire(5030, ActionError::ReadTimeout, false));
    TEST_ASSERT_EQUAL_STRING(R"({"kind":"nfc_uid","uid":"04:AB:01:02"})", runtime.status().output.c_str());
    TEST_ASSERT_EQUAL_UINT32(1, runtime.status().resultSequence);
    TEST_ASSERT_EQUAL_UINT32(20, runtime.status().resultUpdateMs);
    TEST_ASSERT_EQUAL(static_cast<int>(ActionState::Timeout), static_cast<int>(runtime.status().actionState));
}
void test_cancel_reenable_rejects_old_completion_and_keeps_cleanup() {
    ModuleRuntime runtime("wifi");
    runtime.setEnabled(true, 1); runtime.setHealth(true, "ready", 2);
    uint32_t oldTicket = 0, newTicket = 0;
    runtime.beginAction(3, 15000, oldTicket);
    runtime.setEnabled(false, 4, true);
    runtime.setEnabled(true, 5); runtime.setHealth(true, "ready", 6);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::Busy),
        static_cast<int>(runtime.beginAction(7, 15000, newTicket)));
    TEST_ASSERT_FALSE(runtime.completeAction(oldTicket, "old", 8));
    TEST_ASSERT_TRUE(runtime.status().output.empty());
    runtime.finishCleanup(9);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
        static_cast<int>(runtime.beginAction(10, 15000, newTicket)));
    TEST_ASSERT_FALSE(runtime.completeAction(oldTicket, "late", 11));
    TEST_ASSERT_TRUE(runtime.completeAction(newTicket, "new", 12));
    TEST_ASSERT_EQUAL_STRING("new", runtime.status().output.c_str());
}
void test_deadline_wrap_and_identical_results() {
    ModuleRuntime runtime("ir");
    runtime.setEnabled(true, 0); runtime.setHealth(true, "ready", 0);
    uint32_t ticket = 0;
    runtime.beginAction(0xfffffff0u, 32, ticket);
    TEST_ASSERT_FALSE(runtime.expire(0x0fu, ActionError::CaptureTimeout, false));
    TEST_ASSERT_TRUE(runtime.expire(0x10u, ActionError::CaptureTimeout, false));
    runtime.beginAction(20, 100, ticket); runtime.completeAction(ticket, "same", 21);
    runtime.beginAction(22, 100, ticket); runtime.completeAction(ticket, "same", 23);
    TEST_ASSERT_EQUAL_UINT32(2, runtime.status().resultSequence);
    TEST_ASSERT_EQUAL_UINT32(23, runtime.status().resultUpdateMs);
}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_timeout_preserves_previous_result);
    RUN_TEST(test_cancel_reenable_rejects_old_completion_and_keeps_cleanup);
    RUN_TEST(test_deadline_wrap_and_identical_results);
    return UNITY_END();
}
```

- [ ] **Run red:** `pio test -e native -f test_module_runtime`; expected FAIL vì runtime/header chưa có hoặc retention/generation chưa đúng. Không sửa test để theo old behavior.
- [ ] **Implement constructor/methods theo §3.2.** Điểm phải code đúng, không tạo action backlog:

```cpp
CommandError ModuleRuntime::beginAction(uint32_t now, uint32_t deadlineMs, uint32_t& ticket) {
    if (!current_.enabled) return CommandError::ModuleOff;
    if (current_.cleanupPending || current_.actionState == ActionState::Running) return CommandError::Busy;
    if (!current_.connected) return CommandError::HardwareError;
    ticket = ++epoch_;
    startedMs_ = now; deadlineMs_ = deadlineMs;
    current_.actionState = ActionState::Running; current_.actionError = ActionError::None;
    current_.lastUpdateMs = now; ++revision_;
    return CommandError::None;
}
bool ModuleRuntime::completeAction(uint32_t ticket, std::string payload, uint32_t now) {
    if (ticket != epoch_ || current_.actionState != ActionState::Running) return false;
    current_.output = std::move(payload);
    ++current_.resultSequence; current_.resultUpdateMs = now; current_.lastUpdateMs = now;
    current_.actionState = ActionState::Succeeded; current_.actionError = ActionError::None;
    ++revision_; return true;
}
```

Implement fail/expire/disable/finishCleanup bằng rules §3.2; constructor initializes off and zero defaults, revisions increment khi state observable đổi. Không clear payload trong begin/fail. Native filter thêm `+<core/module_runtime.cpp>`; command_types helpers implement trong `module_runtime.cpp` hoặc existing command codec, không create framework.
- [ ] **Status codec và test cleanup.** Thêm enum mapping và 5 metadata fields + cleanupPending theo spec; xóa 3 test field-echo cũ `test_connected_status_encodes_correctly`, `test_disabled_status_encodes_correctly`, `test_output_field_carries_payload_result`. Giữ escaping regression nhưng deserialize JSON rồi assert detail gốc (quote/backslash/newline/Unicode), không pin raw textual formatting. Status lifecycle proof dùng runtime tests ở trên và browser smoke, không re-pin field copies.
- [ ] **Run green/smoke pure seam:** `pio test -e native -f test_module_runtime -f test_status_json`; expected PASS, test cancel giữ cleanup cho đến finishCleanup. Ghi red/green output. Firmware clean build gate ở T3/T4 sau migrate tất cả initializer/callers, không claim hardware pass từ T1.

### Task 2 — Parser strict và FIFO bảo đảm Stop cả 5 category

**Files:** Modify `src/core/ws_command.h`, `src/core/ws_command_json.cpp`, `test/test_ws_command_json/test_ws_command_json.cpp`, `platformio.ini`; Create `src/core/command_queue.h`, `src/core/command_queue.cpp`, `test/test_command_queue/test_command_queue.cpp`.

**Dependencies:** T1 types. **Consumes:** §3.1 commands + §3.3 queue. **Produces:** parseWsCommand(string_view), commandResultToJson, CommandQueue; no driver/network. **Coverage:** R03/R13/R14/R18/R20; AC03/AC05 (pure/protocol edges).

- [ ] **Parser red tests.** Replace old replay-is-valid case; no compatibility alias. Add id/string/fractional/overflow/zero invalid, unknown module/cmd invalid, action missing/empty invalid, unsupported action distinct. Preserve malformed/missing-module/unrecognized-cmd tests with new error assertions. Concrete distinguishing case:

```cpp
void test_unknown_action_is_not_malformed_command() {
    auto unsupported = parseWsCommand(R"({"id":1,"module":"ir","cmd":"action","action":"replay"})");
    auto malformed = parseWsCommand(R"({"id":2,"module":"ir","cmd":"action","action":""})");
    TEST_ASSERT_TRUE(unsupported.correlationValid);
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::UnsupportedAction), static_cast<int>(unsupported.error));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::InvalidCommand), static_cast<int>(malformed.error));
}
```

- [ ] **Queue red tests, full capacity/duplicate/FIFO.** Use local helper below and Unity main registering both tests; do not test a copied queue length as substitute for admission and order.

```cpp
EnqueuedCommand makeRequest(uint32_t id, ModuleId module, CommandKind kind) {
    EnqueuedCommand request;
    request.clientId = 7; request.sessionToken = 1;
    request.command.id = id; request.command.module = module; request.command.cmd = kind;
    request.command.error = CommandError::None; request.command.correlationValid = true;
    return request;
}
void test_normal_saturation_preserves_all_five_stops_in_fifo() {
    CommandQueue queue;
    for (uint32_t id = 1; id <= 8; ++id)
        TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
            static_cast<int>(queue.push(makeRequest(id, ModuleId::Wifi, CommandKind::Enable))));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::QueueFull),
        static_cast<int>(queue.push(makeRequest(99, ModuleId::Wifi, CommandKind::Enable))));
    for (uint8_t module = 0; module < 5; ++module)
        TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
            static_cast<int>(queue.push(makeRequest(9 + module, static_cast<ModuleId>(module), CommandKind::Disable))));
    EnqueuedCommand out;
    for (uint32_t id = 1; id <= 13; ++id) {
        TEST_ASSERT_TRUE(queue.pop(out)); TEST_ASSERT_EQUAL_UINT32(id, out.command.id);
    }
    TEST_ASSERT_FALSE(queue.pop(out));
}
void test_duplicate_stop_does_not_take_another_category_slot() {
    CommandQueue queue;
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
        static_cast<int>(queue.push(makeRequest(1, ModuleId::Ir, CommandKind::Disable))));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::QueueFull),
        static_cast<int>(queue.push(makeRequest(2, ModuleId::Ir, CommandKind::Disable))));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
        static_cast<int>(queue.push(makeRequest(3, ModuleId::Wifi, CommandKind::Disable))));
    EnqueuedCommand out; TEST_ASSERT_TRUE(queue.pop(out));
    TEST_ASSERT_EQUAL(static_cast<int>(CommandError::None),
        static_cast<int>(queue.push(makeRequest(4, ModuleId::Ir, CommandKind::Disable))));
    TEST_ASSERT_TRUE(queue.pop(out)); TEST_ASSERT_EQUAL_UINT32(3, out.command.id);
    TEST_ASSERT_TRUE(queue.pop(out)); TEST_ASSERT_EQUAL_UINT32(4, out.command.id);
}
```

- [ ] **Run red:** `pio test -e native -f test_ws_command_json -f test_command_queue`; expected FAIL before new parser/queue. Add pure queue to native whitelist.
- [ ] **Implement parse/admission.** deserialize buffer view with explicit length, strict JSON integer/boolean/string types; validate id+module first for correlation, then cmd/action/allowlist. If parse impossible use correlationValid=false; known unsupported action returns error with correlation. Reject >512 bytes without constructing copied inbound std::string. Queue algorithm:

```cpp
CommandError CommandQueue::push(const EnqueuedCommand& request) {
    const auto& command = request.command;
    if (command.error != CommandError::None) return command.error;
    const size_t module = static_cast<size_t>(command.module);
    if (!command.correlationValid || module >= stopQueued_.size()) return CommandError::InvalidCommand;
    const bool stop = command.cmd == CommandKind::Disable;
    if (size_ == items_.size() || (stop ? stopQueued_[module] : normalCount_ == 8)) return CommandError::QueueFull;
    items_[(head_ + size_) % items_.size()] = request;
    ++size_;
    if (stop) stopQueued_[module] = true; else ++normalCount_;
    return CommandError::None;
}
bool CommandQueue::pop(EnqueuedCommand& request) {
    if (size_ == 0) return false;
    request = items_[head_]; head_ = (head_ + 1) % items_.size(); --size_;
    if (request.command.cmd == CommandKind::Disable)
        stopQueued_[static_cast<size_t>(request.command.module)] = false;
    else --normalCount_;
    return true;
}
```

Pure queue không thread-safe riêng; T4 khóa ngắn ở admission/pop. Stop giữ FIFO chung; không drain toàn bộ Stops trước normals.
- [ ] **Codec/green/smoke admission:** result schema spec type/id/module/ok/error; None => ok true, error empty. `pio test -e native -f test_ws_command_json -f test_command_queue`; expected PASS. Fault-injection proof queue13 + duplicate is protocol/pure evidence, không claim board/drivers. Real saturation/client routing in T4/T10.


### Task 3 — Health thật và lifecycle bốn peripheral, I/O có giới hạn

**Files:** Create `src/modules/spi_bus.h`, `src/modules/spi_bus.cpp`; Modify `src/modules/cc1101_module.{h,cpp}`, `src/modules/nrf24_module.{h,cpp}`, `src/modules/pn532_module.{h,cpp}`, `src/modules/ir_module.{h,cpp}`.

**Dependencies:** T1/T2. **Consumes/produces:** API module §3.2, including `revision()`; runtime trả const reference. **Coverage:** R02/R08/R16/R17/R21; AC01/AC02/AC03/AC12. T6/T7 hoàn tất action PN532/IR; không nghiệm thu intermediate unsupported-action body như sản phẩm cuối.

- [ ] **Preflight dependency và pin:** đọc source thực cài, không chỉ header; xác minh constructor, timeout, transaction exit, single-instance IR. Đọc ticket 05–08 như lịch sử; tiêu chí boot auto-poll cũ không áp dụng. `begin()` chỉ dựng hạ tầng/off. `setEnabled(true)` áp dụng enabled, thực hiện bring-up có giới hạn; failure trả `HardwareError`, vẫn enabled. Enable lặp là no-op; mọi `poll()` xử lý cleanup trước, off không health/action I/O mới.
- [ ] **Một SPI owner:** `spiBus::begin()` idempotent, CS10/CS14 high, CE9 low trước `SPI.begin(PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI,-1)`. `SPIClass& spiBus::instance()` trả `SPI`. Driver chỉ chạy trong loop; không callback/ISR truy cập SPI và không end/re-begin bus mỗi giao dịch.
- [ ] **CC1101 health-only bằng bounded status read:** installed SmartRC `checkMISO()` trên IDF<5 có vòng chờ tới 1000ms; early return bỏ qua `SpiEnd()`. `getCC1101()` chỉ thử VERSION>0, nên 0xFF floating MISO có thể pass. Không gọi đường này cho health loop. Viết hai static helpers trong `cc1101_module.cpp`: `bool readStatusRegister(uint8_t address,uint8_t& value)` và `bool probeChip(uint8_t& version)`. Giao dịch SPI mode0/2MHz, CS low, chờ SO/MISO low tối đa 5000µs bằng elapsed subtraction; đọc header `address|0xC0` rồi một byte; mọi exit đưa CS high và `endTransaction()`. Probe PARTNUM address0x30 phải 0x00, VERSION0x31 phải khác 0x00/0xFF; detail giữ version thực đọc. PARTNUM=0 là hợp lệ, không áp dụng quy tắc mọi register non-zero. Đây là adapter tối thiểu cho identity-only, không cần cấu hình RF modem/replay. Giữ dependency đã khai báo; không patch `.pio`, không vendoring toàn thư viện. Nguồn: [TI CC1101 datasheet, §10 và status registers](https://www.ti.com/lit/ds/symlink/cc1101.pdf). Khi disable không gọi TX/RX; không có backend RF action cần cleanup.
- [ ] **NRF24:** một `RF24 radio(PIN_NRF24_CE,PIN_NRF24_CS)`; enable dùng `radio.begin(&spiBus::instance()) && radio.isChipConnected()`. Installed RF24 begin(pointer) không SPI.begin, delay(5) có giới hạn; isChipConnected đối chiếu SETUP_AW. CE giữ low, không startListening/write/probe packet; disable `powerDown()` trong poll nếu backend đã bật, dùng cleanup flag cho release đó. Mọi early failure giữ CS high/bus reusable.
- [ ] **PN532:** Wire infrastructure `Wire.begin(PIN_PN532_SDA,PIN_PN532_SCL); Wire.setTimeOut(20)`. Không dùng `Adafruit_PN532::begin()`/getFirmwareVersion với default wait dài và không gọi readPassiveTargetID. T6 định nghĩa project-owned bounded transaction cho GetFirmwareVersion, SAMConfiguration, SetParameters, RFConfiguration và read_uid. T3 chuẩn bị runtime/header; bring-up thật của PN532 kết thúc cùng T6 trước integration acceptance. Presence phải là firmware response IC=0x32, không chỉ I2C address ACK. Không dùng GPIO255 như reset/IRQ giả; IRQ/RSTPD_N không có trong mapping.
- [ ] **IR:** chỉ một IRrecv với raw buffer514 (constructor một lần, không new mỗi enable/poll); enableIRIn/disableIRIn. `connected=true` chỉ khi RX initialized, detail nói rõ không detect physical sensor presence. Không IRsend, không drive TX7. Disable logic trước rồi release receiver trong poll, clear cleanup sau disableIRIn.
- [ ] **Liveness:** CC1101/NRF24/PN532 nhịp 500ms khi enabled; missing/recovery giữ ý định enable, không tự action. Trước health=false phải fail active ticket với HardwareError và cleanup thật nếu có. Không serialize hoặc copy output ở các probe không đổi state; revision chỉ đổi khi observable state/detail đổi. Retry bring-up ở nhịp health, không mỗi vòng loop.
- [ ] **Smoke:** sau hợp nhất T3/T4/T6/T7, `pio run -e attak-iot-firmware`, upload khi có quyền và board; mở Serial/browser. Boot all off; enable từng chip thiếu/có; CC1101+NRF24 cùng lúc; rút/cắm lại và ghi khoảng mất/phục hồi so target2s. Đo loop không bị SmartRC1s wait; kiểm tra CS được release trên lỗi. IR thiếu sensor vẫn chỉ có nhãn RX initialized, không tuyên bố presence. Enable trong cleanup không bỏ flag. Không thêm native tests chỉ echo driver calls; runtime regression T1 + board là proof.

### Task 4 — WebSocket bridge, AP/config và dispatcher thuộc loop

**Files:** Modify `src/core/web_dashboard.h`, `src/core/web_dashboard.cpp`, `src/main.cpp`, `src/modules/wifi_module.{h,cpp}`; consume existing `src/core/storage.*`, `include/board_pins.h`. Không đổi schema DeviceConfig hoặc AP credentials.

**Dependencies:** T1–T3; T5/T6/T7 hoàn tất backend trước acceptance. **Produces:** `nextCommand/reply/publishStatus/loop` §3.3, five-module main orchestration. **Coverage:** R03/R04/R13/R14/R17/R18/R20/R21; AC01/AC03/AC04/AC05.

- [ ] **Migration sạch:** bỏ `CommandHandler/onCommand` và mọi `void setEnabled`/string handleAction caller. Main dùng ModuleId switch và 5 namespace đúng tên. WiFi migration hoàn tất trong T5; không giữ alias hoặc fake ready/action placeholder để gọi firmware đã hoàn thành.
- [ ] **Frame ingress:** WS_EVT_DATA chỉ chấp nhận `info->final && info->index==0 && info->len==len && info->opcode==WS_TEXT && len<=512`. Không ráp fragment hoặc đọc data[len] như NUL. Parse `std::string_view(reinterpret_cast<const char*>(data),len)` ngoài lock. Binary/fragment/oversized: invalid_command, correlation id0/module rỗng nếu không parse được toàn message. Schema/unsupported trả result ngay, không enqueue; queue_full cũng trả ngay. Valid được enqueue, không success ack trước dispatch. Callback tuyệt đối không gọi driver/module status hoặc setEnabled.
- [ ] **Synchronization/session:** queue và `std::map<uint32_t,uint64_t> liveSessions` nằm dưới một `std::mutex` (task-context mutex, không port critical section chứa allocator). WS connect gán token tăng uint64; disconnect erase live session nhưng không xóa queued request/disable module. Parse/serialize/ws.text và I/O ngoài lock; lock chỉ map lookup/mutation hoặc pure push/pop. Không giữ AsyncWebSocketClient pointer trong queue. `reply` kiểm tra clientId+token còn live rồi gửi targeted `ws.text(clientId,json.c_str(),json.size())`, không broadcast. Installed server text(id) tự lock lifetime lookup, IDs tăng monotonic; không reset/reuse server giữa runtime. Hai tab id17 được ack riêng; old-session result không resolve session mới.
- [ ] **Dispatcher:** `CommandError dispatchCommand(const WsCommand&)` trong anonymous namespace main, `switch(module)` gọi đúng setEnabled(true/false) hoặc handleAction(action). Unknown → InvalidCommand. Helper `dispatchToModule(const WsCommand&, CommandError(*)(bool), CommandError(*)(ActionId))` dùng switch CommandKind, không std::function/heap registry. `loop()` pop tối đa13 accepted commands một lượt theo FIFO, dispatch rồi reply; off/busy/hardware precedence ở runtime. Không giữ action đến khi running hết; Stop ack sau logical cancel/clear payload, trước cleanup là hợp lệ. Enable/disable không bị busy.
- [ ] **Main scheduling:** begin đủ5 category, WiFi begin không scan; lưu const-reference statuses hoặc function pointers, không copy struct chứa string. Poll cả5 mọi vòng kể cả off để cleanup; driver poll không đợi trọn5/10s. Heartbeat đủ5 mỗi1000ms, dirty coalesce khi revision đổi và elapsed từ lần publish cuối >=100ms/module; heartbeat cùng vòng không gửi bản dirty lần hai. lastUpdateMs là state uptime, không trẻ hóa resultUpdateMs. Gửi snapshot mới ≤1s sau client open bằng heartbeat, hoặc connect flag yêu cầu snapshot ở loop với cùng rate cap. Web loop chỉ cleanupClients/network upkeep, không mutate module.
- [ ] **AP/config:** tiếp tục load DeviceConfig từ LittleFS/config.json, sử dụng AP/password/deviceName thật của baseline. WiFi scan không disconnect/teardown AP; off WiFi category không tắt AP. Không thêm login/STA provisioning/config endpoint. Đọc nguồn core T5 để chứng minh AP+STA giữ AP.
- [ ] **Protocol smoke trên board + browser thật:** hai tab gửi cùng id với hai command khác nhau; assert từng tab chỉ nhận result của nó, cả hai thấy state. Close tab có command queued: module vẫn thực thi, tab mới không nhận ack cũ. Gửi invalid types/malformed/oversized/binary/fragmented từ throwaway transport, assert không action và lỗi đúng correlation. Saturation dùng tạm pause dequeue sau ingress (gỡ trước upload sản phẩm): nhận8 normal, normal9 queue_full, 5 Stop accepted, Stop lặp queue_full, resume thấy FIFO1..13. Đây là injection, không proof radio. Đo snapshot/dirty pacing bằng frame receive timestamps, không source-text test.
- [ ] **Check:** `pio test -e native` + `pio run -e attak-iot-firmware` sau hợp nhất wave. Launch dashboard AP, chuyển enabled thật rồi observe status/ack. Không claim build T4 chứng minh PN532/IR/WiFi trước T5–T7.

### Task 5 — WiFi scan một lượt, top32 và cancel backend đúng event

**Files:** Modify `src/modules/wifi_module.{h,cpp}`, `platformio.ini`; Create `src/core/wifi_scan_result.h`, `src/core/wifi_scan_result.cpp`, `test/test_wifi_scan_result/test_wifi_scan_result.cpp`.

**Dependencies:** T1/T2/T4. **Consumes:** runtime ticket + async core; **Produces:** spec wifi_scan output, cleanup/revision. **Coverage:** R05/R08/R09/R10/R13/R16/R17/R21; AC06/AC09/AC12.

- [ ] **Red top-N boundary:** portable `struct WifiCandidate { size_t index; int rssi; std::array<uint8_t,6> bssid; }; class WifiTop32` với `void consider(const WifiCandidate&)`, `size_t size() const`, `const WifiCandidate& at(size_t) const`, `bool truncated() const`. Internals fixed array32 + size/total count, insertion RSSI descending và BSSID bytes ascending; không vector N SSID. Native test đưa33 candidates unique BSSID, rssi=-90+i: size32, truncated true, first.index32, last.index1; đưa2 equal RSSI khác BSSID theo thứ tự đảo: output BSSID ascending. 0 candidates: size0/truncated false là empty-success selection, không failure. Chạy `pio test -e native -f test_wifi_scan_result` fail-before, thêm `+<core/wifi_scan_result.cpp>`, run green sau implementation.
- [ ] **API:** begin chỉ off; enable health radio/AP ready, không scan; supported Scan gọi beginAction(now,15000,ticket), lưu ticket và yêu cầu async `WiFi.scanNetworks(true)` trong loop. -1/running là start accepted, failure-start => failAction ScanFailed giữ output. Không đổi AP mode thành STA-only; Arduino scanNetworks enableSTA ORs STA mode. Không auto scan/retry khi connect/enable hoặc previous error.
- [ ] **Event bridge:** đăng ký ARDUINO_EVENT_WIFI_SCAN_DONE; callback chỉ store done/status notice dưới short synchronization/atomic release flag, không module mutation/read results. Chỉ một physical scan in-flight, không đổi armed generation tới khi done đã drain. Callback flag + loop ticket/generation chặn completion cũ. Installed WiFiGeneric gọi `_scanDone()` trước user callback; buffer thuộc Arduino đã fetch IDF records. Không gọi esp_wifi_scan_get_ap_records/free lần nữa.
- [ ] **Result:** khi done, loop lấy count/scanComplete sau callback; 0 là success. Iterate metadata RSSI/BSSID, `WifiTop32.consider`, chỉ copy SSID/channel/security cho32 chọn; encode output một lần bằng ArduinoJson. BSSID uppercase, empty SSID giữ rỗng, escape Unicode/quote; no merge SSID. `truncated = total>32`. Replace payload nguyên tử qua completeAction ticket; sau serialize gọi WiFi.scanDelete để free result buffer. Driver failed status → ScanFailed, health AP vẫn tốt nếu radio không hỏng. Poll không tạo JSON khi idle.
- [ ] **Cancel/deadline:** installed scanDelete chỉ free results, không cancel scan. Disable lập tức `runtime.setEnabled(false,now,scanInFlight)` và invalidate ticket trước stop. Deadline `expire(now,ScanTimeout,scanInFlight)` giữ output cũ. Trong poll gọi `esp_wifi_scan_stop()` khi physical scan active rồi chờ done notice, drain Arduino results bằng scanDelete, mới `finishCleanup`. Core wrapper có scanComplete timer khoảng max_ms_per_chan*20 (default6s); wrapper failed/cleared bit không đủ chứng minh physical cleanup. Không start scan mới khi cleanup/in-flight, không accept result đã cancel. Done race trước stop: consume notice/drain trước quyết định stop; stop error không được fake clear flag. Ghi error code/backend state, xác minh IDF no-active condition nếu muốn finish; chưa chứng minh thì giữ cleanup + block action, không blind scanDelete rồi báo sạch.
- [ ] **Nguồn cleanup:** [ESP-IDF v4.4 ESP32-S3 WiFi guide — WIFI_EVENT_SCAN_DONE](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32s3/api-guides/wifi.html): natural completion/scan_stop đều sinh done. Đọc lại installed core/header khi version khác; không hardcode event behavior từ core mới. AP vẫn phục vụ browser trong toàn scan/cleanup.
- [ ] **Smoke thật:** hai AP cùng SSID khác BSSID, tên Unicode/quote, scan hai lần, empty-success trong môi trường/fixture phù hợp; đang scan disable rồi enable/action trước done → cleanup giữ/busy, old output không sống lại; AP/browser không bị teardown. Inject scan_failed/deadline/top33 nếu khó tạo thật, ghi riêng. Đo start/finish/timeout/Stop cleanup từ Serial+frame, đối chiếu target15s. Không đạt target chặn AC06/AC12, trình bằng chứng xin duyệt sửa spec, không tự tăng deadline.

### Task 6 — PN532 UID đầy đủ4/7/10 byte, transaction và abort có giới hạn

**Files:** Modify `src/modules/pn532_module.{h,cpp}`, `platformio.ini`; Create `src/core/nfc_frame.h`, `src/core/nfc_frame.cpp`, `src/core/nfc_uid_format.h`, `src/core/nfc_uid_format.cpp`, `test/test_nfc_frame/test_nfc_frame.cpp`, `test/test_nfc_uid_format/test_nfc_uid_format.cpp`.

**Dependencies:** T1/T3/T4. **Produces:** ready/error health thật, nfc_uid, cancellation-safe bounded reader. **Coverage:** R06/R08/R09/R13/R16/R17/R21; AC02/AC07/AC09/AC12.

- [ ] **Dependency finding:** Adafruit readDetectedPassiveTargetID đọc20 frame bytes rồi copy UID từ offset13; UID7/10 bị thiếu bytes, buffer caller10 không sửa được. Public sendCommandCheckAck còn đợi response nên không là start nonblocking. Không dùng đường này, private API, partial-header reread hoặc patch `.pio`. Viết adapter nhỏ trong pn532_module.cpp; pure frame validation/format tách core. Nguồn [NXP UM0701-02](https://www.nxp.com/docs/en/user-guide/141520.pdf), §6.2.1/6.2.2/6.2.4/7.2.8/7.2.9/7.3.1/7.3.5.
- [ ] **Pure APIs:** `enum class NfcFrameResult { Uid, NoTarget, Malformed }; NfcFrameResult parseUidFrame(const uint8_t* frame,size_t available,std::array<uint8_t,10>& uid,uint8_t& uidLength); bool isAckFrame(const uint8_t* frame,size_t available); size_t buildCommandFrame(const uint8_t* command,size_t commandLength,uint8_t* out,size_t capacity); std::string formatUid(const uint8_t* uid,size_t length); std::string uidToJson(const uint8_t* uid,size_t length);` trong namespace `nfcFrame` cho3 frame functions, namespace `nfcUid` cho2 format functions. `buildCommandFrame` thêm TFI D4 và checksum, capacity check trước write; chỉ normal frames cho command allowlist nhỏ, không general NFC framework.
- [ ] **Parser regression red:** validate preamble0000FF, LEN/LCS sum0, TFI D5, response4B, advertised total=LEN+7 đủ available, DCS sum0, postamble0, NbTg=1/Tg=1, UID length4/7/10 và offsets bounded. NbTg0 chỉ normal LEN3 là NoTarget. Short/checksum/length malformed không overwrite output args hoặc state. Cho phép transport trailing padding nhưng parse đúng advertised frame; không dùng unread/stale bytes. Dùng golden4/10 dưới, thêm7 từ independent fixture; invalid uid length5 recompute checksum để test lỗi length, không vô tình test checksum lần hai.

```cpp
const uint8_t frame4[] = {
  0,0,0xFF,0x0C,0xF4,0xD5,0x4B,1,1,0,4,8,4,4,0xAB,1,2,0x1C,0
};
const uint8_t frame10[] = {
  0,0,0xFF,0x12,0xEE,0xD5,0x4B,1,1,0,4,8,0x0A,
  4,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xC7,0
};
const uint8_t noTarget[] = {0,0,0xFF,3,0xFD,0xD5,0x4B,0,0xE0,0};
const uint8_t ack[] = {0,0,0xFF,0,0xFF,0};
```

- [ ] **Formatter regression:** bytes0A/00/FF/0B => `0A:00:FF:0B`; full10 UID preserves all bytes, zero padding/uppercase/colon. Không thêm payload-field-echo JSON test; frame boundary + display normalization có consumer-visible bug. Register tests bằng Unity main/setUp/tearDown như T1. Native filter thêm nfc_frame.cpp/nfc_uid_format.cpp; `pio test -e native -f test_nfc_frame -f test_nfc_uid_format`, red trước/green sau.
- [ ] **I2C primitives:** address0x24; `enum class ReadyState { Busy, Ready, IoError }` cho status-byte poll, phân biệt read count0/NACK với RDY0. `writeFrame` kiểm tra Wire.write length/endTransmission; each bus op timeout20ms. `readAck` một read7 bytes (status+6 ACK). `readResponse` một read26 bytes (status+max25 frame), kiểm tra status Ready và parse advertised length/checksum trong chính buffer đó; không đọc header6 rồi request frame lại. Short count không đủ advertised total => IoError/malformed. Extra padding không là payload. Installed Adafruit dùng fixed-length reads; board smoke phải chứng minh fixed-cap26 read hợp lệ với thẻ4/7/10. Nếu transport chip/library yêu cầu đọc đúng length, dùng một I2C read transaction có header+remaining trong cùng START/STOP bằng ESP-IDF command link, không hai requestFrom giả rewind; version-specific API phải đọc trước khi đổi adapter.
- [ ] **Init/health commands:** GetFirmwareVersion command02 → response03/IC32; SAMConfiguration `14 01 14 00` (normal mode, IRQ unused); SetParameters `12 04` giữ normal pre/postamble và **tắt fAutomaticRATS**, để UID-only response không mang ATS vượt25; RFConfiguration `32 05 FF 01 00` đặt finite passive activation trials. Validate ACK và command-specific response, không coi address ACK là ready. Bounded enable exchange tối đa40ms/command (ngoài bus ops cũng chịu remaining deadline), không wait1000ms mặc định; fail→HardwareError/enabled retained. Health probe500ms khi không outstanding exchange. Chip recovery chạy init cần thiết rồi ready, không tự read_uid. Trước implementation đối chiếu byte/bit meanings và result lengths với manual, đo trên board; các constants này không phải config UI mới.
- [ ] **Reader state machine:** `Idle/AwaitAck/AwaitResponse/Aborting`, transaction kind (`Firmware/Sam/Parameters/Retries/Uid`), frame buffers fixed26 và ticket. `handleAction(ReadUid)` beginAction(now,5000,ticket), lưu deadline; poll phát một InListPassiveTarget `4A 01 00`, chờ ACK rồi response không blocking. NoTarget hữu hạn: tiếp tục probe trong cùng action, nhịp100ms, deadline chung không reset; Uid complete một lần và dừng. Giữa probes, health GetFirmwareVersion theo500ms; không interleave commands trong outstanding exchange. Trong active exchange I2C NACK xác định transport loss ngay; fail hardware + healthfalse. Transaction vượt250ms thì abort trước bất kỳ command khác; sau abort probe firmware, chip sống tiếp tục chờ UID trong deadline chung, chip không đáp ứng là hardware_error. Không gán Idle khi chỉ hết timer.
- [ ] **Cancellation/deadline:** invalidate ticket/clear payload (disable) hoặc giữ previous payload (read_timeout) trước cleanup; outstanding exchange => cleanupPending. Manual §6.2.2.2: host gửi ACK frame `00 00 FF 00 FF 00` để abort, PN532 không trả response và chờ command mới; không đợi card response vô hạn. Aborting poll write ACK có Wire timeout, giải phóng host exchange chỉ sau write thành công theo protocol; drain frame đã sẵn sàng nếu cần trước abort để không chấp nhận old UID. Không dùng unwired reset() làm proof. Nếu NACK/bus error không xác minh abort: giữ flag, log lý do, không fake finish sau fixed timer. Cleanup attempt budget250ms; off hết budget dừng I/O và giữ flag. Enable lại không clear flag, chỉ rearm bounded abort attempt; khi enabled retry cleanup nhịp500ms, action Busy cho đến ACK abort thành công. Không auto read_uid sau recovery. Đây là fault state quan sát được, không fallback thành success.
- [ ] **Smoke thật:** enable không chip hardware_error, cắm lại ready≤target2s; hai thẻ, cùng thẻ hai lần sequence tăng; thẻ UID7/10 nếu có để kiểm fixed-cap transport, nếu thiếu ghi boundary proof native/injection riêng. Không thẻ5s read_timeout healthready; rút PN532 khi chờ hardware_error; Stop lúc AwaitAck/AwaitResponse, enable trước cleanup xong busy, thẻ đến muộn không phục hồi output. Ghi ACK-abort/timing bằng Serial/logic analyzer nếu cần; phải chứng minh giao thức cleanup thực, không chỉ counter runtime.

### Task 7 — IR một thông điệp, repeat/UNKNOWN/raw512 đúng

**Files:** Modify `src/modules/ir_module.{h,cpp}`, `platformio.ini`; Create `src/core/ir_capture_format.h`, `src/core/ir_capture_format.cpp`, `test/test_ir_capture_format/test_ir_capture_format.cpp`.

**Dependencies:** T1/T3/T4. **Produces:** ir_capture chuẩn hóa một lần trước buffer reuse. **Coverage:** R07/R09/R13/R17/R21; AC08/AC09/AC12.

- [ ] **Portable normalization seam:** `enum class IrFormatResult { Ignored, TooLong, Ready }; IrFormatResult formatIrCapture(bool repeat,bool overflow,const volatile uint16_t* raw,size_t rawLength,uint32_t tickUs,const std::string& protocol,bool numericValueKnown,uint64_t value,std::string& json);` trong namespace `irCapture`. Raw length gồm leading gap; overflow hoặc rawLength-1>512 => TooLong và không đổi json; repeat/no message => Ignored và không đổi json. Ready bỏ raw[0], convert uint32µs bằng tickUs, value chuẩn `0x`+uppercase hex hoặc null nếu UNKNOWN/no numeric value, no double/JS Number. Caller chỉ truyền tickUs thực kRawTick (installed2µs), không tin stale comment0.5µs.
- [ ] **Regression red:** raw `{100,4500,2250,280}` tick2 UNKNOWN => decoded raw `[9000,4500,560]` và value null (không synthetic hash); repeat không overwrite previous result; rawLength513 với512 timings accepted, rawLength514/overflow rejected, previous json giữ nguyên; known value `0xFEDCBA9876543210` giữ chính xác hex64. Assertions deserialize payload và kiểm behavioral conversion/precision/boundary, không pin incidental JSON wording/order. Unity main đăng ký từng hàm. Thêm native `+<core/ir_capture_format.cpp>`, run `pio test -e native -f test_ir_capture_format` red/green.
- [ ] **Capture driver:** buffer514 cho512 timings+gap+overflow detection; một IRrecv và decode_results, không receiver thứ hai. Enable RX giữ healthready/detail init; idle không publish frames nền. Action Capture beginAction(now,10000,ticket), `resume()` bỏ frame cũ lúc bắt đầu, poll decode khi running; success normalize trước resume rồi complete ticket đúng generation. UNKNOWN value của thư viện có thể là hash, phải bỏ/null. Repeat decode → resume và tiếp tục chờ, không reset deadline. Mọi decode branch (success/repeat/error) resume đúng, không giữ raw pointer sau resume.
- [ ] **Timeout/Stop:** elapsed deadline CaptureTimeout giữ previous payload; timeout resume/drop current frame, cleanup flag nếu receiver cần deferred release. Disable logical cancel/clear trước disableIRIn trong poll; enable trong cleanup không khởi tạo RX lần hai. Không IRsend/TX/replay. Overflow CaptureTooLong/actionerror không đổi health tốt hoặc overwrite result cũ.
- [ ] **Smoke:** hai nút remote ra kết quả khác; cùng nút hai action sequence tăng; giữ nút repeat-only không tạo thành công mới;10s no signal timeout, UNKNOWN rawµs có label đúng; Stop giữa chờ không resurrect output. Injection riêng cho >512/overflow nếu không tạo được thật; so decoded/raw export với tín hiệu remote/receiver thật, không gọi fixture là proof cảm biến. Build/native sau integration, không native test echo enableIRIn.

### Task 8 — Browser transport, freshness, controls và detail thật

**Files:** Modify `data/index.html`; Create `data/dashboard.js` (một classic script, không bundler/framework). Không permanent UI test suite.

**Dependencies:** T1–T7 wire/backend; có thể phát triển UI bằng throwaway injection nhưng acceptance vẫn dùng board. **Produces:** shared JS state/helpers cho T9. **Coverage:** R03/R04/R08/R09/R12/R13/R14/R17/R21; AC01/AC03/AC04/AC05/AC09/AC11.

- [ ] **Cutover:** xóa startMock/Math.random/fallback, !!status.connected và Date(lastUpdateMs); giữ sidebar/detail pattern nhưng đủ5 category và Overview. `data/index.html` chỉ markup/style, `<script src="/dashboard.js" defer></script>`; không action RF/replay buttons. IR label RX/capture, không RX/TX như chức năng đã có.
- [ ] **JS state:** `MODULE_IDS`5, action allowlist wifi/scan,pn532/read_uid,ir/capture; `state={transport,selectedModule,socket,socketToken,reconnectAttempt,reconnectTimer,modules,pending:new Map(),recentCommands:new Map(),nextCommandId:1,log:[],logFilter:'all'}`. Each module stores validated status, decoded payload, `receivedAtMs`, `resultReceivedAtIso`, result signature, stale flag. Clock performance.now cho freshness; Date.now chỉ wallclock nhận/export. Không gọi uptime là thời gian thu.
- [ ] **Functions/contracts:** `connect()`, `scheduleReconnect()`, `handleFrame(data,socketToken)`, `validateStatus(value)`, `decodePayload(module,output)`, `applyStatus(status)`, `sendCommand(module,cmd,action)`, `handleCommandResult(frame,token)`, `expirePending(now)`, `markStale(now)`, `renderOverview()`, `renderDetail()`, `logEvent(module,type,content)`, `renderLog()`, `downloadJson(filename,value)`. Validate returns parsed value/null, decode returns validated object/null; caller phân biệt output rỗng và malformed. Render helpers DOM nodes/textContent, không device string vào innerHTML. Không expose fake WebSocket trong production.
- [ ] **Transport:** URL `${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws`; một socket/timer, backoff1/2/4/8s cap8/reset open. Bump socketToken khi tạo socket, mọi callback kiểm token + socket identity để socket cũ không mutate state. Open là transport live nhưng chưa fresh5 modules; close marks all stale/unknown pending và không clear payload/disable firmware. Message JSON parse try/catch, `type==='command_result'` xử lý trước status. Không auto-resend sau timeout/reconnect.
- [ ] **Strict validation:** mandatory fields spec§6.1; thật boolean, uint32 integer, string enums, actionError/state consistent, off connectedfalse/idle/no output. Unknown extra fields ignored. Payload WiFi columns/32/truncated/BSSID; UID canonical4/7/10 bytes; IR UNKNOWN/null, hex string64, raw integerµs<=512. Malformed status không overwrite state/freshness; malformed payload không replace last validated result hoặc tạo export giả, log protocol và giữ last-good display có cảnh báo. Valid off/empty output phải clear current result ngay, không giữ last-good như payload thiết bị. Không dùng `resultUpdateMs===0` để đo presence.
- [ ] **Freshness/result identity:** stale nếu socket không live hoặc elapsed nhận valid module>=5000ms. Giữ health gốc, không sửa connected để giả missing chip. Nhận status mới unlock đúng module; action chỉ ready+notrunning+notcleanup+fresh. New result nếu sequence **khác** (không >), hoặc output/resultUpdateMs thay đổi; reconnect snapshot đầu so toàn signature và reset freshness. Payload mới/snapshot đầu đặt resultReceivedAtIso; heartbeat không thay receive time của result. Boot/off/empty clear current payload, kể cả sequence đã reset/wrap. PreviousResult nếu payload tồn tại và action running/error/timeout; output cũ không là proof ack.
- [ ] **Pending bounded/no ID reuse:** pending chỉ chứa chờ ack tối đa3s; hết hạn hoặc close chuyển unknown vào recentCommands và log, remove pending. RecentCommands FIFO cap200, giữ unknown records để late ack cập nhật đúng token/id/module nếu còn record; evicted late ack chỉ log protocol, không suy ra success và không mutate state. Id tăng đơn điệu, không reuse trong một socket ngay cả record đã evict; tới UINT32_MAX thì tạo socket mới/resync trước dùng id1. Close clear identity maps sau ghi unknown; không giữ unresolved tăng vô hạn. Đây không phải limit mới cho command protocol; chỉ bounded browser history. Send mới khi fresh; id reuse hai tab hợp lệ. Pending UI không chặn Stop vì action/enable đang pending; disable có reserve riêng ở backend.
- [ ] **Ack semantics:** encode JSON size UTF8<=512 bằng TextEncoder; set pending trước ws.send, send exception→unknown vì outcome không chứng minh. ok=false hiện exact error code/label; ok=true action chỉ accepted, chờ status. Late ack chỉ cập nhật record trên socket hiện tại, không toggle enabled hoặc chạy action. Unknown result sau3s giữ nhãn, operator đọc fresh state để gửi thao tác mới; không tự retry.
- [ ] **Detail controls:** enable/disable button thật, status health/action/error/result riêng; Enable idempotent, Stop khả dụng kể cả running/cleanup nếu module fresh. Action khóa off/error/running/cleanup/stale, cleanup nhãn “Đang giải phóng tài nguyên” kể cả off/enable lại. WiFi table giữ hidden label nhưng dữ liệu SSID rỗng; NFC UID; IR protocol/value/raw. Không numeric uint64 conversion, không DOM execution của SSID/detail.
- [ ] **Browser smoke thật:** mở board AP bằng Chromium, chụp5 category và controls; hai tab đồng bộ, close/reconnect giữ action, nhận snapshot≤1s. Throwaway `data/__smoke.html` inject FakeWebSocket trước load dashboard.js: missing field/"false"/bad JSON/markup/sequence wrap/output cleared/cleanup/ack late&lost/old socket callback. Dùng browser clicks/Tab/Enter và screenshots, console/network capture; không chỉ chạy node unit tests hoặc inspect source. Sau fixture, remove trước uploadfs production và mở lại trang thật để xác nhận không fallback.

### Task 9 — Overview, bounded log, export và responsive viewing

**Files:** Modify `data/index.html`, `data/dashboard.js`; update root README sau smoke, không thêm UI framework/dependency.

**Dependencies:** T8 JS contract, T5/T6/T7 payload. **Coverage:** R09/R10/R11/R12/R15/R19/R21; AC09/AC10/AC11/AC12.

- [ ] **Overview:** thống kê off/ready/error/stale đủ5 module từ current validated status/freshness, chưa snapshot = chưa có dữ liệu/stale, không health guessed. Show selected detail/action/result metadata. Stale tính tách health, số stale không biến ready thành hardware error.
- [ ] **Event signature:** `(enabled,connected,detail,actionState,actionError,cleanupPending,resultSequence,output)`; không lastUpdateMs/result receive age. State change/transport/command/result mới tạo event; success cùng payload nhưng sequence đổi vẫn event; disable đang running ghi hủy, cleanup true/false riêng. Keep oldest-first array max200, render newest-first; filter module/all, Clear chỉ log, không device command/payload clear.
- [ ] **Export helpers:** `buildLogExport(nowIso)` → `{exportedAtIso,events:[...oldestToNewest]}`; `buildResultExport(module,nowIso)` → module/enabled/connected/actionState/actionError/cleanupPending/resultSequence/resultUpdateMs/receivedAtIso/exportedAtIso/stale/previousResult/payload decoded. receivedAtIso lấy resultReceivedAtIso, không heartbeat timestamp. Không payload validated khóa nút result; stale/offline vẫn download old valid payload với stale label. Hai buttons độc lập, filename chứa module/date nhưng không gọi date đó là giờ thu ESP32.
- [ ] **Download:** JSON.stringify(...,null,2), Blob application/json UTF8, URL.createObjectURL, anchor.download/click/remove, revoke URL sau browser đã bắt đầu tải (deferred task). Không /export endpoint, không writes LittleFS. Open JSON và so semantic Unicode/quotes/BSSID/UID/raw64, không chỉ file tồn tại/non-empty. Clear log không xóa payload hoặc disable; export log sau Clear có events=[] là đúng, không tạo payload rỗng giả.
- [ ] **CSS/keyboard:** button/focus visible, trạng thái có text không chỉ màu, laptop768/1280 dùng full controls/log/export bằng Tab/Enter; 360px overview/status/result/log đọc được, WiFi/raw table horizontal scroll trong panel, không overflow body. Không nâng phone-control thành scope mới.
- [ ] **Browser smoke:** inject201+distinct state events để chứng minh FIFO200 và filter/order, heartbeat100 frames không spam; dùng real successful WiFi/NFC/IR payload export3 files, log export riêng. Giữ running/error với previous payload, export metadata chính xác; close socket/stale vẫn export và label; Clear log không đổi device result. Screenshot768/1280/360 và markup string hiển thị literal. Injection định danh riêng; gỡ fixture, uploadfs thật và re-open board dashboard để chứng minh real surface.

### Task 10 — RGB enabled-only và nghiệm thu tích hợp trên board

**Files:** Create `src/core/status_health.h`, `src/core/status_health.cpp`, `src/core/status_led.h`, `src/core/status_led.cpp`, `test/test_status_health/test_status_health.cpp`; Modify `src/main.cpp`, `platformio.ini`, `README.md`, `docs/planning/README.md`, tickets05–09 theo criteria hiện hành. Evidence lưu trong bộ nghiệm thu được giao, không tự tạo báo cáo pass trống.

**Dependencies:** T1–T9. **Coverage:** R01/R02/R08/R15/R16/R19/R21; toàn bộ AC01–AC12.

- [ ] **Pure RGB API/test red:** `enum class HealthLevel { Off, Healthy, Degraded }; HealthLevel aggregateHealth(const ModuleStatus* const* modules,size_t count);` trong status_health.h/.cpp. Tests: mọi off (kể cả off stale-connected)→Off; enabled healthy + off disconnected→Healthy; thêm enabled disconnected→Degraded; connected PN532 actionTimeout vẫn Healthy; đủ5 category, WiFi enabled/error cũng Degraded. Không assert enum count/default or FastLED call echoes. `pio test -e native -f test_status_health` red/green; thêm `+<core/status_health.cpp>`.
- [ ] **RGB wrapper:** `namespace statusLed { void begin(); void update(HealthLevel level); }`; một LED pin48 dùng FastLED installed NEOPIXEL, black/green/yellow; begin black, update chỉ FastLED.show khi level đổi. Main aggregate const status pointers sau dispatch/poll, không copy outputs; IR/read timeout không health fault. T9 UI health cùng enabled-only rules nhưng transport/stale là browser state, không làm LED đổi theo browser disconnect.
- [ ] **Final native source filter** (giữ native flags/dependency, không driver/network vào host):

```ini
build_src_filter =
    -<*>
    +<core/storage_json.cpp>
    +<core/module_status_json.cpp>
    +<core/ws_command_json.cpp>
    +<core/module_runtime.cpp>
    +<core/command_queue.cpp>
    +<core/wifi_scan_result.cpp>
    +<core/nfc_frame.cpp>
    +<core/nfc_uid_format.cpp>
    +<core/ir_capture_format.cpp>
    +<core/status_health.cpp>
```

- [ ] **Integrated checks một lượt sau hợp nhất:** `pio test -e native`; `pio run -e attak-iot-firmware`; khi có board/quyền `pio run -e attak-iot-firmware -t upload`, `pio run -e attak-iot-firmware -t uploadfs`, `pio device monitor`. Ghi output thật. Không pio run -e native vì test-only không application main. Native smoke chạy runtime/queue/frame/normalizer thật, nhưng không thay board/browser proof. Baseline failed regression và green output lưu; thiếu tool/hardware ghi đúng blocker, không pass giả.
- [ ] **Board checklist AC01–AC12** dưới: mỗi row ghi PASS/FAIL/BLOCKED, link ảnh/log/export và board/core/driver version; không chỉ đánh dấu done từ build. RGB kiểm all off/tất cả enabled khỏe/một enabled chip mất/WiFi error; actiontimeout healthy không đổi vàng. Browser cuối đóng module vẫn hoạt động. Screenshot/JSON/log đủ, video không bắt buộc.
- [ ] **10-minute run hai tab:** Serial ghi millis, free heap, min-free heap lúc đầu và mỗi60s (cùng điểm idle sau tác vụ), loop max latency, scan start/finish/stop, health lost/recovered. Luân phiên scan/read/capture, timeout/disable/re-enable, reconnect, export/log>200. So current free-heap sau các vòng tương đương; min-free là high-water metric, không tự kết luận leak từ min-free giảm lần đầu. Không reset, không free heap giảm dần vô hạn; nếu có phải điều tra/sửa/re-run scenario trước AC12. Gỡ debug/dequeue pause/FakeWebSocket/scaffold sau smoke, giữ chỉ diagnostics cần vận hành.
- [ ] **Docs sau proof:** README cách nối AP/5 category, Start/Stop/cleanup/unknown ack/last-result/export và caveat IR presence; ticket05–09 thay boot-auto-poll/RGB-all-chips bằng spec AC tương ứng, không đánh dấu replay/jammer/clone đã làm. Nếu có CHANGELOG theo convention thì cập nhật mục đợt này; không tạo docs cạnh tranh spec/plan. Không commit khi chưa được cho phép.

## 6. Traceability R01–R21 → task → AC

| Requirement | Task thực thi | Regression/smoke bắt buộc | AC |
|---|---|---|---|
| R01 | T4–T10 | Real board/browser action success/error/cancel, không chỉ native | AC12 |
| R02 | T3/T8/T10 | Allowlist controls, no TX/replay/clone, boot off | AC01, AC12 |
| R03 | T2/T4/T8 | Client-session ack same id hai tab, shared status | AC03 |
| R04 | T4/T8 | Close browser cuối trong action, reconnect snapshot | AC04 |
| R05 | T5/T8 | Enable ready/no scan; một scan/lệnh, AP sống | AC06 |
| R06 | T6/T8 | Real UID, no-card5s vs chip-missing, bounded abort | AC07 |
| R07 | T7/T8 | Real remote, repeat ignored, UNKNOWN/raw,10s | AC08 |
| R08 | T1/T3/T5/T6/T7/T10 | Health loss keeps enabled, recover no auto action | AC02 |
| R09 | T1/T5–T9 | Retention/identical-success sequence/cancel regression, old-result label | AC09 |
| R10 | T5/T8/T9 | BSSID top32 stable order, same SSID distinct BSSID | AC06, AC10 |
| R11 | T9 | Hai download riêng, schema/Unicode/metadata/Clear | AC10 |
| R12 | T8/T9 | Real browser768/1280 keyboard,360 viewing | AC11 |
| R13 | T1/T2/T4–T8 | Busy/off precedence, no queued-later action, disable cancels | AC03, AC09 |
| R14 | T4/T8 | Lost/late ack3s unknown, no auto resend/optimistic state | AC05 |
| R15 | T10 | Checklist + ảnh/export/log real success/error/cancel | AC12 |
| R16 | T3/T5/T6/T10 | Đo liveness2s/WiFi15s, gate revision nếu fail | AC02, AC06, AC12 |
| R17 | T1/T4–T8 | Logical cancel/clear ack before bounded backend cleanup | AC03, AC09 |
| R18 | T2/T4 | 8 normals + reserved per-module Stops, FIFO regression | AC03 |
| R19 | T8–T10 | Injection edges tách bằng chứng board, không video bắt buộc | AC12 |
| R20 | T2/T4 | 5 Stop capacity13, duplicate admission/FIFO order | AC03 |
| R21 | T1/T3–T9 | Cleanup every tab, enable preserves, action locked/busy, export flag | AC03, AC11 |

## 7. Checklist nghiệm thu AC01–AC12 và loại bằng chứng

Không row nào đã PASS bởi tài liệu này. Đọc đầy đủ kịch bản spec§10; bảng dưới định tuyến task/proof, không giảm acceptance scope.

| AC | Task | Native/protocol evidence | Board/browser evidence bắt buộc |
|---|---|---|---|
| AC01 | T3/T4/T8/T10 | Parser allowlist | Boot off/5 categories/no mock/no action ngoài scope |
| AC02 | T1/T3/T6/T10 | Enabled/health transition, RGB enabled-only | Missing/reinsert chips, shared SPI, timing2s, IR presence caveat |
| AC03 | T1/T2/T4/T8 | Queue8+5/FIFO/duplicate, epoch/cleanup/busy; saturation injection | Hai tab same id, real Stop/cleanup/enable, no old completion |
| AC04 | T4/T8 | Old-socket callback injection | Close last browser during action, reconnect≤1s snapshot/no replay |
| AC05 | T4/T8 | Lost/late/mismatched ack injection, unknown3s | Live status/payload old-label while observing injected transport |
| AC06 | T5/T8/T9 | Top32/order; scan-failure/deadline injection nếu cần | Scan AP/BSSID/Unicode, AP alive, cancel, target15s measurement |
| AC07 | T6/T8 | UID4/7/10/checksum/length, abort state | Hai thẻ/same twice/no-card5s/chip loss/Stop |
| AC08 | T7/T8 | Repeat/512/overflow/UNKNOWN/64-bit | Hai remote buttons/no-signal10s/Stop/no TX |
| AC09 | T1/T5–T9 | Retention/sequence/wrap/stale completion | Success→running→error→same success→disable, heartbeat age stable |
| AC10 | T9 | Escaping protocol injection | Actual WiFi/NFC/IR JSON exports/log/Clear/stale/offline |
| AC11 | T8/T9/T10 | Markup/>200 events/cleanup injection | Laptop keyboard/full operations;360 viewing; real RGB/two-tab cleanup |
| AC12 | T10 | Full native/build logs, labelled edge injection | Board checklist/screenshots/exports/log,10min heap/timing, target gate |

## 8. Quy tắc bàn giao implementation

- Các snippet/assertion ở đây là hướng dẫn worker, chưa là code chạy trong repository. Không báo task done khi chỉ có interface, mock, intermediate unsupported body hoặc build xanh.
- Plan này tự chứa: mọi shared API, dependency, target và proof nằm ở đây hoặc spec, không phụ thuộc tài liệu tạm bên ngoài. Dependency installed-version review ở T3/T5/T6/T7 là preflight bắt buộc, không là placeholder cho behavior chưa quyết định.
- Khi thiếu board/module/card/remote/cổng Serial: hoàn tất reachable pure/UI/protocol work và ghi rõ từng AC phần cứng BLOCKED; không thu hẹp AC hoặc gọi toàn đợt complete. Khi target2s/15s fail: giữ spec và trình measured evidence/đề xuất revision để duyệt, không tự nới.
- Final delivery phải nêu actual tests/build/browser/board đã chạy, evidence links và blockers; không lặp baseline12/12 thành fresh verification. Không commit/flash/build/implementation từ yêu cầu chỉ viết plan hiện tại.

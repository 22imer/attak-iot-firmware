# Kế hoạch triển khai: Attack modules + action catalog

> Trạng thái: **draft roadmap** (2026-10-05). Đây là kế hoạch mở rộng SAU đợt
> dashboard/observation hiện tại (xem [`spec.md`](spec.md), [`map.md`](map.md)).
> Nó KHÔNG thay thế spec hiện hành; các mục ở đây chỉ thành hợp đồng khi được
> đưa qua `/to-spec` → `/to-tickets`.

Tham chiếu lịch sử: phần "v2: Attack payloads" trong [`map.md`](map.md) đã phác 1
payload/category. Kế hoạch này tổng quát hoá thành **catalog nhiều attack mỗi
module**, hiển thị thành **một danh sách chọn được** trên dashboard.

---

## 1. Mục tiêu

1. Mỗi module phơi ra một **danh sách action/attack** (không còn cứng 1 action).
2. Dashboard hiển thị danh sách đó để người dùng **chọn và chạy dễ dàng** (menu
   theo module + form tham số + Start/Stop + vùng output).
3. Bộ attack tham khảo **đầy đủ như Bruce** (sub-GHz, 2.4GHz, NFC, IR, WiFi),
   nhưng **tự viết code** — không copy mã AGPL-3.0 của Bruce.
4. Phân tầng pháp lý rõ ràng; các action gây nhiễu (jam/deauth) có rào xác nhận
   và chỉ dùng trong môi trường được phép/được kiểm soát.

## 2. Nguyên tắc tham khảo Bruce (quan trọng về license)

- **Chỉ tham khảo kiến trúc/cách làm, KHÔNG fork, KHÔNG copy code.** Lý do đã chốt
  trong `map.md`: Bruce là AGPL-3.0; driver NRF24 `RF24` (GPL-2.0-only) đang dùng
  không tương thích AGPL trong cùng một binary. Vì vậy mọi logic attack phải được
  viết lại độc lập.
- **"Khởi tạo từ Bruce"** ở đây nghĩa là: đọc cấu trúc module của Bruce để học
  cách tổ chức, rồi hiện thực theo kiến trúc `ModuleRuntime` sẵn có của dự án.
  Bản đồ nơi cần đọc trong repo Bruce (chỉ để học, không bê nguyên):

  | Nhóm | Thư mục/khái niệm trong Bruce để tham khảo |
  |---|---|
  | Sub-GHz (CC1101) | `src/modules/rf/` — record/replay, spectrum, jammer, custom TX |
  | 2.4GHz (NRF24) | `src/modules/rf/nrf*` — spectrum/analyzer, jammer |
  | NFC/RFID (PN532) | `src/modules/rfid/` — read, clone, write/NDEF, emulate, chameleon |
  | IR | `src/modules/ir/` — TV-B-Gone, custom IR protocols, replay |
  | WiFi | `src/core/wifi/` (`wifi_common.cpp`, `wifi_atks.cpp`) — deauth, beacon, evil portal, sniffer; **pattern teardown AP** (`cleanlyStopWebUiForWiFiFeature()`) |
  | Chia sẻ SPI | pattern `acquireSPIBus` (CC1101 + NRF24 chung bus) |

## 3. Kiến trúc: từ "1 action" sang "action catalog"

Hiện tại (`command_types.h`): `ActionId{None,Scan,ReadUid,Capture}` cứng, parser
`ws_command_json.cpp` allowlist tay từng cặp module/action. Để có danh sách
chọn được và dễ mở rộng, ta đảo nguồn sự thật: **module tự khai báo catalog**,
parser và dashboard đều đọc từ catalog đó.

### 3.1. Mô hình action descriptor (đề xuất)

```cpp
enum class ActionKind : uint8_t {
    OneShot,     // chạy một lần, trả 1 kết quả (scan, read_uid, capture)
    Continuous,  // chạy liên tục tới khi Stop (spectrum, jammer, sniffer)
    Record,      // ghi vào buffer RAM của module (rf_record, ir_capture)
    Replay,      // phát lại buffer đã ghi (rf_replay, ir_replay)
};

enum class LegalTier : uint8_t {
    Observe,     // chỉ quan sát/thu — rủi ro pháp lý thấp
    ActiveOwn,   // phát/clone — chỉ trên thiết bị/thẻ của mình
    Disruptive,  // gây nhiễu/từ chối dịch vụ — chỉ môi trường được phép
};

struct ActionDescriptor {
    const char *id;        // id trên wire, vd "rf_record", "wifi_deauth"
    const char *label;     // nhãn hiển thị, vd "Ghi tín hiệu RF"
    ActionKind  kind;
    LegalTier   tier;
    bool        radioExclusive; // cần độc chiếm radio (teardown AP dashboard)
    bool        needsBuffer;    // cần buffer đã Record trước đó (Replay)
};
```

Mỗi module thêm 2 hàm (song song với `status()/revision()` đã có):

```cpp
const ActionDescriptor *actions();   // mảng tĩnh, kết thúc bằng id == nullptr
size_t                   actionCount();
```

`ModuleRuntime` giữ nguyên state machine một-action-đang-chạy; nâng để biết
action nào đang chạy (`activeActionId`) thay vì chỉ cờ Running.

### 3.2. Catalog là nguồn sự thật cho parser

- `ws_command_json.cpp`: bỏ allowlist tay; thay bằng tra `action` id trong catalog
  của module tương ứng. Action không có trong catalog → `UnsupportedAction`.
- Lệnh `action` được mở rộng để mang **tham số**:
  `{"id":N,"module":"cc1101","cmd":"action","action":"rf_replay","params":{...}}`.
  Mỗi descriptor khai báo schema param tối thiểu; parser validate kiểu + biên.

### 3.3. Dashboard nhận catalog → render danh sách chọn

- Thêm frame WS `{"type":"catalog","modules":[{module, actions:[descriptor...]}]}`
  gửi khi client kết nối (và khi thay đổi).
- UI: mỗi module có một **danh sách action chọn được** (menu/dropdown hoặc nhóm
  nút) sinh từ catalog — không hard-code trong JS nữa. Chọn action → hiện form
  tham số (nếu có) → nút **Start/Stop** → vùng output riêng.
- Badge **tầng pháp lý** theo `tier`; action `Disruptive` có hộp xác nhận trước
  khi gửi.

## 4. Độc quyền radio & teardown AP

Học từ Bruce: nhiều attack cần độc chiếm radio nên Bruce teardown web UI trước
(`cleanlyStopWebUiForWiFiFeature()`), chạy xong khôi phục.

- Action `radioExclusive=false` (vd WiFi scan) chạy song song AP dashboard — như
  hiện tại.
- Action `radioExclusive=true` (deauth, beacon spam, sniffer, NRF jammer) cần một
  **radio arbiter**: tạm dừng/teardown AP dashboard, chạy attack, rồi khôi phục
  AP và thông báo client reconnect. Khi AP tắt, dashboard mất kết nối — cần cơ
  chế báo trước trên UI ("thiết bị sẽ ngắt WiFi khi chạy X, kết nối lại sau").
- CC1101 + NRF24 dùng chung SPI bus → arbiter cũng phải serialize hai module này
  (không chạy RF record và NRF jammer cùng lúc).

## 5. Danh sách attack theo module (catalog đề xuất)

Cột **Phase** xem §7. Cột **Tầng**: Observe / ActiveOwn / Disruptive.

### 5.1. CC1101 — Sub-GHz RF

| action id | Nhãn | Kind | Tầng | Radio độc quyền | Phase | Ghi chú |
|---|---|---|---|---|---|---|
| `rf_scan` | Quét/đo tần số | OneShot | Observe | không | B | Đo RSSI, đoán tần số tín hiệu |
| `rf_record` | Ghi tín hiệu | Record | Observe | có (SPI) | B | Lưu raw OOK/ASK vào RAM |
| `rf_replay` | Phát lại | Replay | ActiveOwn | có (SPI) | B | Cần buffer từ `rf_record` |
| `rf_spectrum` | Spectrum | Continuous | Observe | có (SPI) | E | Quét dải, stream mức tín hiệu |
| `rf_custom_tx` | Phát tùy chỉnh | OneShot | ActiveOwn | có (SPI) | E | Tần số + payload nhập tay |
| `rf_jammer` | Gây nhiễu | Continuous | **Disruptive** | có (SPI) | D | Constant carrier; chỉ môi trường được phép |

### 5.2. NRF24 — 2.4GHz

| action id | Nhãn | Kind | Tầng | Radio độc quyền | Phase | Ghi chú |
|---|---|---|---|---|---|---|
| `nrf_scan` | Quét kênh 2.4G | Continuous | Observe | có (SPI) | B | Analyzer hoạt động các kênh |
| `nrf_jammer` | Gây nhiễu 2.4G | Continuous | **Disruptive** | có (SPI) | D | Constant carrier quét kênh |
| `nrf_mousejack` | Mousejack | OneShot | **Disruptive** | có (SPI) | E (stretch) | Bruce cũng chưa hoàn thiện |

### 5.3. PN532 — NFC/RFID

| action id | Nhãn | Kind | Tầng | Radio độc quyền | Phase | Ghi chú |
|---|---|---|---|---|---|---|
| `nfc_read_uid` | Đọc UID | OneShot | Observe | không | A (đã có) | Giữ nguyên hành vi hiện tại |
| `nfc_read_dump` | Đọc dump | OneShot | Observe | không | C | Mifare Classic/Ultralight block |
| `nfc_clone_uid` | Clone UID | ActiveOwn | ActiveOwn | không | C | Ghi UID sang thẻ magic của mình |
| `nfc_write_ndef` | Ghi NDEF | OneShot | ActiveOwn | không | C | Ghi record NDEF |
| `nfc_emulate` | Giả lập thẻ | Continuous | ActiveOwn | không | E (stretch) | PN532 emulation hạn chế |

### 5.4. IR — Hồng ngoại

| action id | Nhãn | Kind | Tầng | Radio độc quyền | Phase | Ghi chú |
|---|---|---|---|---|---|---|
| `ir_capture` | Capture | Record | Observe | không | A (đã có) | Giữ nguyên; coi như Record vào buffer |
| `ir_replay` | Phát lại | Replay | ActiveOwn | không | B | Cần buffer từ `ir_capture` |
| `ir_tvbgone` | TV-B-Gone | OneShot | ActiveOwn | không | E | Bộ mã tắt TV phổ biến |
| `ir_custom_tx` | Phát tùy chỉnh | OneShot | ActiveOwn | không | E | NEC/SIRC/RC5/… nhập tay |

### 5.5. WiFi — sóng onboard ESP32-S3

| action id | Nhãn | Kind | Tầng | Radio độc quyền | Phase | Ghi chú |
|---|---|---|---|---|---|---|
| `wifi_scan` | Quét SSID | OneShot | Observe | không | A (đã có) | Chạy song song AP dashboard |
| `wifi_sniff` | Sniffer | Continuous | Observe | có | D | Promiscuous, cần teardown AP |
| `wifi_deauth` | Deauth | Continuous | **Disruptive** | có | D | Chỉ mạng của mình / được phép |
| `wifi_beacon` | Beacon spam | Continuous | **Disruptive** | có | D | Phát SSID giả |
| `wifi_evil_portal` | Evil portal | Continuous | **Disruptive** | có | E (stretch) | Captive portal thu thập |
| `wifi_wardrive` | Wardriving | Continuous | Observe | có | E (stretch) | Cần GPS/log, cân nhắc sau |

## 6. Phân tầng pháp lý & an toàn

- **Observe**: quan sát/thu (scan, record, spectrum, read). Rủi ro thấp — bật mặc định.
- **ActiveOwn**: phát lại/clone/ghi. Chỉ hợp pháp trên **thiết bị/thẻ/mạng của
  chính mình** hoặc khi được chủ sở hữu cho phép. UI nhắc trách nhiệm trước khi chạy.
- **Disruptive**: jam/deauth/beacon/evil-portal. **Ở nhiều quốc gia việc gây nhiễu
  tần số là bất hợp pháp.** Chỉ dùng trong môi trường được phép/được kiểm soát
  (phòng lab chắn sóng, mạng thử của mình). Bắt buộc:
  - Hộp **xác nhận** trước khi chạy, nêu rõ cảnh báo pháp lý.
  - Mặc định **tắt**; có thể thêm cờ build `ENABLE_DISRUPTIVE` để loại hẳn khỏi
    firmware khi nộp/demo nếu cần.
  - README thêm disclaimer (trùng ISSUE #16): chỉ dùng hợp pháp, được phép.

Kế hoạch này mô tả ở mức kiến trúc/tính năng, không đi vào tham số tối ưu hoá
mức độ phá hoại.

## 7. Lộ trình theo phase

- **Phase A — Catalog framework** (không thêm attack mới): thêm `ActionDescriptor`
  + `actions()/actionCount()`; chuyển `ws_command_json` sang tra catalog; thêm
  frame `catalog`; dashboard render danh sách chọn được từ catalog, migrate 3
  action đang có (`wifi_scan`, `nfc_read_uid`, `ir_capture`). Rủi ro thấp, nền tảng.
- **Phase B — Record/Replay + quan sát RF**: `rf_scan`, `rf_record`/`rf_replay`,
  `ir_replay`, `nrf_scan`; buffer RAM; radio arbiter sơ bộ (teardown khi cần).
- **Phase C — NFC mở rộng**: `nfc_read_dump`, `nfc_clone_uid`, `nfc_write_ndef`.
- **Phase D — Disruptive (sau rào pháp lý)**: `rf_jammer`, `nrf_jammer`,
  `wifi_sniff`, `wifi_deauth`, `wifi_beacon`; hoàn thiện arbiter + teardown/khôi
  phục AP + luồng reconnect dashboard.
- **Phase E — Stretch**: `rf_spectrum`, `rf_custom_tx`, `ir_tvbgone`,
  `ir_custom_tx`, `nfc_emulate`, `nrf_mousejack`, `wifi_evil_portal`,
  `wifi_wardrive`.

## 8. Thay đổi protocol (tóm tắt)

- Thêm frame server→client: `catalog` (lúc kết nối), `action_output` (stream cho
  Continuous), tái dùng `status`/`command_result` hiện có.
- Mở rộng lệnh `action` mang `params` object; firmware validate theo descriptor.
- Stop cho Continuous: tái dùng **slot Stop dành riêng mỗi module** đã có trong
  `CommandQueue` (disable = stop action đang chạy + giải phóng, đúng model on/off).
- Buffer Record/Replay: RAM only (mất khi tắt module), khớp model on/off hiện tại;
  LittleFS save/load để sau nếu cần.

## 9. Kiểm thử

- **Native (không cần board)**: parse lệnh `action` + `params` theo catalog; tra
  catalog; validate tầng/biên tham số; state machine Record→Replay (buffer rỗng →
  Replay bị từ chối). Mở rộng bộ `test/` hiện có.
- **Phần cứng (AC từng attack)**: mỗi action một tiêu chí nghiệm thu riêng (vd
  `rf_replay` bật được thiết bị đích của mình; `ir_replay` lặp đúng mã; teardown AP
  rồi khôi phục được). Bảng AC chi tiết lập khi `/to-spec`.
- CI: thêm `pio test -e native` + `pio run` (trùng ISSUE #15) để bảo vệ framework
  catalog khi thêm action.

## 10. Việc cần chốt (open questions)

- Form tham số trên dashboard tới đâu (preset vs nhập tay tần số/giao thức)?
- Có build flag loại bỏ nhóm Disruptive khi nộp đồ án không?
- Buffer Record giữ RAM hay cho lưu LittleFS để tái dùng giữa các lần bật?
- Khi teardown AP để chạy attack độc quyền radio, cách báo và tự kết nối lại của
  dashboard (timeout, thử lại) như thế nào?
- Thứ tự ưu tiên Phase B/C (RF trước hay NFC trước) theo phần cứng đã test được.

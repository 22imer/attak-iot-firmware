# PLAN.md — Kế hoạch implement payload đa năng theo module

> Mục đích: **xác định boundary dự án** và chia nhỏ công việc thành các **ticket
> độc lập để giao cho subagent** triển khai. Đây là tài liệu kế hoạch — không phải
> lệnh implement ngay. Mỗi subagent nhận **một ticket**, làm theo hợp đồng kiến
> trúc ở §3, rồi được review bằng [`REVIEW.md`](REVIEW.md).
>
> Nền tảng đã có (Phase A, đã merge): catalog action (`src/core/action_catalog.*`,
> `catalog_json.cpp`), parser tra catalog (`ws_command_json.cpp`), dashboard đọc
> frame `catalog`. Bối cảnh kiến trúc đầy đủ: [`docs/planning/attack-modules-plan.md`](docs/planning/attack-modules-plan.md).
> Nguồn tham khảo tính năng: readme Bruce (BruceDevices/firmware).

---

## 1. Nguyên tắc chủ đạo

- **Mỗi module phơi NHIỀU payload** (như mục WiFi của Bruce), khai báo trong catalog;
  dashboard render thành danh sách chọn được. Thêm payload = thêm `ActionDescriptor`
  + 1 handler, KHÔNG sửa parser/dashboard.
- **Chỉ tham khảo Bruce, tự viết code** — KHÔNG copy mã AGPL-3.0 (lý do: xung đột
  với RF24 GPL-2.0, đã chốt trong `map.md`).
- **Phân tầng pháp lý**: `observe` / `active_own` / `disruptive`. Nhóm `disruptive`
  có rào xác nhận trên UI + build flag loại bỏ được.
- Mỗi ticket phải **độc lập, có acceptance criteria rõ**, và **thêm test native**
  cho phần logic portable.

## 2. Boundary dự án (in / out of scope)

### 2.1. Trong phạm vi (sẽ implement — nhiều payload/module)

| Module | Payload trong phạm vi |
|---|---|
| WiFi (onboard) | `scan` (đã có), `sniff` (raw), `beacon_spam`, `deauth` (target + flood), `evil_portal` (stretch) |
| CC1101 (sub-GHz) | `rf_scan`, `rf_record` + `rf_replay`, `rf_spectrum`, `rf_custom_tx`, `rf_jammer` (full + intermittent) |
| NRF24 (2.4GHz) | `nrf_scan` (2.4G spectrum/analyzer), `nrf_jammer` |
| PN532 (NFC) | `read_uid` (đã có), `nfc_read_dump`, `nfc_clone_uid`, `nfc_write_ndef`, `nfc_erase` |
| IR | `capture` (đã có), `ir_replay`, `ir_tvbgone`, `ir_custom_tx` |

### 2.2. Ngoài phạm vi (non-goals — KHÔNG làm đợt này)

- **BLE toàn bộ** (BLE scan/spam/BadBLE). ESP32-S3 có BLE nhưng không thuộc 4 module
  đã mua; để phase riêng nếu cần.
- **WiFi networked tooling của Bruce**: TelNet, SSH, TCP client/listener, Scan hosts,
  Responder, ARP spoof/poison, Wireguard, Brucegotchi, Wardriving(+GPS) — quá nặng
  cho capstone và cần network stack/GPS.
- **NFC companion-chip**: Amiibolink, Chameleon, PN532Killer, save/load file LittleFS
  — cần lib companion AGPL-3.0 (đã loại trong `map.md`).
- **OTA, battery management, PCB tùy chỉnh.**
- Boundary này là quyết định có chủ đích: định nghĩa cả cái KHÔNG làm. Thay đổi phải
  cập nhật file này trước.

### 2.3. Boundary pháp lý & phần cứng

- `disruptive` (jam/deauth/beacon/evil_portal): chỉ chạy trong môi trường được phép;
  mặc định tắt; bọc sau build flag `ENABLE_DISRUPTIVE` (không định nghĩa = loại khỏi
  catalog + binary). PLAN này mô tả mức kiến trúc/tính năng, không tối ưu mức phá hoại.
- Phần cứng cố định: ESP32-S3-N16R8; CC1101+NRF24 chung SPI bus; PN532 I2C; IR RX/TX
  rời (xem `include/board_pins.h`). Không đổi pin mapping.

## 3. Hợp đồng kiến trúc (BẮT BUỘC cho mọi ticket)

Subagent PHẢI tuân thủ, review sẽ kiểm theo đúng các mục này:

1. **Catalog là nguồn sự thật.** Payload mới = thêm `ActionDescriptor` vào
   `src/core/action_catalog.cpp` (id, label, `ActionId`, `ActionKind`, `LegalTier`,
   `radioExclusive`, `needsBuffer`) + nhánh dispatch trong module. KHÔNG allowlist tay
   trong parser.
2. **Mở rộng `ActionId`** trong `command_types.h` cho mỗi action mới (giữ thứ tự enum
   hiện có; chỉ thêm vào cuối).
3. **Vòng đời qua `ModuleRuntime`**: OneShot dùng `beginAction/completeAction/failAction/expire`
   sẵn có. Continuous/Record/Replay cần phần mở rộng ở ticket framework (T02/T03) — KHÔNG
   tự chế state machine song song trong module.
4. **Không chặn `loop()`**: thao tác chip phải có budget thời gian (mẫu: PN532
   `kCommandBudgetMs`, CC1101 timeout chờ MISO). Dùng cơ chế deadline/poll.
5. **Bộ nhớ payload (record)**: RAM only, mất khi tắt module (khớp model on/off). KHÔNG
   ghi LittleFS trừ khi ticket nói rõ.
6. **Radio độc quyền**: action `radioExclusive=true` phải đi qua radio arbiter (T04):
   teardown AP dashboard → chạy → khôi phục AP. KHÔNG tự ý tắt AP trong module.
7. **License**: tham khảo Bruce, tự viết. KHÔNG copy mã AGPL. Ghi rõ file Bruce đã
   tham khảo trong comment/commit.
8. **Test native**: mọi logic portable (parse param, format payload, state machine,
   chọn kênh…) phải có test trong `test/` và được thêm vào `build_src_filter` của
   env `native` trong `platformio.ini`. Code chạm Arduino/SPI/WiFi/RMT để ESP32-only.
9. **Dashboard DOM an toàn**: dữ liệu từ thiết bị chỉ qua `textContent`/prop `text`,
   không `innerHTML`. Payload mới cần renderer riêng trong `dashboard.js`.
10. **Disruptive**: handler + descriptor bọc trong `#ifdef ENABLE_DISRUPTIVE`; UI đã
    có confirm theo `tier`.

## 4. Ticket framework (làm trước — các payload phụ thuộc)

> Thứ tự: T01 → T02 → T03 → T04. Payload nhóm §5 phụ thuộc các ticket này.

### T01 — Tham số lệnh (`params`)
- **Mục tiêu**: lệnh `action` mang `params` object; validate theo mô tả param của
  descriptor.
- **Touch**: `ws_command.h` (thêm field params đã-parse), `ws_command_json.cpp`,
  `action_catalog.*` (khai báo param spec tối thiểu: tên, kiểu, min/max).
- **AC**: param hợp lệ → parse; sai kiểu/biên → `InvalidCommand`/`UnsupportedAction`;
  action không cần param vẫn chạy. Test native đủ nhánh.

### T02 — Action Continuous + streaming output
- **Mục tiêu**: hỗ trợ action chạy liên tục tới khi Stop; thêm frame server→client
  `action_output` để stream; Stop dùng **slot Stop dành riêng mỗi module** đã có.
- **Touch**: `module_runtime.*` (trạng thái Continuous không auto "succeeded"),
  `web_dashboard.cpp` (gửi `action_output`), `dashboard.js` (nhận + render stream,
  nút Stop).
- **AC**: start→stream→stop sạch; disable khi đang chạy = stop + cleanup; test native
  cho state machine.

### T03 — Buffer record/replay (RAM)
- **Mục tiêu**: lưu payload ghi được gần nhất mỗi module trong RAM; `needsBuffer`
  (Replay) bị từ chối khi buffer rỗng.
- **Touch**: core buffer nhỏ (portable), `module_runtime` hook, dispatch.
- **AC**: Record→Replay dùng đúng buffer; Replay khi rỗng → lỗi rõ; tắt module xóa
  buffer; test native.

### T04 — Radio arbiter (teardown/restore AP)
- **Mục tiêu**: serialize AP dashboard với action `radioExclusive`; teardown AP → chạy
  → khôi phục; đồng thời serialize CC1101 ↔ NRF24 (chung SPI).
- **Touch**: module mới `src/core/radio_arbiter.*`, `wifi_ap.*`, `web_dashboard.cpp`,
  `dashboard.js` (cảnh báo mất kết nối + auto-reconnect).
- **AC**: chạy action độc quyền rồi AP trở lại, dashboard tự reconnect; không chạy
  song song 2 action chung bus; phần state machine test native, phần radio ghi AC
  phần cứng.

## 5. Ticket payload theo module

> Mỗi payload = 1 ticket. Mẫu chung ở §6. Cột **Tier**: O=observe, A=active_own,
> D=disruptive. Cột **Dep**: ticket phụ thuộc.

### 5.1. CC1101 (sub-GHz)

| Ticket | action id | Tier | Kind | Dep | Tham khảo Bruce |
|---|---|---|---|---|---|
| T10 | `rf_scan` | O | OneShot | T01 | RF "Scan/Copy" |
| T11 | `rf_record` / `rf_replay` | O / A | Record/Replay | T03 | RF "Replay" |
| T12 | `rf_spectrum` | O | Continuous | T02,T04 | RF "Spectrum" |
| T13 | `rf_custom_tx` | A | OneShot | T01 | RF "Custom SubGhz" |
| T14 | `rf_jammer` | D | Continuous | T02,T04 | RF "Jammer Full/Intermittent" |

### 5.2. NRF24 (2.4GHz)

| Ticket | action id | Tier | Kind | Dep | Tham khảo Bruce |
|---|---|---|---|---|---|
| T20 | `nrf_scan` | O | Continuous | T02,T04 | "2.4G Spectrum" |
| T21 | `nrf_jammer` | D | Continuous | T02,T04 | "NRF24 Jammer" |

### 5.3. PN532 (NFC)

| Ticket | action id | Tier | Kind | Dep | Tham khảo Bruce |
|---|---|---|---|---|---|
| T30 | `nfc_read_dump` | O | OneShot | T01 | RFID "Read tag" |
| T31 | `nfc_clone_uid` | A | OneShot | T03 | RFID "Clone tag" |
| T32 | `nfc_write_ndef` | A | OneShot | T01 | RFID "Write NDEF records" |
| T33 | `nfc_erase` | A | OneShot | — | RFID "Erase data" |

### 5.4. IR

| Ticket | action id | Tier | Kind | Dep | Tham khảo Bruce |
|---|---|---|---|---|---|
| T40 | `ir_replay` | A | Replay | T03 | IR "Custom IR"/replay |
| T41 | `ir_tvbgone` | A | OneShot | — | IR "TV-B-Gone" |
| T42 | `ir_custom_tx` | A | OneShot | T01 | IR "Custom IR" (NEC/SIRC/RC5/…) |

### 5.5. WiFi (onboard)

| Ticket | action id | Tier | Kind | Dep | Tham khảo Bruce |
|---|---|---|---|---|---|
| T50 | `wifi_sniff` | O | Continuous | T02,T04 | WiFi "RAW Sniffer" |
| T51 | `wifi_beacon` | D | Continuous | T02,T04 | WiFi "Beacon Spam" |
| T52 | `wifi_deauth` | D | Continuous | T02,T04 | WiFi "Target Deauth / Deauth Flood" |
| T53 | `wifi_evil_portal` | D | Continuous | T02,T04 | WiFi "Evil Portal" |

## 6. Mẫu ticket (subagent điền khi nhận việc)

```
### <Txx> — <action id>
- Tier / Kind / Dep: …
- Mục tiêu: 1–2 câu.
- Files touch: liệt kê chính xác (catalog, command_types, module cpp/h, dashboard, test, platformio.ini).
- Hợp đồng: dùng ModuleRuntime/arbiter/buffer nào; frame WS liên quan.
- Param (nếu có): tên, kiểu, min/max.
- Tham khảo Bruce: file/khu vực đã đọc (chỉ học, không copy).
- Acceptance (native): các test phải thêm + pass.
- Acceptance (hardware): tiêu chí đo trên board (deferred).
- Ngoài phạm vi ticket: …
```

## 7. Definition of Done (toàn cục, mọi ticket)

1. `pio test -e native` xanh (gồm test mới của ticket).
2. `pio run -e attak-iot-firmware` build pass.
3. Payload xuất hiện trong catalog + dashboard render + chạy được end-to-end (hardware
   AC có thể defer nhưng phải ghi lại).
4. Không hồi quy: 3 action Phase A (`scan`, `read_uid`, `capture`) vẫn chạy.
5. Tuân thủ đủ §3 (đặc biệt: không copy AGPL, không chặn loop, disruptive có gate).
6. Comment nêu file Bruce tham khảo; commit theo chuẩn repo.

## 8. Cách dùng với subagent

1. Chọn 1 ticket (tôn trọng `Dep`); giao subagent kèm: file này, `attack-modules-plan.md`,
   mã Phase A liên quan.
2. Subagent điền mẫu §6, implement, tự chạy `pio test`/`pio run`.
3. Reviewer dùng [`REVIEW.md`](REVIEW.md) để nghiệm thu trước khi merge.
4. Ưu tiên: T01–T04 trước → nhóm Observe (T10, T20, T30, T40) → active_own → disruptive
   (sau khi có `ENABLE_DISRUPTIVE` + gate).

## 9. Việc cần chốt trước khi phát ticket disruptive

- Bật/tắt `ENABLE_DISRUPTIVE` mặc định khi nộp đồ án?
- Môi trường test disruptive (phòng lab/anten giả) đã sẵn sàng chưa?
- Buffer record giữ RAM hay cho lưu LittleFS để tái dùng?

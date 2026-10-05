# PLAN.md — Kế hoạch implement payload đa năng theo module

> Mục đích: **xác định boundary** và chia nhỏ công việc thành **ticket độc lập để
> giao cho subagent**. Tài liệu kế hoạch — không phải lệnh implement ngay. Mỗi
> subagent nhận **một ticket**, theo hợp đồng kiến trúc §3, rồi được review bằng
> [`REVIEW.md`](REVIEW.md).
>
> **Phạm vi (cập nhật 2026-10-05):** giữ **toàn bộ payload** như Bruce, gồm cả nhóm
> gây nhiễu — dự án là **bài tập lớn chạy trong môi trường thử nghiệm được phép**.
> Chiến lược: **phát triển nền móng trước** (framework dùng chung + bring-up phần
> cứng), payload là lớp mỏng đặt lên trên; KHÔNG cắt bớt payload để "cho kịp".
>
> Nền tảng đã có (Phase A, merged): catalog action (`src/core/action_catalog.*`,
> `catalog_json.cpp`), parser tra catalog (`ws_command_json.cpp`), dashboard đọc
> frame `catalog`. Kiến trúc nền: [`docs/planning/attack-modules-plan.md`](docs/planning/attack-modules-plan.md).

---

## 1. Nguyên tắc chủ đạo

- **Nền móng trước, payload sau.** Giá trị kỹ thuật nằm ở framework dùng chung: thêm
  payload mới phải chỉ còn là "thêm 1 descriptor + 1 handler", không đụng parser/
  dashboard/vòng đời. Đầu tư vào §4 để §5 thành việc cơ học.
- **Mỗi module phơi NHIỀU payload** khai báo trong catalog; dashboard render danh
  sách chọn được.
- **Chỉ tham khảo Bruce, tự viết code** — KHÔNG copy mã AGPL-3.0 (xung đột RF24
  GPL-2.0, đã chốt trong `map.md`).
- **Môi trường thử nghiệm được phép.** Nhóm `disruptive` nằm trong phạm vi. Vẫn giữ
  rào xác nhận UI + cờ build `ENABLE_DISRUPTIVE` như vệ sinh kỹ thuật (để tạo bản
  không chứa chúng khi cần mang ra ngoài phòng lab), không phải để loại khỏi phạm vi.
- Mỗi ticket **độc lập, có acceptance criteria rõ**, và **thêm test native** cho
  phần logic portable.

## 2. Boundary dự án

### 2.1. Trong phạm vi — toàn bộ payload (nhiều/ module)

| Module | Payload |
|---|---|
| WiFi (onboard) | `scan` (đã có), `sniff` (raw), `beacon_spam`, `deauth` (target + flood), `evil_portal` |
| CC1101 (sub-GHz) | `rf_scan`, `rf_record` + `rf_replay`, `rf_spectrum`, `rf_custom_tx`, `rf_jammer` (full + intermittent) |
| NRF24 (2.4GHz) | `nrf_scan` (2.4G spectrum/analyzer), `nrf_jammer` |
| PN532 (NFC) | `read_uid` (đã có), `nfc_read_dump`, `nfc_clone_uid`, `nfc_write_ndef`, `nfc_erase` |
| IR | `capture` (đã có), `ir_replay`, `ir_tvbgone`, `ir_custom_tx` |

### 2.2. Ngoài phạm vi (non-goals — vì phần cứng/lib, KHÔNG phải vì cắt cho kịp)

- **BLE toàn bộ** — không thuộc 4 module đã mua; để phase riêng nếu cần.
- **WiFi networked tooling của Bruce**: TelNet, SSH, TCP client/listener, Scan hosts,
  Responder, ARP spoof/poison, Wireguard, Brucegotchi, Wardriving(+GPS) — cần network
  stack/GPS ngoài trọng tâm RF/embedded.
- **NFC companion-chip**: Amiibolink, Chameleon, PN532Killer, save/load file — cần lib
  companion AGPL-3.0 (đã loại trong `map.md`).
- **OTA, battery management, PCB tùy chỉnh.**
- Thay đổi boundary phải cập nhật file này trước.

### 2.3. Boundary pháp lý & phần cứng

- Dự án chạy trong **môi trường thử nghiệm được phép**; chỉ dùng trên thiết bị/mạng
  của mình hoặc được cho phép. `disruptive` giữ confirm UI + cờ `ENABLE_DISRUPTIVE`
  (bản mặc định ngoài lab không chứa chúng). README cần disclaimer (ISSUE #16).
- Phần cứng cố định: ESP32-S3-N16R8; CC1101+NRF24 chung SPI; PN532 I2C; IR RX/TX rời
  (`include/board_pins.h`). Không đổi pin mapping.

## 3. Hợp đồng kiến trúc (BẮT BUỘC mọi ticket)

1. **Catalog là nguồn sự thật.** Payload mới = thêm `ActionDescriptor` vào
   `action_catalog.cpp` + nhánh dispatch trong module. KHÔNG allowlist tay.
2. **`ActionId`** mới chỉ thêm vào cuối enum (`command_types.h`).
3. **Vòng đời qua `ModuleRuntime`** + phần mở rộng framework (§4); KHÔNG state machine
   song song tự chế.
4. **Không chặn `loop()`**: thao tác chip có budget/deadline (mẫu PN532/CC1101).
5. **Buffer record**: RAM only (khớp model on/off); không ghi LittleFS trừ khi ticket nói.
6. **Radio độc quyền** phải qua radio arbiter (T04); module KHÔNG tự tắt AP.
7. **License**: tham khảo Bruce, tự viết; KHÔNG copy AGPL; ghi chú file đã tham khảo.
8. **Test native** cho mọi logic portable + thêm vào `build_src_filter` env `native`.
9. **Dashboard DOM an toàn**: dữ liệu thiết bị chỉ qua `textContent`/prop `text`.
10. **Disruptive**: descriptor + handler bọc `#ifdef ENABLE_DISRUPTIVE`; UI confirm theo `tier`.

## 4. NỀN MÓNG (ưu tiên cao nhất — làm trước mọi payload)

> Đây là trọng tâm đầu tư. Payload §5 chỉ bắt đầu khi nền móng tương ứng đã vững.

### F0 — Bring-up phần cứng (gate tất cả)
- **Mục tiêu**: chứng minh từng chip phản hồi trên board thật trước khi xây tiếp.
- **Nội dung**: flash firmware observation hiện có; xác nhận CC1101 PARTNUM/VERSION,
  NRF24 `isChipConnected`, PN532 firmware version, IR RX nhận tín hiệu; xác nhận SPI
  dùng chung CC1101+NRF24 không xung đột (ISSUE #6: RF24 có ghi đè `SPI.begin`?); AP +
  dashboard điều khiển được.
- **AC**: mỗi chip báo "ok" trên Serial + dashboard; ghi lại mốc 15s/2s thực đo.
- **Quy tắc**: KHÔNG viết payload chạm phần cứng nào trước khi chip đó xanh ở F0.

### F1 — Tham số lệnh (`params`)
- Lệnh `action` mang `params`; validate theo param-spec của descriptor.
- Touch: `ws_command.*`, `ws_command_json.cpp`, `action_catalog.*`. AC: test native đủ nhánh.

### F2 — Action Continuous + streaming output
- Action chạy tới khi Stop; frame `action_output` stream; Stop qua slot Stop dành riêng.
- Touch: `module_runtime.*`, `web_dashboard.cpp`, `dashboard.js`. AC: start→stream→stop sạch; test native state machine.

### F3 — Buffer record/replay (RAM)
- Lưu payload ghi gần nhất mỗi module; `needsBuffer` chặn khi rỗng; tắt module xóa buffer.
- AC: Record→Replay đúng buffer; Replay rỗng → lỗi rõ; test native.

### F4 — Radio arbiter + **giải quyết mâu thuẫn kênh điều khiển** (ticket then chốt)
- **Vấn đề cốt lõi**: action WiFi độc quyền radio phải teardown AP → **mất dashboard
  đang ra lệnh**. Thiết bị headless không có màn hình như Bruce. Nền móng này PHẢI
  giải xong trước mọi payload `radioExclusive`.
- **Phải chốt (thiết kế trước khi code)**: cơ chế nào giữ được khả năng Dừng/điều
  khiển khi AP tắt? Lựa chọn: (a) chạy có thời hạn cứng rồi tự khôi phục AP; (b) kênh
  phụ (Serial/BLE) để Stop; (c) nhận lệnh "run N giây" một chiều rồi AP trở lại +
  dashboard auto-reconnect. Ghi quyết định vào đây.
- **Cũng serialize** CC1101 ↔ NRF24 (chung SPI bus).
- Touch: `src/core/radio_arbiter.*`, `wifi_ap.*`, `web_dashboard.cpp`, `dashboard.js`.
- **AC**: sau action độc quyền, AP khôi phục + dashboard reconnect; không chạy song
  song 2 action chung bus; state machine test native; phần radio ghi AC phần cứng.

## 5. Ticket payload theo module

> Mỗi payload = 1 ticket. Mẫu §7. Tier: O=observe, A=active_own, D=disruptive.
> **Dep** gồm cả ticket nền móng §4 (và F0 ngầm định cho mọi payload chạm chip).

### 5.1. CC1101 (sub-GHz)
| Ticket | action id | Tier | Kind | Dep |
|---|---|---|---|---|
| T10 | `rf_scan` | O | OneShot | F1 |
| T11 | `rf_record` / `rf_replay` | O / A | Record/Replay | F3 |
| T12 | `rf_spectrum` | O | Continuous | F2,F4 |
| T13 | `rf_custom_tx` | A | OneShot | F1 |
| T14 | `rf_jammer` (full+intermittent) | D | Continuous | F2,F4 |

### 5.2. NRF24 (2.4GHz)
| Ticket | action id | Tier | Kind | Dep |
|---|---|---|---|---|
| T20 | `nrf_scan` | O | Continuous | F2,F4 |
| T21 | `nrf_jammer` | D | Continuous | F2,F4 |

### 5.3. PN532 (NFC)
| Ticket | action id | Tier | Kind | Dep |
|---|---|---|---|---|
| T30 | `nfc_read_dump` | O | OneShot | F1 |
| T31 | `nfc_clone_uid` | A | OneShot | F3 |
| T32 | `nfc_write_ndef` | A | OneShot | F1 |
| T33 | `nfc_erase` | A | OneShot | — |

### 5.4. IR
| Ticket | action id | Tier | Kind | Dep |
|---|---|---|---|---|
| T40 | `ir_replay` | A | Replay | F3 |
| T41 | `ir_tvbgone` | A | OneShot | — |
| T42 | `ir_custom_tx` | A | OneShot | F1 |

### 5.5. WiFi (onboard) — tất cả `radioExclusive`, phụ thuộc F4
| Ticket | action id | Tier | Kind | Dep |
|---|---|---|---|---|
| T50 | `wifi_sniff` | O | Continuous | F2,F4 |
| T51 | `wifi_beacon` | D | Continuous | F2,F4 |
| T52 | `wifi_deauth` (target+flood) | D | Continuous | F2,F4 |
| T53 | `wifi_evil_portal` | D | Continuous | F2,F4 |

## 6. Lộ trình foundation-first

- **Phase 0 — Bring-up (F0):** gate cứng. Chip phản hồi + observation release xanh
  trên board. Không code payload chạm chip trước khi xong.
- **Phase 1 — Nền móng (F1→F4):** params, continuous/streaming, buffer, radio arbiter
  (+ chốt mâu thuẫn kênh điều khiển). Đây là phần đầu tư chính.
- **Phase 2 — Observe:** T10, T20, T30, T50 (đặt lên nền F1/F2/F4).
- **Phase 3 — active_own:** T11, T13, T31, T32, T33, T40, T41, T42.
- **Phase 4 — disruptive (trong môi trường được phép):** T12, T14, T21, T51, T52, T53
  — sau khi F4 vững và `ENABLE_DISRUPTIVE` + confirm sẵn sàng.
- **Quy tắc gate:** không mở ticket payload khi ticket nền móng ở `Dep` chưa xanh;
  không viết code chạm chip khi F0 của chip đó chưa xanh.

## 7. Mẫu ticket (subagent điền)

```
### <id> — <action id | tên>
- Tier / Kind / Dep: …
- Mục tiêu: 1–2 câu.
- Files touch: catalog, command_types, module cpp/h, dashboard, test, platformio.ini.
- Hợp đồng: ModuleRuntime/arbiter/buffer nào; frame WS liên quan; param (tên/kiểu/biên).
- Tham khảo Bruce: file/khu vực đã đọc (học, không copy).
- Acceptance (native): test phải thêm + pass.
- Acceptance (hardware): tiêu chí đo trên board (deferred nếu chưa có board).
- Ngoài phạm vi ticket: …
```

## 8. Definition of Done (toàn cục)

1. `pio test -e native` xanh (gồm test mới). 2. `pio run -e attak-iot-firmware` build pass.
3. Payload vào catalog + dashboard render + chạy end-to-end (hardware AC có thể ghi nợ).
4. Không hồi quy 3 action Phase A. 5. Tuân thủ §3 (không copy AGPL, không chặn loop,
disruptive có gate). 6. Comment nêu file Bruce tham khảo; commit theo chuẩn repo.

## 9. Việc cần chốt

- Board đã có trong tay chưa? (F0 là đường găng — mọi thứ chờ nó.)
- F4: chọn cơ chế giữ điều khiển khi teardown AP (a/b/c ở §F4)?
- `ENABLE_DISRUPTIVE` mặc định bật trong bản lab, tắt trong bản mang ra ngoài?
- Buffer record RAM-only hay cho phép lưu LittleFS để tái dùng?

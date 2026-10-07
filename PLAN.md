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
>
> **Cập nhật kiến trúc 2026-10-06 (§13):** đường quản trị đổi sang **USB NCM**
> (`192.168.7.1`, DHCP) là chính; **AP tắt khi boot**, bật/tắt bằng Serial
> `ap on`/`ap off`. Điều này giải cấu trúc vấn đề mất-dashboard của F4/§2.4 —
> dashboard không còn nằm trên radio WiFi. 17 action observe/active_own (§11) +
> 5 payload disruptive sau `#ifdef ENABLE_DISRUPTIVE` (§12) đã tích hợp phần mềm;
> hardware AC toàn bộ còn chờ board.

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

### 2.4. Quyết định nhóm disruptive (2026-10-06)

Người dùng chốt hướng triển khai cho T14, T21, T51, T52, T53 (thay cho mô hình
"chỉ teardown AP trong cửa sổ 30 s" của F4 áp cho các payload này):

- **WiFi payload dùng chính AP của thiết bị** (không teardown sang STA): beacon
  spam và deauth phát khung thô qua `esp_wifi_80211_tx(WIFI_IF_AP, ...)`; evil
  portal phục vụ captive portal (DNS spoof + trang đăng nhập) trên chính AP đó.
  Vì vậy ba payload WiFi này **không** `radioExclusive`; `wifi_sniff` vẫn giữ mô
  hình exclusive cũ. **USB NCM (§13):** AP **tắt khi boot**; beacon/deauth
  vẫn cần Serial `ap on` trước. `wifi_evil_portal` tự bật AP tạm theo action,
  kể cả khi điều khiển qua USB và chưa bật AP quản trị. Stop/disable hoặc lỗi
  khởi tạo khôi phục mode/kênh trước action; không đổi ý định `ap on/off`.
  Portal chỉ shadow HTTP *trên AP*, không ảnh hưởng dashboard USB NCM.
- **Hai kênh điều khiển độc lập radio WiFi**: (1) **USB NCM** là đường quản trị
  chính — dashboard HTTP/WS tại `192.168.7.1`, không nằm trên radio WiFi nên
  không bị payload WiFi shadow (§13); (2) `serial_console` nhận đúng JSON lệnh
  như WebSocket, dispatch qua cùng `dispatchCommand`, in `command_result` và
  snapshot `[F0]`, đồng thời mang lệnh `ap on`/`ap off`. Nhờ USB NCM, vấn đề cốt
  lõi của F4 (mất dashboard khi AP tắt) **đã được giải cấu trúc**; hard-window
  30 s của F4 nay chỉ còn vai trò khôi phục radio cho action exclusive
  (`wifi_sniff`), không còn là cơ chế duy nhất giữ quyền điều khiển.
- **Cờ `ENABLE_DISRUPTIVE` mặc định TẮT** ở `env:attak-iot-firmware` (bản mặc
  định không chứa payload/catalog disruptive, đúng REVIEW §2.5). Có
  `env:attak-iot-firmware-lab` bật cờ để dùng trong phòng lab; `env:native` cũng
  bật cờ để test logic portable.
- `rf_jammer` (CC1101) và `nrf_jammer` (NRF24) dùng carrier liên tục qua
  PATABLE/GDO0 và `RF24::startConstCarrier()`; vẫn qua arbiter `SharedSpi`.

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

### F0 — Bring-up phần cứng (gate payload và nghiệm thu hardware)
- **Mục tiêu**: chứng minh từng chip phản hồi trên board thật trước khi mở payload.
- **Nội dung**: flash firmware observation hiện có; xác nhận CC1101 PARTNUM/VERSION,
  NRF24 `isChipConnected`, PN532 firmware version, IR RX nhận tín hiệu; xác nhận SPI
  dùng chung CC1101+NRF24 không xung đột (ISSUE #6: RF24 có ghi đè `SPI.begin`?); AP +
  dashboard điều khiển được.
- **AC**: mỗi chip báo "ok" trên Serial + dashboard; ghi lại mốc 15s/2s thực đo.
- **Quy tắc cập nhật theo lựa chọn người dùng (2026-10-05):** cho phép viết/tích
  hợp payload software-first trước F0; native/build/smoke không thay hardware AC.
  Chỉ công bố payload chạy được trên chip sau F0 và nghiệm thu action thật.

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
- **Quyết định software-first (2026-10-05):** chọn (a), thời hạn cứng do server
  sở hữu **30.000 ms**, grace **250 ms** trước teardown AP để enqueue ack/thông báo.
  Dashboard không thể Stop khi AP vắng; firmware tự disable khi hết hạn, đợi
  cleanup rồi khôi phục AP. Không bổ sung BLE/Serial command transport.
- **Cập nhật (cutover USB NCM, 2026-10-06 — §13):** vấn đề cốt lõi ở trên nay
  được giải bằng kiến trúc chứ không chỉ bằng hard-window: dashboard sống trên
  **USB NCM** (`192.168.7.1`), không nằm trên radio WiFi, nên AP tắt không làm
  mất quyền điều khiển. Thêm `serial_console` là kênh thứ hai. Hard-window (a)
  vẫn áp cho `wifi_sniff` để **khôi phục radio** sau action exclusive; nó không
  còn là cách duy nhất giữ liên lạc. AP mặc định tắt khi boot, chỉ bật bằng
  `ap on`.
- **Cũng serialize** CC1101 ↔ NRF24 (chung SPI bus).
- Touch: `src/core/radio_arbiter.*`, `wifi_ap.*`, `web_dashboard.cpp`, `dashboard.js`.
- **AC**: sau action độc quyền, AP khôi phục + dashboard reconnect; không chạy song
  song 2 action chung bus; state machine test native; phần radio ghi AC phần cứng.

## 5. Ticket payload theo module

> Mỗi payload = 1 ticket. Mẫu §7. Tier: O=observe, A=active_own, D=disruptive.
> **Dep** gồm ticket nền móng §4; F0 vẫn bắt buộc cho nghiệm thu trên chip, không
> còn chặn viết phần mềm sau khi người dùng chọn software-first.

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
| T30 | `nfc_read_dump` | O | Record (hữu hạn; xem §11) | F1,F3 |
| T31 | `nfc_clone_uid` | A | OneShot | F3 |
| T32 | `nfc_write_ndef` | A | OneShot | F1 |
| T33 | `nfc_erase` | A | OneShot | — |

### 5.4. IR
| Ticket | action id | Tier | Kind | Dep |
|---|---|---|---|---|
| T40 | `ir_replay` | A | Replay | F3 |
| T41 | `ir_tvbgone` | A | OneShot | — |
| T42 | `ir_custom_tx` | A | OneShot | F1 |

### 5.5. WiFi (onboard) — `wifi_sniff` exclusive; beacon/deauth/portal dùng AP hiện tại (§2.4)
| Ticket | action id | Tier | Kind | Dep |
|---|---|---|---|---|
| T50 | `wifi_sniff` | O | Continuous | F2,F4 |
| T51 | `wifi_beacon` | D | Continuous | F2 |
| T52 | `wifi_deauth` (target+flood) | D | Continuous | F2 |
| T53 | `wifi_evil_portal` (+ evil twin: clone `ssid`, `channel`; deauth kèm `deauth`/`bssid`/`client`/`reason`/`intervalMs`) | D | Continuous | F2 + `serial_console` |

## 6. Lộ trình foundation-first

- **Phase 0 — Bring-up (F0):** gate nghiệm thu phần cứng. Chip phản hồi +
  observation release xanh trên board; đang ưu tiên flash/thử F0 theo yêu cầu mới.
- **Phase 1 — Nền móng (F1→F4):** người dùng cho phép hoàn thiện phần mềm khi chưa
  có phần cứng (2026-10-05): params, continuous/streaming, buffer, radio arbiter
  và tích hợp dashboard. Native/build/browser chứng minh logic, không thay F0.
- **Phase 2 — Observe:** T10, T20, T30, T50 (đặt lên nền F1/F2/F4).
- **Phase 3 — active_own:** T11, T13, T31, T32, T33, T40, T41, T42.
- **Phase 4 — disruptive (trong môi trường được phép):** T12, T14, T21, T51, T52, T53
  — sau khi F4 vững và `ENABLE_DISRUPTIVE` + confirm sẵn sàng.
- **Quy tắc gate:** dependency phần mềm ở `Dep` phải xanh trước tích hợp payload.
  Người dùng đã cho phép software-first toàn bộ action; F0 của chip vẫn là gate
  cho tuyên bố hoạt động trên phần cứng, không được dùng mock thay nghiệm thu.

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

- Board đã kết nối và flash F0 thành công; theo người dùng, chỉ CC1101 chưa lắp.
- F4 đã chốt (a): hard window 30.000 ms, grace 250 ms, restore sau cleanup (§F4).
  Áp cho action exclusive (`wifi_sniff`); WiFi disruptive dùng AP hiện tại (§2.4).
- **Đường quản trị đã chốt (cutover 2026-10-06, §13):** USB NCM `192.168.7.1`
  là đường chính; AP tắt khi boot, bật/tắt bằng Serial `ap on`/`ap off`. Giải
  cấu trúc vấn đề mất-dashboard của F4. Hardware AC (Windows nhận NCM + DHCP,
  phát 802.11 thô, AP dự phòng) còn chờ board.
- `ENABLE_DISRUPTIVE` đã chốt (2026-10-06): TẮT ở `env:attak-iot-firmware`, BẬT ở
  `env:attak-iot-firmware-lab` và `env:native` (§2.4).
- Buffer record đã chốt RAM-only; disable xóa, không persist LittleFS.

## 10. Trạng thái thực thi và phân tầng (2026-10-05)

**F0: đã flash + boot trên board thật; chưa nghiệm thu peripheral/action.**
- USB CH343 `1a86:55d3`, bus `4-1`, đã attach vào WSL thành `/dev/ttyACM0`.
- esptool xác nhận ESP32-S3 revision v0.2, flash 16 MB, PSRAM 8 MB; MAC
  `90:70:69:f7:d7:90`. Đã backup toàn bộ flash 16 MB trước ghi:
  `.pio/f0-backup-g1LZD3/original-16mb.bin` (không xóa backup).
- `uploadfs` và `upload` đều **SUCCESS**, các vùng ghi có **Hash of data verified**.
- UART 115200 sau reset: `wifiAp: AP "AttakIoT" up at 192.168.4.1` (log này có
  **trước** cutover USB NCM §13; boot hiện tại là AP-off + USB NCM);
  năm snapshot `[F0]` đều `en=0 ok=0 detail=off`, đúng boot-off, không phải
  kết luận chip lỗi. Log: `.pio/f0-backup-g1LZD3/boot-serial.log`.
- Boot báo `/littlefs/config.json` chưa có; đã lên AP bằng config mặc định.
  FastLED báo generic fallback clockless driver; LED/timing chưa được nghiệm thu.
- Người dùng xác nhận **CC1101 chưa lắp**, các module khác đã lắp. Không bật
  CC1101 trong lượt này; shared SPI hai radio vẫn chưa thể nghiệm thu.
- Máy tính đang ở Wi-Fi `192.168.1.x`, chưa ở subnet AP `192.168.4.x`; chưa gửi
  Enable/action đến NRF24/PN532/IR/WiFi. Cần nối AP rồi kiểm tra từng module,
  UID hai thẻ, remote IR và mốc 15s/2s. Không tự ngắt mạng Wi-Fi hiện tại.

Khảo sát ban đầu trước khi board kết nối (giữ làm lịch sử):

| Subagent | Phạm vi đã làm | Kết quả |
|---|---|---|
| BringupGate | Driver/probe, shared SPI, đường Serial/dashboard và bằng chứng F0 | Probe có trong source; thiếu board và chưa đo 15s/2s; tìm thấy thiếu Serial health và lỗi health khi disable |
| FoundationLayers | F1–F4, seam parser/runtime/buffer/arbiter và file giao nhau | Chưa có param-spec, streaming, replay buffer hoặc arbiter; lập boundary để tránh cùng sửa file |

Phần chuẩn bị F0 đã sửa: disable trả health off đúng hợp đồng dashboard;
Serial ghi snapshot revision bằng buffer cố định và drain có giới hạn.
Kiểm chứng host: `pio test -e native` **56/56 PASS**, build ESP32 **SUCCESS**,
build LittleFS **SUCCESS**; smoke formatter/drain Serial với UART đầy/ghi từng
phần và không flood; Chromium nhận ba frame runtime thật (ready → off/cleanup
→ off), hiển thị off và cho bật lại, không page error. Transport và UART trong
smoke là fixture, **không phải kết quả chạy trên board**.

Phân công triển khai Phase 1 software-first (đã được người dùng cho phép):

| Tầng | Ownership | Gate / phối hợp |
|---|---|---|
| F1 | Catalog param-spec, parser/request, test validation | Chốt API params trước khi migrate handler; integrator sở hữu `main.cpp` và dashboard |
| F2 | `ModuleRuntime`, streaming codec/bridge, test lifecycle | Tái dùng disable/slot Stop hiện có; không tự thêm một lệnh Stop khác |
| F3 | Buffer RAM portable, test record/replace/empty/clear | Không dùng JSON output làm buffer; hook disable/runtime qua integrator F2 |
| F4 | Arbiter portable, AP lifecycle, test exclusion/recovery | Chốt lựa chọn a/b/c trước code; tích hợp dựa trên lifecycle F2 |
| Payload observe / active_own / disruptive | Một ticket mỗi payload theo §5 | Người dùng cho phép software-first; dependency phần mềm phải xanh, hardware AC/F0 vẫn ghi riêng; catalog/UI đi qua integrator |

`main.cpp`, `dashboard.js`, catalog và `platformio.ini` có một integration owner,
không để nhiều subagent sửa đồng thời. **F1–F4 hoàn tất phần mềm**, đã tích hợp
module/main/dashboard. Catalog đã thêm 14 action observe/active_own (tổng 17
action), tái dùng các nền móng này; trạng thái payload và bằng chứng mới ở §11.
Hardware F4/peripheral/action vẫn chưa nghiệm thu. Lượt payload không flash,
commit hoặc push.

### Bằng chứng Layer 1 (lịch sử trước triển khai payload)

| Mục | Kết quả phần mềm | Hardware AC |
|---|---|---|
| F1 | Params bounded/owned, schema-driven validation, strict full JSON; form UTF-8/range/presence trên Chromium | Không cần board cho codec/queue; handler chip mới vẫn theo F0 |
| F2 | Start→stream→disable; generation/sequence/cadence, giữ final result; status xuất trước sample mới | Payload Continuous thật chưa triển khai |
| F3 | Record binary→Replay admission/đọc đúng bytes→disable clear; IR 512 timings, UNKNOWN/null và 64-bit lossless; đã xóa formatter legacy | Capture sensor và phát Replay thật chờ F0/ticket T40 |
| F4 | Main scheduler smoke: SPI exclusion tới cleanup, grace 250 ms, deadline 30 s wrap-safe, cleanup-held lease, restore fail/retry 1 s; backend AP restore không copy credentials | AP mất/khôi phục và shared SPI trên board còn chờ |

- `pio test -e native`: **131/131 PASS**, 16 suite.
- `pio run -e attak-iot-firmware`: **SUCCESS**; RAM 85.540 byte, flash 1.415.485 byte.
- `pio run -e attak-iot-firmware -t buildfs`: **SUCCESS**.
- Host smoke dùng `main.cpp`, runtime, codecs, queue, arbiter thật; chỉ hardware
  và transport là fixture. AP smoke dùng `wifi_ap.cpp` thật với WiFi API fixture,
  gồm failed bring-up còn radio on, STA teardown và failed restore sau mode loss.
- Chromium kiểm tra no-catalog/no-actions, params typed/16-byte UTF-8 boundary,
  required empty string/optional presence, Replay gate, stale ticket/duplicate
  sequence, final-result retention, Stop=disable và reconnect; **0 page error**.
  Screenshot desktop và mobile 390px đã quan sát; không horizontal overflow.
- Review blockers đã sửa: encoder test API, strict JSON toàn buffer, suspend
  dựa radio mode thật, borrow credentials; đã bỏ field/cache-echo tests và API
  serializer chỉ dành cho test. Không thêm synthetic production action.
- Tooling không hoàn toàn sạch: clangd còn cascade Xtensa/newlib tại Serial
  `std::string`; compiler thật pass. `containsKey` có deprecation warning.
- **Không dùng kết quả trên để đánh dấu F0 hay hardware AC F4 xanh.**

## 11. Cutover payload software-first (2026-10-05)

Catalog production có **17 action**: giữ `scan`, `read_uid`, `capture` và thêm
14 action dưới đây. Param bounds/defaults, family thẻ, protocol và giới hạn
record được ghi trong [`README.md`](README.md) §Payload trong mã nguồn.
Không copy AGPL Bruce; nguồn protocol/chip tham khảo được ghi trong source.

| Ticket | Đã tích hợp trong source | Phạm vi nghiệm thu phần cứng |
|---|---|---|
| T10 | `rf_scan`, sweep trong một band, ≤128 điểm | RSSI/frequency trên CC1101 còn chờ |
| T11 | `rf_record` → RAM → `rf_replay`, waveform RMT | Capture/replay OOK trên thiết bị được phép còn chờ |
| T12 | `rf_spectrum`, Continuous tier **observe** | Spectrum và shared SPI còn chờ |
| T13 | `rf_custom_tx`, hex OOK 1 kbit/s, phát hữu hạn | Waveform/carrier trên CC1101 còn chờ |
| T20 | `nrf_scan`, RPD, phiên RX mới cho từng probe | Đo RPD/settle và shared SPI còn chờ |
| T30 | `nfc_read_dump`, Classic 1K/4K và Type 2, giữ binary RAM | Thẻ/keys thực tế còn chờ |
| T31 | `nfc_clone_uid`, thẻ magic cùng family/UID length, verify UID | CUID/Gen2 writable-UID thực tế còn chờ |
| T32 | `nfc_write_ndef`, Text UTF-8 Type 2, read-back verify | NDEF trên thẻ được phép còn chờ |
| T33 | `nfc_erase`, không ghi block 0/trailer Classic, verify | Layout/locks/erase trên thẻ thực tế còn chờ |
| T40 | `ir_replay`, raw capture buffer → RMT | IR carrier/timing/receiver thực tế còn chờ |
| T41 | `ir_tvbgone`, sáu mã power-toggle tự viết | TV tương thích còn chờ; không phải universal database |
| T42 | `ir_custom_tx`, NEC/SONY/RC5/SAMSUNG/PANASONIC/RAW | Đo timing/carrier thực tế còn chờ |
| T50 | `wifi_sniff`, ring bounded, stream metadata/prefix | Promiscuous RX, AP restore/reconnect thực tế còn chờ |
| T14 | `rf_jammer` (full+intermittent) — §12 | Carrier/GDO0 thực tế trên CC1101 còn chờ |
| T21 | `nrf_jammer` — §12 | CONT_WAVE/hop thực tế trên NRF24 còn chờ |
| T51 | `wifi_beacon` — §12 | Phát 802.11 thô trên AP thực tế còn chờ |
| T52 | `wifi_deauth` (target+flood) — §12 | Deauth/scan thực tế trên AP thực tế còn chờ |
| T53 | `wifi_evil_portal` — §12 | DNS spoof/captive portal thực tế còn chờ |

### Các quyết định và sửa lỗi cutover

- **T30 dùng Record thay OneShot trong bảng ban đầu**, nhưng vẫn chạy hữu hạn:
  `completeRecord()` giữ dump nhị phân cho T31; không dùng JSON như replay buffer.
  Vì vậy dependency hiện tại là F1,F3.
- RF dùng transport SPI trực tiếp có SO-ready timeout, chờ MARCSTATE RX/TX
  bằng poll, đợi RSSI settle sau RX-ready. Mất chip/RSSI read lỗi không tạo
  sample giả. RF/IR TX dùng RMT bất đồng bộ, buffer tồn tại tới completion;
  cancel dừng burst và cleanup trước nhả ownership. Channel 0 để LED, 1 RF, 2 IR.
- NRF xóa RPD latch bằng CE low → đổi channel → CE high **mỗi probe**,
  kể cả scan một channel; probe liveness khi Continuous vẫn chạy.
- WiFi reset ring trước enable callback; xử lý failure từng stage, generation
  gate và cooperative drain callback. Không busy-wait producer; giữ cleanup
  lease tới khi callback hết rồi mới nhả radio cho AP restore của main.
- NFC clone splice UID/BCC vào manufacturer region **của thẻ đích**. Ghi/erase
  re-select chống tag swap; Classic authenticate dùng bốn byte cuối UID và
  authenticate lại khi verify. PN532 write NAK abort ngay, không ghi tiếp rồi
  dựa read-back để che lỗi. Ghi nhiều unit không atomic, không có rollback trên thẻ.
- Bỏ dependency SmartRC và Adafruit PN532/BusIO đã không còn consumer; không
  sửa third-party để che lỗi. Giữ nguyên pin mapping và central lifecycle.
- Giữ các test behavioral/bounds; bỏ JSON-order/prefix và field-copy snapshots.
  RAW IR cap vẫn 500000 µs: sửa phép cộng sai của test, không tăng cap.

### Bằng chứng cutover cuối cùng

- `pio test -e native`: **219/219 PASS**, **23 suite**, lượt cuối 16,602 s;
  không dùng số 131 của Layer 1 làm bằng chứng payload.
- `pio run -e attak-iot-firmware`: **SUCCESS**, RAM **120620/327680 byte
  (36,8%)**, flash **1461737/3342336 byte (43,7%)**. Không đo heap/stack peak.
- `pio run -e attak-iot-firmware -t buildfs`: **SUCCESS**, dashboard.js/index.html.
- Smoke source module thật, backend fixture: RF **84/84**, IR **86/86**,
  NFC **138/138**; NRF/WiFi **ALL CHECKS PASSED**. Đã exercise async TX/Stop,
  record/replay/disable clear, chip mất, RPD latch/settle, enable-time callback,
  partial WiFi startup failure và concurrent producer cleanup.
- NFC smoke gồm UID4/7, Classic auth sector-local/UID tail, manufacturer/BCC,
  max text 64 byte, dynamic lock, standard control TLV giữ nguyên, reserved
  region từ chối trước ghi, write NAK abort, tag swap và read-back verification.
- Catalog codec emit đủ 17 descriptor hợp lệ. Chromium dùng HTML/JS/catalog
  thật: typed RF params, NRF Stop=disable, text-only generic result/HTML
  injection, `nfc_tag_error`; mobile 390px không overflow, **0 page error**.
  Status/transport là fixture, không phải frame từ board.
- Diagnostics probe NFC module/helpers **0 diagnostic**; probe RF/WiFi còn
  lỗi clangd về member `std::atomic`, một file khác timeout. Compiler Xtensa
  build thật pass. Không suppress diagnostic hoặc bỏ đồng bộ để che tooling.
  Warning `containsKey`/FastLED còn; không tuyên bố tooling hoàn toàn sạch.
- Không flash/commit/push trong lượt payload. Giữ nguyên backup F0. Toàn bộ
  hardware AC vẫn chờ; không lấy host fixture/build làm nghiệm thu chip, và
  không đánh dấu toàn bộ roadmap hoàn tất.

## 12. Cutover disruptive (2026-10-06)

Năm payload D (T14, T21, T51, T52, T53) đã tích hợp phần mềm theo quyết định §2.4.
Mọi thứ nằm sau `#ifdef ENABLE_DISRUPTIVE`: env `attak-iot-firmware` **không**
chứa chúng trong catalog lẫn binary; `attak-iot-firmware-lab` và `env:native` bật cờ.

| Ticket | Đã tích hợp trong source | Hợp đồng / ghi chú |
|---|---|---|
| T14 | `rf_jammer` full/intermittent, stream `rf_jammer` | CC1101 carrier qua PATABLE/GDO0; `jam_plan::DutyGate` cho intermittent; qua `SharedSpi` |
| T21 | `nrf_jammer`, stream `nrf_jammer` | `RF24::startConstCarrier()`, `jam_plan::ChannelHopper` hop `[first,last]`; qua `SharedSpi` |
| T51 | `wifi_beacon`, stream `wifi_beacon` | `esp_wifi_80211_tx(WIFI_IF_AP, ...)`; SSID quay vòng (`wifiAttack::lureSsid`) hoặc cố định |
| T52 | `wifi_deauth` target/flood, stream `wifi_deauth` | frame 26 byte (`wifiAttack::buildDeauth`); flood quét async rồi lần lượt AP (≤24) |
| T53 | `wifi_evil_portal`, stream `evil_portal` | captive portal trên AP: `DNSServer` + handler `web_dashboard` (capture POST bounded); trang từ `/example.html` LittleFS (`storage::readTextFile`, fallback `evilTwin::defaultPage`); probe OS trả 302 + `no-store`; AP tạm **luôn mở (open)** qua `wifiAp::portalCredentials` — không bao giờ dùng lại mật khẩu AP quản trị — và clone `ssid`/`channel` qua `wifiAp::beginPortal`/`endPortal`; POST được giải mã thành `user`/`pass` (`evilTwin::parseFormCredentials`) và dashboard ghi vào nhật ký loại `capture` |

- **Kênh điều khiển**: `src/core/serial_console.*` nhận JSON lệnh như WebSocket,
  dispatch qua `dispatchCommand`, in `command_result`; cho phép Dừng khi AP bị chiếm.
- **Portable + test native**: `core/wifi_attack.*` (beacon/deauth/MAC/SSID),
  `core/evil_twin.*` (probe paths, cap trang, quy tắc SSID/kênh clone, giải mã
  credential form) và `core/jam_plan.*` (hopper/gate); đã thêm vào
  `build_src_filter` env native.
- **Bằng chứng (2026-10-07, sau khi thu thập credential + AP portal mở)**:
  `pio test -e native` **253/253 PASS** (29 suite, native bật cờ);
  `pio run -e attak-iot-firmware-lab` **SUCCESS**; `pio run -e attak-iot-firmware`
  **SUCCESS**; `pio run -e attak-iot-firmware -t buildfs` **SUCCESS** và đóng gói
  `/example.html`; wire id disruptive (kể cả `wifi_evil_portal`) và chuỗi clone
  chỉ có trong binary lab, không có trong binary release.
- **AP portal luôn mở (2026-10-07).** Evil twin là mồi câu: ai ở gần cũng phải
  nối được mà không cần biết mật khẩu. `wifiAp::portalCredentials` xoá mật khẩu
  khoá cứng, kể cả khi clone tên; AP quản trị có mật khẩu sẽ bị **restart** thay vì
  tái sử dụng (`wifiAp::portalNeedsRestart`), còn AP vốn đã mở thì chỉ đổi kênh và
  không rớt client. `endPortal()` khôi phục lại đúng credential gốc — AP quản trị
  **có** mật khẩu trở lại sau khi portal dừng. Đã test native, **chưa** nghiệm thu
  client thật nối không mật khẩu.
- **Evil twin — nghiệm thu board (2026-10-06)**: nạp `uploadfs` + firmware lab
  qua `/dev/ttyACM0` (ESP32-S3 rev 0.2, PSRAM 8 MB, cầu CH343), điều khiển bằng
  Serial console. Đo được: boot sạch, `ap on` → `AP "AttakIoT" up at
  192.168.4.1`; `wifi_evil_portal` + `ssid=VanTot,channel=6` → `AP cloned as
  "VanTot" on channel 6 (operator)`; không truyền `channel` → `on channel 1
  (kept)`; Stop → `AP name restored`. Từ chối đúng: SSID 33 byte, SSID chứa ký
  tự điều khiển (BEL, newline), `channel` 0/14, tham số lạ ⇒ `invalid_params`;
  chạy không tham số ⇒ OK và **không** clone. Không dùng backup flash (người dùng
  bỏ qua — đã có commit).
- **Evil twin — phần client (2026-10-07)**: phần mềm đủ (DNS wildcard `*`,
  probe `302` + `no-store`, `/example.html` từ LittleFS, giải mã credential +
  ghi nhật ký `capture` — đã kiểm bằng native test và dashboard chạy thật),
  nhưng **chưa nghiệm thu client thật**: một thiết bị phải nối vào AP, thấy tên
  twin trong danh sách Wi-Fi, bị captive portal ép ra trang đăng nhập và POST
  được. `netsh wlan` trên Windows cần Location services + admin; điện thoại/laptop
  nối tay là được.
- **Hardware AC còn nợ (khác T53)**: carrier/chip RF, phát 802.11 thô và shadow
  dashboard cũng chưa nghiệm thu board.
- **Chưa làm**: không commit/push; BLE/OTA/battery vẫn ngoài phạm vi.

## 13. Cutover đường quản trị USB NCM (2026-10-06)

Thay đổi kiến trúc điều khiển: **USB NCM là đường quản trị chính**, thay cho mô
hình AP-là-chính của §2.4/F4. Đồng bộ với [`intent.md`](docs/planning/intent.md) §Kiến
trúc, [`map.md`](docs/planning/map.md) §Đường quản trị, [`spec.md`](docs/planning/spec.md)
§USB NCM và [`README.md`](README.md) §Dashboard qua USB NCM.

- **Boot không AP.** `wifiAp::begin()` đặt `WIFI_OFF` tất định; log boot in
  "AP not started (requested off)". Radio WiFi rảnh hoàn toàn cho scan/sniff/payload.
- **USB NCM = admin path.** `usbNetwork::begin()` chạy **trước** `webDashboard::begin()`
  trong `setup()` để dashboard trả lời ngay trên USB. Mạng cục bộ `192.168.7.0/24`,
  firmware cấp IP laptop qua DHCP (`usb_dhcp.*`), **không** quảng bá default
  gateway/DNS (không đổi đường Internet của laptop). HTTP/WS `/ws` phục vụ trên
  `192.168.7.1`. Driver Windows 11 tích hợp `UsbNcm.sys`; chưa cam kết Windows
  10/ECM trước nghiệm thu board. Cổng native (GPIO19 D−/GPIO20 D+), không phải CH343.
- **AP dự phòng theo yêu cầu.** Bật bằng Serial `ap on`, tắt bằng `ap off`;
  không lưu qua reboot; không tự bật khi USB lỗi/rút cáp. `handleApRequest` nhận
  chủ trung tâm trong `main.cpp`: phải disable module WiFi và đợi cleanup/radio
  idle trước khi đổi AP. Beacon/deauth cần `ap on` trước. Riêng `wifi_evil_portal`
  sở hữu AP tạm qua `wifiAp::beginPortal`/`endPortal`: tự bật lúc Start, trả lại
  AP/mode/kênh trước action khi Stop/disable hoặc lỗi; AP boot-off vẫn giữ nguyên.
- **Hệ quả với F4/§2.4.** Dashboard không còn phụ thuộc radio WiFi, nên AP tắt
  (do action exclusive hoặc do payload chiếm) **không** làm mất quyền điều khiển.
  Hard-window 30 s của F4 giữ lại chỉ để **khôi phục radio** cho `wifi_sniff`.
  `serial_console` là kênh điều khiển thứ hai, độc lập cả USB lẫn AP.
- **Code đã tích hợp:** `src/core/usb_network.*`, `src/core/usb_dhcp.*`,
  `src/third_party/tinyusb_ncm/` (NCM device/driver tự viết, không copy AGPL),
  `wifi_ap.*` (boot-off + `ap on/off`), `main.cpp` (thứ tự begin + `handleApRequest`),
  dashboard hiển thị trạng thái `usbUp`.
- **Windows NCM đã nghiệm thu một phần (2026-10-06):** sửa NTB OUT divisor
  1→4 (Code 10), tách MAC host/board và nhận datagram trước NDP theo layout
  Windows. Upload/hash verified; NTB/ARP probe thật pass sau sửa; validator
  ASan/UBSan 7/7. Windows adapter Up; HTTP dashboard/JS 200; WS catalog/status/
  transport_info USB; Chromium trang thật connected, 0 page error. Re-enumeration
  Windows→WSL→Windows vẫn HTTP/WS pass; boot UART xác nhận AP off.
- **Lựa chọn IP của người dùng:** giữ `192.168.7.3/24` static trên Windows,
  không đổi cấu hình sang DHCP. Firmware DHCP giữ nguyên; cấp lease tự động
  Windows chưa nghiệm thu, không lấy thành công static làm bằng chứng DHCP.
- **Hardware AC còn nợ:** DHCP tự động khi người dùng chọn lại; rút/cắm USB
  nhiều lần không replay lệnh; `ap on/off` đúng điều kiện idle; Internet laptop
  giữ đường cũ; kiểm tra không thấy SSID quản trị khi boot. Không thay các AC
  này bằng test native/build/smoke.


## 14. Evil twin — action điều phối `wifi_evil_twin` (2026-10-07)

`main` đã có evil twin tích hợp trong `wifi_evil_portal` (clone AP + deauth đồng
hành + thu credential + trang từ LittleFS). Mục này bổ sung **một action điều
phối riêng, Plan-driven** `wifi_evil_twin` cho người vận hành muốn một preset
gọn, bắt buộc đủ ba mục tiêu (SSID + BSSID + kênh nạn nhân).

- **State machine portable** `evilTwin::Plan` (`core/evil_twin.*`, khuôn theo
  [`jam_plan`], clock injected, wrap-safe): `begin`→CloningAp→`apReady`→Running
  →`stop`/`fail`; `step()` phát `StartTwinAp` một lần rồi `SendDeauth` theo nhịp.
  Validate ssidLen 1..32 + `channelUsable` (1..13), clamp reason/interval.
- **AP clone**: `wifiAp::startTwin/stopTwin/twinActive` là wrapper mỏng trên
  `beginPortal/endPortal` đã kiểm chứng (mở, clone SSID, snapshot/khôi phục AP
  quản trị) — không nhân đôi vòng đời radio.
- **Backend** `wifi_module.cpp` `stepEvilTwin`: StartTwinAp → `startTwin` + DNS
  spoof + `enableEvilPortal(resolvePortalPage())`; SendDeauth → `buildDeauth`
  broadcast tới BSSID nạn nhân trên **chính kênh clone** (không `set_channel`);
  tái dùng `parseFormCredentials` để stream `{kind:"evil_twin",event:capture}`.
- **Catalog**: `ActionId::WifiEvilTwin` (cuối enum) + descriptor `wifi_evil_twin`
  (disruptive, params ssid/bssid/channel bắt buộc, reason/intervalMs tùy chọn),
  tất cả `#ifdef ENABLE_DISRUPTIVE`. Dashboard catalog-driven; log capture nhận
  cả `evil_twin`.
- **Test**: `test/test_evil_twin_plan/` 19 ca cho `Plan` (bounds, pha, nhịp
  deauth wrap-safe). Giữ nguyên `test_evil_twin` của portal.
- **Quan hệ với `wifi_evil_portal`**: hai đường vào song song, có phần trùng
  (người dùng chấp nhận); portal là bộ knob linh hoạt, evil_twin là preset chặt.

**Trạng thái kiểm chứng:** máy phát triển KHÔNG có PlatformIO/trình biên dịch
C++ — lượt này CHƯA chạy `pio test`/`pio run`. Code review tay khớp hợp đồng §3,
tái dùng helper đã test của main. DoD (native xanh + build release/lab + wire id
chỉ trong binary lab + hardware AC) còn nợ, cần chạy ở môi trường build thật.

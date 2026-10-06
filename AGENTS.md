# AGENTS.md — Hướng dẫn cho agent làm việc trong repo này

> Tài liệu định hướng cho AI agent/contributor. Đọc file này + [`PLAN.md`](PLAN.md)
> (§3 hợp đồng kiến trúc) + [`REVIEW.md`](REVIEW.md) trước khi sửa code. Mỗi
> subagent nhận **một ticket** trong PLAN §5, theo hợp đồng §3, rồi review bằng
> REVIEW.md. Tài liệu kế hoạch là nguồn boundary — đổi phạm vi phải cập nhật
> PLAN trước.

## 1. Dự án là gì

Firmware pentest IoT đa năng cho **ESP32-S3-N16R8** (headless, điều khiển qua web
dashboard): sub-GHz RF (CC1101), 2.4GHz (NRF24L01), NFC (PN532 I2C), IR RX/TX.
Dự án **riêng, độc lập** — chỉ *tham khảo* Bruce firmware (AGPL-3.0), **không fork,
không copy mã AGPL**. License repo: GPL-2.0 (do dùng lib RF24). Chi tiết mục tiêu:
[`docs/planning/intent.md`](docs/planning/intent.md).

## 2. Build & test (PlatformIO)

```sh
pio test -e native                        # unit test logic portable (bắt buộc xanh)
pio run  -e attak-iot-firmware            # build release (KHÔNG có payload disruptive)
pio run  -e attak-iot-firmware-lab        # build lab (-DENABLE_DISRUPTIVE: có jammer/deauth/portal)
pio run  -e attak-iot-firmware -t buildfs # đóng gói data/ (dashboard) vào LittleFS
pio run  -t uploadfs && pio run -t upload # nạp LittleFS rồi firmware
pio run  -e i2c-scan -t upload            # chẩn đoán I2C độc lập (GPIO4/5)
```

- `env:native` bật `-DENABLE_DISRUPTIVE` để test cả logic disruptive.
- Thêm logic portable mới ⇒ **phải thêm test native** và vào `build_src_filter`
  của `env:native` trong [`platformio.ini`](platformio.ini).
- Trạng thái xanh gần nhất (tham chiếu, không phải nghiệm thu phần cứng): native
  **257/257** (27 suite, cờ disruptive), build release + lab SUCCESS.

## 3. Bất biến kiến trúc (BẮT BUỘC — rút gọn PLAN §3)

1. **Catalog là nguồn sự thật.** Payload mới = thêm `ActionDescriptor` vào
   [`src/core/action_catalog.cpp`](src/core/action_catalog.cpp) + nhánh dispatch
   trong module. KHÔNG allowlist tay trong parser.
2. **`ActionId` mới chỉ thêm vào cuối enum** ([`src/core/command_types.h`](src/core/command_types.h)).
3. **Vòng đời qua `ModuleRuntime`** ([`src/core/module_runtime.*`](src/core/module_runtime.h));
   KHÔNG tự chế state machine song song, KHÔNG thêm lệnh Stop thứ hai (dùng slot Stop/disable sẵn có).
4. **Không chặn `loop()`.** Thao tác chip có budget/deadline; không busy-wait,
   không `delay()` dài. TX dùng RMT bất đồng bộ (channel 0=LED, 1=RF, 2=IR).
5. **Buffer record = RAM only** ([`record_buffer.h`](src/core/record_buffer.h));
   disable module xóa buffer; không ghi LittleFS trừ khi ticket yêu cầu.
6. **Radio độc quyền & shared SPI qua radio arbiter** ([`radio_arbiter.*`](src/core/radio_arbiter.h)).
   CC1101+NRF24 chung một SPI bus — không bao giờ chạy đồng thời. Module KHÔNG tự tắt AP.
7. **License:** tham khảo Bruce, tự viết; ghi chú file đã đọc trong comment; KHÔNG copy AGPL.
8. **Dashboard DOM an toàn:** dữ liệu từ thiết bị chỉ qua `textContent`/prop `text`,
   KHÔNG `innerHTML` ([`data/dashboard.js`](data/dashboard.js)).
9. **Disruptive bọc `#ifdef ENABLE_DISRUPTIVE`** (cả descriptor lẫn handler); UI
   confirm theo `tier`. Env release KHÔNG được chứa wire id/descriptor disruptive.

## 4. Phần cứng & đường quản trị — KHÔNG đổi nếu không cập nhật PLAN

- **Pin mapping cố định** trong [`include/board_pins.h`](include/board_pins.h):
  CC1101+NRF24 chung SPI; PN532 I2C; IR RX/TX rời; RGB LED GPIO48. Không đổi.
- **Đường quản trị (cutover 2026-10-06, PLAN §13):** **USB NCM** là đường chính —
  dashboard HTTP/WS tại `192.168.7.1`, DHCP tự động, không quảng bá gateway/DNS.
  Cổng **native** (GPIO19 D−/GPIO20 D+), không phải CH343. `usbNetwork::begin()`
  chạy **trước** `webDashboard::begin()` trong `setup()`.
- **AP tắt khi boot.** AP quản trị bật bằng Serial `ap on`, tắt `ap off`; đổi
  AP quản trị yêu cầu module WiFi disable + cleanup/radio idle (`handleApRequest`).
  Beacon/deauth cần `ap on` trước; `wifi_evil_portal` tự bật AP tạm và khôi phục
  mode/kênh trước action khi Stop/disable hoặc lỗi, không đổi intent `ap on/off`.
- **`serial_console`** (115200) nhận đúng JSON lệnh như WebSocket ⇒ kênh điều
  khiển thứ hai, độc lập USB & AP. Giữ được Stop khi AP bị payload chiếm.

## 5. Bản đồ mã nguồn

- `src/core/` — framework dùng chung: catalog, params (`action_params.*`,
  `json_strict.h`), runtime, arbiter, record buffer, codecs JSON, USB NCM
  (`usb_network.*`, `usb_dhcp.*`), serial console, wifi_ap, web_dashboard.
- `src/modules/` — một driver mỗi chip: `wifi_module`, `cc1101_module`,
  `pn532_module`, `nrf24_module`, `ir_module`. Mỗi module phơi nhiều action.
- `src/third_party/tinyusb_ncm/` — NCM device/driver tự viết (không AGPL).
- `src/diagnostics/` — `i2c_scan` độc lập.
- `test/` — một thư mục test native mỗi đơn vị logic.
- `data/` — dashboard tĩnh (index.html, dashboard.js) nạp vào LittleFS.
- `docs/planning/` — intent / map / spec / attack-modules-plan (nguồn boundary).

## 6. Phạm vi & kỷ luật nghiệm thu

- **Trong phạm vi:** toàn bộ payload 5 category (gồm disruptive, chạy trong môi
  trường thử nghiệm được phép). **Ngoài phạm vi:** BLE, WiFi networked tooling của
  Bruce, NFC companion-chip (AGPL), OTA/battery/PCB. Xem PLAN §2.
- **Phần mềm ≠ phần cứng.** Test native/build/smoke bằng transport giả lập **KHÔNG**
  thay nghiệm thu trên board (F0 + action thật). Không đánh dấu hardware AC xanh
  bằng fixture. Báo cáo trung thực: nợ phần cứng phải ghi rõ.
- Không flash/commit/push trong lượt làm payload trừ khi được yêu cầu rõ. Giữ
  nguyên backup flash F0 (`.pio/f0-backup-*`).

## 7. Quy ước commit

Theo lịch sử repo: `feat:/fix:/...` mô tả tiếng Việt, kèm dòng
`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` khi do agent tạo.

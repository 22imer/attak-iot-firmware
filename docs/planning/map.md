Label: wayfinder:map

## Destination

Một spec hoàn chỉnh, sẵn sàng đưa vào `/to-spec` → `/to-tickets` → `/implement`, cho dự án firmware ESP32-S3 độc lập **attak-iot-firmware** (headless, điều khiển qua web dashboard) — v1 = driver skeleton kết nối + hiển thị trạng thái cho CC1101, NRF24, PN532, IR RX/TX trên board **ESP32-S3-N16R8** (16MB flash / 8MB octal PSRAM).

## Notes

Bài tập lớn cá nhân (school capstone), phi thương mại. Domain: ESP32-S3 embedded firmware, tham khảo kiến trúc Bruce Device firmware (`firmware/` trong repo này, AGPL-3.0) nhưng KHÔNG fork.

Standing decisions (đã chốt lúc chart map — không cần mở lại):

- Modules đã mua sẵn (breakout rời): CC1101, NRF24L01, PN532 (I2C/SPI jumper-selectable), IR RX/TX rời (TSOP + LED).
- Main board: **ESP32-S3-N16R8** dev board (16MB flash, 8MB octal PSRAM, dual USB-C) — xác nhận qua ảnh product-listing của board (filename chứa "n16r8...16mb-flash-8mb-psram"). Sửa lần 2: nhận diện ban đầu "YD-ESP32-S3/VCC-GND Studio" (từ `pin_layout.jpeg`) đã bị thay thế — ảnh đó không khớp board thật.
- Dự án riêng tại `/home/duck/school_proj/Attk_IOT/attak-iot-firmware/` (git repo mới, đã dời từ `/mnt/d/ATTAK_IOT` sang WSL native fs để build không bị treo do I/O mount chậm) — không copy code AGPL-3.0 của Bruce, chỉ tham khảo cách làm.
- Framework: PlatformIO + Arduino framework.
- WiFi: AP mode (giống `BruceNet`), không cần STA/captive-portal provisioning cho v1.
- Dashboard: WebSocket, ưu tiên payload/tần suất update nhẹ (đơn giản cho đồ án).
- CC1101 + NRF24 dùng chung 1 SPI bus (pattern `acquireSPIBus` của Bruce).
- PN532: chế độ I2C (không dùng SPI).
- License audit (đã xong): CC1101 lib (SmartRC-CC1101-Driver-Lib) + PN532/BusIO (Adafruit) = permissive (MIT/BSD). IR lib (IRremoteESP8266) = LGPL-2.1. Web stack (ESPAsyncWebServer+AsyncTCP) = LGPL-3.0. NRF24 lib (RF24) = GPL-2.0-only — **chấp nhận**, không né (personal/academic use). KHÔNG dùng các lib companion whywilson (PN532Killer/BLE/UART) — AGPL-3.0, cũng không cần cho v1 (không phải driver chip PN532).
- v1 scope: driver skeleton cho cả 4 module (kết nối được + hiển thị trạng thái qua dashboard). Tính năng pentest cụ thể là phase sau (xem Out of scope).

Skills mỗi ticket session nên gọi: `grilling`, `domain-modeling`.

## Decisions so far

- [Pin mapping ESP32-S3-N16R8](issues/01-pin-mapping.md) — SPI chung SCK12/MOSI11/MISO13; CC1101 CS10/GDO8; NRF24 CS14/CE9; PN532 I2C SDA4/SCL5; IR RX6/TX7; status dùng RGB LED onboard **GPIO48** (sửa từ GPIO47 sau khi xác nhận board thật); né GPIO0/3/45/46 (strap) + 19/20 (USB) + 43/44 (UART flash) + 26/33/34/35/36/37 (có thể bị PSRAM chiếm dụng nội bộ trên module octal).
- [Project scaffold & architecture](issues/02-project-scaffold-architecture.md) — cấu trúc thư mục PlatformIO chuẩn; `ModuleStatus{name,connected,detail,lastUpdateMs}` chung cho 4 module; WebSocket JSON status broadcast; storage LittleFS `/config.json`; không auth. Resolved bằng scaffold thật (`attak-iot-firmware` commit `fa5880a`/`ed881fe`), build pass.
- [Dashboard UI/UX](issues/03-dashboard-ui-ux.md) — 3 variant thử (card grid / sidebar+detail / table+log), verify bằng browser thật; ghép sidebar (chọn module) + detail panel/event log real-time làm bản chính thức. Folded vào `main` (commit `f62ec65`); 3 variant gốc giữ nguyên trên nhánh `prototype/dashboard-ui`.

## Not yet specified

- Nguồn điện & quản lý pin (USB-only hay có pin sạc/battery management trong firmware) — chưa đủ rõ, chờ xác nhận kế hoạch lắp ráp vật lý.
- Cơ chế cập nhật firmware (OTA qua dashboard hay chỉ flash USB) — có thể cần cho demo nhưng chưa đủ sharp để ticket ngay.

## Out of scope

- Thiết kế PCB tùy chỉnh / vỏ máy vật lý — đã chốt dùng module rời + board ESP32-S3-N16R8 có sẵn, không fab PCB.
- Tính năng pentest cụ thể theo từng module (RF sniff/replay, NFC clone, IR replay, NRF24 sniff/jam) — ngoài phạm vi v1 của map này, sẽ là effort/map riêng sau khi v1 ship.

## v2: Attack payloads (grilled inline, không mở map mới)

Ngày sau khi v1 ship dashboard+driver skeleton — user chủ động mở lại phần "Out of scope" ở trên. Quyết định (breadth-first grill inline):

- **Kiến trúc polling đổi**: bỏ auto-poll liên tục (ticket 05-08 cũ viết theo model cũ, cần sửa lại acceptance criteria). Module có 3 trạng thái: `off` (trung tính, chưa kiểm tra) → bật → vòng lặp liên tục (status + payload) chạy trong lúc bật → tắt = dừng hẳn.
- **Category thứ 5**: WiFi/BLE, dùng sóng onboard ESP32-S3 (không phải module rời nào trong 4 module đã mua).
- **License**: giữ nguyên — chỉ tham khảo Bruce, tự viết code, KHÔNG copy. Lý do cụ thể: RF24 (đã dùng, GPL-2.0-only) không tương thích AGPL-3.0 (license Bruce) trong cùng 1 binary — xác nhận qua tra cứu, không phải suy đoán.
- **Payload/category cho đợt này** (1 tính năng cốt lõi/category, không làm hết mọi tính năng Bruce có):
  - RF (CC1101): Record + Replay 1 tín hiệu cố định tần số
  - NRF24: Jammer (quét kênh 2.4GHz + constant carrier)
  - PN532: Đọc UID thẻ
  - IR: Capture + Replay
  - WiFi: Scan SSID xung quanh — KHÔNG làm deauth/evil-portal/promiscuous đợt này, vì Bruce tự teardown+restore AP dashboard trước khi chạy các attack đó (radio cần độc quyền); Scan qua `WiFi.scanNetworks()` không cần teardown (ESP32 Arduino core tự lo AP+STA đồng thời) nên an toàn chạy song song với AP dashboard đang có — xác nhận qua đọc `firmware/src/core/wifi/wifi_common.cpp` + `wifi_atks.cpp` (Bruce serialize dashboard AP vs mọi WiFi attack, gọi `cleanlyStopWebUiForWiFiFeature()` trước mỗi attack).
- **Lưu trữ dữ liệu ghi được** (tín hiệu RF/IR): RAM only, không cần LittleFS — mất khi tắt module, khớp model on/off.
- **Dashboard**: trang/tab mới "Attacks", tái dùng pattern sidebar+detail+log đã build ở v1; mỗi category có nút Start/Stop + khu vực output riêng. WebSocket cần chuyển từ broadcast-only sang 2 chiều (nhận lệnh bật/tắt/replay từ dashboard).
- **Phạm vi**: xây cả 5 category trong đợt này (không làm tuần tự từng cái), nhưng mỗi category chỉ 1 payload cốt lõi.

# ISSUE.md — Kết quả review firmware (2026-10-05)

Phạm vi đã đọc: `src/core/*`, `src/modules/*`, `src/main.cpp`, `platformio.ini`,
`include/board_pins.h`, README, `docs/planning/intent.md`.
Chưa đọc: `data/dashboard.js`, `test/`, `spec.md`, `map.md`, `storage.h`.
Chưa build/chạy test; mọi nhận xét dựa trên đọc mã.

Mức độ: **High** = nên sửa trước khi nghiệm thu / demo, **Medium** = nên sửa sớm, **Low** = dọn dẹp.

---

## High

### 1. `/config.json` bị serve qua web (lộ `apPassword`)
- **Vị trí:** `src/core/web_dashboard.cpp` (`server.serveStatic("/", LittleFS, "/")`), `src/core/storage.cpp` (`kConfigPath = "/config.json"`)
- **Vấn đề:** gốc LittleFS được phục vụ toàn bộ, trong khi config (gồm `apPassword`) nằm ở gốc. Ai nối được vào AP đều tải được `http://<ip>/config.json`.
- **Đề xuất:** đưa dashboard vào thư mục riêng (ví dụ `/www`) và chỉ serve thư mục đó; hoặc đổi đường dẫn config ra ngoài thư mục được serve.
- [ ] Sửa
- [ ] Thêm kiểm tra thủ công: `GET /config.json` phải trả 404

### 2. WebSocket/dashboard không có xác thực
- **Vị trí:** `src/core/web_dashboard.cpp`
- **Vấn đề:** bất kỳ client nào trong AP đều bật/tắt được module. Không kiểm tra `Origin` trên `/ws`, nên một trang web mở trên thiết bị đang nối AP cũng có thể điều khiển thiết bị.
- **Đề xuất:** token đơn giản (gửi ở handshake hoặc frame đầu), và/hoặc kiểm tra `Origin`. Đổi mật khẩu AP mặc định trong `storage.h`, không để mặc định trong repo.
- [ ] Sửa

### 3. Null pointer khi dựng kết quả WiFi scan
- **Vị trí:** `src/modules/wifi_module.cpp`, `buildScanOutput()`
- **Vấn đề:** vòng đầu có kiểm tra `bssid` null, vòng thứ hai gọi `WiFi.BSSID(index)` rồi truyền thẳng vào `snprintf` không kiểm tra → crash nếu trả về null.
- **Đề xuất:** kiểm tra null, bỏ qua hoặc ghi BSSID rỗng.
- [ ] Sửa

---

## Medium

### 4. `softAP` không kiểm tra kết quả, không validate mật khẩu
- **Vị trí:** `src/core/wifi_ap.cpp`
- **Vấn đề:** mật khẩu < 8 ký tự (ví dụ do sửa tay `/config.json`) làm `softAP` thất bại; thiết bị không phát WiFi và không có log lý do.
- **Đề xuất:** kiểm tra giá trị trả về, validate độ dài khi `fromJson`, fallback về cấu hình mặc định và log ra Serial.
- [ ] Sửa

### 5. WiFi scan làm gián đoạn dashboard
- **Vị trí:** `src/modules/wifi_module.cpp`
- **Vấn đề:** thiết bị chạy `WIFI_AP`; `scanNetworks()` bật STA (APSTA) và nhảy kênh, WebSocket có thể đứng/rớt trong lúc quét (tới 15 giây).
- **Đề xuất:** đo trên board thật (cả mốc 15s lẫn việc client có rớt không); UI cảnh báo trước khi quét; cân nhắc quét có giới hạn kênh/thời gian.
- [ ] Đo trên phần cứng
- [ ] Cập nhật UI/spec

### 6. Rủi ro RF24 ghi đè cấu hình SPI
- **Vị trí:** `src/modules/nrf24_module.cpp` (`radio.begin(&spiBus::instance())`), `src/modules/spi_bus.cpp`
- **Vấn đề:** tùy phiên bản core/RF24, `begin()` có thể gọi lại `SPI.begin()` với pin mặc định. Trên ESP32-S3 pin mặc định (SCK 36 / MOSI 35 / MISO 37) trùng vùng PSRAM octal của N16R8.
- **Đề xuất:** xác nhận khi bring-up phần cứng (issue 05/06); nếu bị ghi đè thì truyền pin tường minh hoặc khởi tạo SPI sau `radio.begin`.
- [ ] Xác nhận trên board

### 7. Dependency chưa ghim phiên bản
- **Vị trí:** `platformio.ini` (`lib_deps`)
- **Vấn đề:** `ArduinoJson`, `ESPAsyncWebServer`, `AsyncTCP` không có version; hai lib `bmorcelli/...` lấy từ git URL không ghim commit. Code dùng API `JsonDocument` nên cần ArduinoJson v7. Build có thể vỡ khi upstream đổi.
- **Đề xuất:** ghim `bblanchon/ArduinoJson @ ^7`, ghim commit/tag cho các URL git, ghim version cho ESPAsyncWebServer/AsyncTCP.
- [ ] Sửa

### 8. Partition table chưa khai báo
- **Vị trí:** `platformio.ini`
- **Vấn đề:** đặt `flash_size = 16MB` nhưng không có `board_build.partitions`, có thể đang dùng bảng phân vùng mặc định nhỏ (LittleFS bị giới hạn).
- **Đề xuất:** thêm file partition cho 16MB, dành vùng LittleFS đủ lớn.
- [ ] Sửa

---

## Low

### 9. PN532: gọi `probeFirmware` thừa khi bật module
- **Vị trí:** `src/modules/pn532_module.cpp` — `initChip()` đã gọi `probeFirmware`, `setEnabled()` gọi thêm lần nữa.
- [ ] Gộp lại

### 10. PN532: vòng chờ I2C không nhường CPU
- **Vị trí:** `runCommand()` — hai vòng `while` đọc liên tục, không `yield`.
- **Đề xuất:** thêm `delay(1)`/`yield()` giữa các lần đọc.
- [ ] Sửa

### 11. `loop()` chạy không nghỉ
- **Vị trí:** `src/main.cpp`
- **Đề xuất:** thêm `delay(1)` cuối `loop()` để giảm tải CPU.
- [ ] Sửa

### 12. `handleData` khóa mutex hai lần liên tiếp
- **Vị trí:** `src/core/web_dashboard.cpp` — lấy `sessionToken` rồi `queue_.push` ở hai khối khóa riêng.
- **Đề xuất:** gộp thành một khối khóa.
- [ ] Sửa

### 13. `storage::load` đọc từng ký tự, không giới hạn kích thước
- **Vị trí:** `src/core/storage.cpp`
- **Đề xuất:** giới hạn kích thước file (ví dụ 1–2 KB) và đọc theo khối.
- [ ] Sửa

### 14. Lib CC1101 trong `lib_deps` hiện chưa dùng
- **Vị trí:** `platformio.ini` (`SmartRC-CC1101-Driver-Lib`) — module CC1101 hiện dùng SPI thô. Giữ nếu sắp dùng cho RF replay, bỏ nếu không.
- [ ] Quyết định

### 15. Chưa có CI
- **Đề xuất:** GitHub Actions chạy `pio test -e native` và `pio run -e attak-iot-firmware` trên mỗi push/PR.
- [ ] Thêm

### 16. README thiếu disclaimer pháp lý
- **Vấn đề:** README nêu mục tiêu sniff/replay RF, clone NFC, jam 2.4GHz. Jamming bị cấm ở hầu hết quốc gia.
- **Đề xuất:** thêm mục "Phạm vi sử dụng": chỉ dùng trên thiết bị/mạng của mình hoặc khi có cho phép bằng văn bản; ghi rõ jammer/deauth ngoài phạm vi đợt hiện tại.
- [ ] Thêm

---

## Điểm tốt (giữ nguyên)

- `ModuleRuntime`: state machine thuần, ticket/epoch chặn kết quả trễ, deadline an toàn khi `millis()` tràn, `cleanupPending` chỉ xóa khi phần cứng đã nhả tài nguyên.
- `CommandQueue`: slot Stop riêng theo module, không bị lệnh thường chặn.
- Tách logic thuần khỏi code ESP32 để chạy `native` test; driver có budget thời gian giới hạn.
- README nêu rõ chưa nghiệm thu phần cứng.

## Việc chưa review

- `data/dashboard.js`, `data/index.html`
- Toàn bộ `test/` (50 test native)
- `docs/planning/spec.md`, `map.md`, kế hoạch superpowers
- `src/core/storage.h` (mật khẩu AP mặc định)

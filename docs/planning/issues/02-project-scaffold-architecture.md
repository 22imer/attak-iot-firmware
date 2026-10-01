Type: grilling
Status: resolved

## Question

Kiến trúc project & scaffold cho `attak-iot-firmware`:

- Cấu trúc thư mục PlatformIO project.
- Lớp abstraction chung cho "trạng thái module" (connected/disconnected/health) để dashboard hiển thị đồng nhất cho cả 4 module (CC1101, NRF24, PN532, IR).
- Schema message WebSocket (JSON) cho cập nhật trạng thái real-time.
- Cơ chế lưu trữ cấu hình bền vững (NVS hay LittleFS) cho AP credentials.
- Dashboard có cần auth (username/password) hay để mở (phù hợp bài tập lớn, không public-facing)?

## Answer

Resolved implicitly by building the actual scaffold in `attak-iot-firmware/` (commit `fa5880a`, corrected in `ed881fe`):

- **Cấu trúc thư mục**: PlatformIO chuẩn — `include/board_pins.h` (pin mapping), `src/core/` (module_status, storage, wifi_ap, web_dashboard), `src/modules/` (cc1101/nrf24/pn532/ir, mỗi module 1 cặp .h/.cpp), `src/main.cpp`, `data/` (LittleFS dashboard static files).
- **Abstraction chung**: `struct ModuleStatus { name, connected, detail, lastUpdateMs }` (`src/core/module_status.h`), mỗi module export `begin()/poll()/status()` cùng chữ ký qua namespace riêng.
- **WebSocket schema**: `{"module": "...", "connected": bool, "detail": "...", "lastUpdateMs": N}`, broadcast mỗi loop() qua `webDashboard::publishStatus()`.
- **Storage**: LittleFS, `/config.json` (`apSsid`, `apPassword`, `deviceName`), qua `src/core/storage.{h,cpp}`.
- **Auth**: không — dashboard mở, đúng quyết định ban đầu.

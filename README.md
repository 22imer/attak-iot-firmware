# attak-iot-firmware

Firmware độc lập cho một công cụ pentest IoT đa năng: sniff/replay RF, đọc/clone
NFC, capture/replay IR, sniff/jam 2.4GHz — điều khiển hoàn toàn qua web dashboard
(headless, không màn hình vật lý).

Tham khảo kiến trúc của [Bruce Device firmware](https://github.com/BruceDevices/firmware)
nhưng là một dự án riêng, không fork — xem lý do trong
`docs/planning/map.md`.

## Phần cứng

- Board: **ESP32-S3-N16R8** (16MB flash / 8MB octal PSRAM, dual USB-C)
- Module: CC1101 (sub-GHz), NRF24L01 (2.4GHz), PN532 (NFC, I2C mode), IR RX/TX rời
- Pin mapping: [`include/board_pins.h`](include/board_pins.h)

## Trạng thái

**v1**: driver skeleton cho cả 4 module — kết nối được + hiển thị trạng thái qua
dashboard. Chưa có tính năng pentest cụ thể (sniff/replay/clone/jam).

`cc1101`, `nrf24`, `pn532`, `ir` (`src/modules/*_module.cpp`) vẫn trả về
`connected: false, detail: "not implemented"` — phần khởi tạo driver thật
(SPI/I2C/RMT init) là công việc của phase implementation tiếp theo.

**v2 (đang làm)**: 5 category tấn công, mỗi category 1 payload cốt lõi. Đã xong
phần hạ tầng: kênh WebSocket 2 chiều (`src/core/ws_command.h`,
`ws_command_json.cpp`) và category WiFi — `src/modules/wifi_module.cpp` scan SSID
thật qua `WiFi.scanNetworks()`, chạy song song với AP dashboard vì ESP32 Arduino
core giữ AP+STA đồng thời. `ModuleStatus` đã mở rộng thêm `enabled` và `output`.
Quyết định chi tiết: [`docs/planning/map.md`](docs/planning/map.md) § v2.

## Build

```sh
pio run                # build
pio run -t uploadfs     # nạp data/ (dashboard static files) vào LittleFS
pio run -t upload       # nạp firmware
pio device monitor
```

Sau khi nạp, kết nối WiFi vào AP theo `apSsid`/`apPassword` mặc định trong
[`src/core/storage.h`](src/core/storage.h) (có thể đổi qua `/config.json` trên
LittleFS), rồi mở `http://<AP gateway IP>/` để xem dashboard.

## License

GPL-2.0 — bắt buộc vì phụ thuộc [`nrf24/RF24`](https://github.com/nRF24/RF24)
(GPL-2.0-only) cho driver NRF24. Xem [LICENSE](LICENSE) và phần license audit
trong `docs/planning/map.md`.

## Kế hoạch

Xem `docs/planning/intent.md` và `docs/planning/map.md` (wayfinder
map — quyết định kiến trúc, ticket còn mở).

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

**Đợt dashboard/quan sát (spec 2026-10-04)**: đã triển khai trong mã nguồn.

- 5 category `cc1101`, `nrf24`, `pn532`, `ir`, `wifi`: bật/tắt thật với health
  check/liveness (CC1101 + NRF24 dùng chung một SPI bus, PN532 I2C, IR RX).
- Lệnh 2 chiều qua WebSocket `/ws`: parser strict + hàng đợi FIFO 13 (8 lệnh
  thường + 5 slot Stop riêng theo category), ack trả đúng client/session.
- Hành động: WiFi scan một lượt trả tối đa 32 AP (SSID/BSSID/RSSI/kênh/bảo mật),
  PN532 đọc một UID (4/7/10 byte) trong 5 giây, IR capture một thông điệp không
  repeat trong 10 giây (raw timing nếu UNKNOWN).
- `actionState`/`actionError`/`cleanupPending`/`resultSequence`/`resultUpdateMs`
  được publish để UI hiển thị kết quả cũ, hủy và cleanup.
- Dashboard `data/index.html` + `data/dashboard.js`: reconnect/backoff, stale,
  DOM an toàn (textContent), log giới hạn 200, xuất log/kết quả riêng, xem được
  trên điện thoại 360px.
- RGB (GPIO48) báo aggregate health chỉ theo category đang bật.

**Chưa nghiệm thu phần cứng.** Các mốc WiFi 15 giây và liveness chip 2 giây là
mục tiêu cần đo trên board thật (cần ESP32-S3-N16R8, 2 thẻ NFC khác UID, remote
IR, AP thử có BSSID phân biệt, cổng Serial). Xem bảng AC trong
[`docs/planning/spec.md`](docs/planning/spec.md) §10.

Ngoài phạm vi đợt này: RF replay, IR replay, jammer/deauth, clone/ghi NFC, BLE,
OTA, battery.

## Kiểm thử

```sh
pio test -e native                  # 50 test logic thuần (không cần board)
pio run -e attak-iot-firmware       # build firmware
```

## Vận hành

```sh
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

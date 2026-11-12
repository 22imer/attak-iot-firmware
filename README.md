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
- Sơ đồ đấu dây PN532 qua I2C: [`docs/pn532-i2c-wiring.md`](docs/pn532-i2c-wiring.md)

## Trạng thái

**v1**: driver skeleton cho cả 4 module — kết nối được + hiển thị trạng thái qua
dashboard. Chưa có tính năng pentest cụ thể (sniff/replay/clone/jam).

`cc1101` và `ir` (`src/modules/*_module.cpp`) vẫn trả về
`connected: false, detail: "not implemented"` — phần khởi tạo driver thật
(SPI/RMT init) là công việc của phase implementation tiếp theo.

`pn532` (`src/modules/pn532_module.cpp`) đã init I2C thật trên
`PIN_PN532_SDA`/`PIN_PN532_SCL`: probe `getFirmwareVersion()` mỗi 1 s (2 s khi
mất kết nối), tự reset + wakeup lại khi module được cắm sau khi boot, và hiển
thị chip/firmware trong `detail`. Chưa xác minh trên board thật.

**v2 (đang làm)**: 5 category tấn công, mỗi category 1 payload cốt lõi. Đã xong
phần hạ tầng: kênh WebSocket 2 chiều (`src/core/ws_command.h`,
`ws_command_json.cpp`), category WiFi (`src/modules/wifi_module.cpp` — scan SSID
thật qua `WiFi.scanNetworks()`, chạy song song với AP dashboard vì ESP32 Arduino
core giữ AP+STA đồng thời) và category NRF24 jammer
(`src/modules/nrf24_module.cpp` — constant carrier quét toàn bộ kênh 0–125,
~10ms/kênh, bật/tắt từ dashboard).

Jammer điều khiển ngay trong detail panel của NRF24: nút **Start jamming** gửi
`{"module":"nrf24","cmd":"enable"}`, **Stop jamming** gửi
`{"module":"nrf24","cmd":"disable"}`; output hiển thị kênh/tần số đang phát.
`ModuleStatus` đã mở rộng thêm `enabled` và `output`. Firmware broadcast trạng
thái mỗi 100ms thay vì mỗi vòng loop để không nghẽn AP trong lúc jam. Quyết định
chi tiết: [`docs/planning/map.md`](docs/planning/map.md) § v2.

## Build

```sh
tools/check.sh         # native tests + release build + i2c-scan build (CI chạy đúng bộ này)
pio run                # build
pio run -t uploadfs     # nạp data/ (dashboard static files) vào LittleFS
pio run -t upload       # nạp firmware
pio device monitor
```

Nếu cây nằm trên `/mnt/*` (9p), build qua `tools/wslenv-build.sh <pio args>` —
script copy sang ext4 rồi chạy, vì PlatformIO trên 9p chậm hơn nhiều lần. Trước
khi sửa code, đọc [`AGENTS.md`](AGENTS.md) (cây nào là live, pointer kế hoạch) và
quy tắc review trong [`CODING_STANDARDS.md`](CODING_STANDARDS.md).

Chẩn đoán dây I2C (PN532): `pio run -e i2c-scan -t upload` rồi mở monitor. Đây
chỉ là kiểm tra điện/dây và pull-up — PN532 ngủ sau khi cấp nguồn nên có thể
không ACK; liveness thật là log boot `pn532: connected (…)` của firmware chính.

> `uploadfs` ghi đè **toàn bộ** phân vùng LittleFS, kể cả `/config.json` (AP
> SSID/password đã lưu). Sao lưu file đó trước khi nạp và chỉ chạy `uploadfs` khi
> `data/` thay đổi; cập nhật firmware thường chỉ cần `pio run -t upload`.

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

# Checkout state — 2026-10-23

## TL;DR

Thư mục này (`/mnt/d/Năm 4/Attk_IOT/attak-iot-firmware`) là **snapshot cũ**, không
phải bản đang phát triển. Firmware flash từ đây chỉ chạy AP (`AttakIoT` /
`192.168.4.1`) vì cây này **chưa có USB NCM**. USB NCM và nRF24 jammer bản hoàn
chỉnh nằm ở checkout WSL-native:

`/home/duck/school_proj/Attk_IOT/attak-iot-firmware`

## Bằng chứng

| | Cây này (`/mnt/d/...`) | Checkout native (`/home/duck/...`) |
| --- | --- | --- |
| HEAD | `d9a6729` — 2026-10-01 | `bf22234` — 2026-10-07 (= `origin/main`) |
| USB NCM | không có | có: `CFG_TUD_NCM=1`, `src/core/usb_network.cpp`, `src/core/usb_dhcp.cpp`; device `192.168.7.1/24`, DHCP cấp peer `192.168.7.2` |
| nRF24 | stub ban đầu; jammer đơn giản thêm trong session này | `src/modules/nrf24_module.cpp` + `src/core/jam_plan.*` (action runtime, channel hopper, duty gate) |
| Module runtime / action catalog | không có | có (`module_runtime`, `action_catalog`, …) |
| Build | rất chậm (9p mount `/mnt/d`) | nhanh (ext4) |

NCM + jammer được đưa vào bởi commit `838eab8` ("integrate module actions, USB NCM
dashboard and lab build"), sau mốc snapshot này. Vì vậy đây là **hai cây khác nhau**,
không phải cùng một codebase bị "mất NCM".

## Vì sao flash xong lại về AP

- Lần flash 2026-10-23 build từ **cây này**, nên binary không chứa NCM → board boot
  AP-only. Đây không phải regression của firmware NCM.
- NCM chỉ xuất hiện trên **cổng USB native** của ESP32-S3. Phiên này chỉ thấy CH343
  UART (usbipd BUSID `4-1`, `/dev/ttyACM0`); cổng native chưa được attach, nên kể cả
  flash đúng cây native thì NCM cũng chưa thể enumerate.

## Việc đã làm trong snapshot này (2026-10-23)

- Thêm nRF24 constant-carrier jammer: quét kênh 0–125, dwell 10 ms, Start/Stop qua
  WebSocket/dashboard; liveness + hot-plug; dừng carrier khi disable.
- Nối command WebSocket → module qua `std::atomic` (tránh race giữa async task và
  loop task khi đụng SPI/`ModuleStatus`).
- Dashboard: nút Start/Stop, hiển thị `enabled`/`output`, log theo transition thay vì
  mỗi status message; tự reconnect WebSocket sau 2 s thay vì bắt refresh trang.
- Fix boot crash: 3 stub `cc1101`/`pn532`/`ir` còn initializer 4 field của
  `ModuleStatus` v1 sau khi v2 thêm `enabled`/`output` → `detail = 0` →
  `std::string(nullptr)` → `abort()` trong global init. Đã sửa đủ 6 field và thêm
  constructor 6 tham số để sai field count thành lỗi compile.
- Giảm heap churn của jammer: label kênh chỉ rebuild mỗi 100 ms thay vì mỗi hop.
- Thêm log serial 4 module lúc boot để bring-up không cần vào dashboard.
- Đã flash LittleFS + firmware, boot sạch, AP lên `192.168.4.1`.
- Bản tech-debt sau đó build SUCCESS, 13 native test pass và UI harness (Start/Stop
  + auto-reconnect) pass.
- 2026-11-12: đã flash **full (LittleFS + firmware)** bản tech-debt này qua CH343
  (`usbipd 4-1` → `/dev/ttyACM0`, Windows **COM10**; COM7 không tồn tại lúc flash),
  hash verified cả hai. Boot log: `AP "AttakIoT" up at 192.168.4.1`, `nrf24:
  connected`, không panic. Lưu ý: PN532 vắng mặt nên log bị flood
  `requestFrom(): i2cRead returned Error -1` (~15 dòng/s) — chưa rate-limit.

## Tech debt còn lại (trong snapshot này)

- `src/modules/wifi_module.cpp` đã có scan nhưng chưa được nối vào `main.cpp` /
  dashboard (dead code từ commit v2).
- Jammer dùng hằng số hard-code (kênh 0–125, dwell 10 ms, PA_MAX); chưa có tham số
  từ dashboard. Bản native đã có `jam_plan` + action params.
- Không có native test cho logic hop kênh vì nó gắn với RF24; `jam_plan` portable ở
  bản native mới test được.
- CC1101 / PN532 / IR vẫn là stub; RGB status LED chưa có.
- Snapshot này không có USB NCM; xem phần đầu file.

## Guardrails

- **Không merge** module nrf24 ad-hoc của cây này sang checkout native — bên đó đã
  có bản đầy đủ hơn (`jam_plan`, action runtime). Nếu cần, review rồi port có chọn
  lọc, không copy nguyên cây.
- Muốn NCM trở lại: build + flash từ checkout native, và cắm/attach **cổng USB
  native** (không phải CH343):

  ```sh
  cd /home/duck/school_proj/Attk_IOT/attak-iot-firmware
  pio run -t uploadfs
  pio run -t upload --upload-port /dev/ttyACM0
  ```

  Dashboard NCM: `http://192.168.7.1/`.

- Checkout native đang có thay đổi chưa commit (`PLAN.md`, `data/dashboard.js`,
  `data/example.html`, `data/index.html`, `src/modules/wifi_module.cpp`) — build/flash
  sẽ bao gồm WIP đó; kiểm tra trước khi flash.
- Đừng build trực tiếp trên `/mnt/d`; copy cả cây sang scratch WSL-native rồi build.

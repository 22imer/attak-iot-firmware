# NCM-AP.md — Đường quản trị USB NCM và AP của thiết bị

> Ghi lại cấu hình đường quản trị, cách flash/điều khiển thật đã chạy, kết quả
> nghiệm thu `wifi_evil_portal` (evil twin) trên board, và phần AC còn nợ.
> Nguồn boundary vẫn là [`PLAN.md`](PLAN.md) §13; tài liệu này là nhật ký thực
> thi + hướng dẫn vận hành.

## 1. Vì sao có hai đường: NCM và AP

| Đường | Kết nối vật lý | Địa chỉ | Vai trò | Khi nào dùng |
|---|---|---|---|---|
| **USB NCM** | cổng USB-C **native** (GPIO19 D− / GPIO20 D+) | `192.168.7.1` | **Đường quản trị chính**: dashboard HTTP + WebSocket, `usbNetwork::begin()` chạy **trước** `webDashboard::begin()` | Mặc định |
| **AP Wi-Fi** | onboard radio, bật bằng `ap on` hoặc tự bật tạm cho `wifi_evil_portal` | `192.168.4.1` | Payload WiFi phát beacon/deauth, và captive portal `wifi_evil_portal` | Khi chạy payload WiFi |

Hai đường là **hai netif khác nhau** trên cùng một board. Ranh giới quan trọng:

- AP **mặc định tắt lúc boot**. AP quản trị bật bằng `ap on` qua Serial.
  `wifi_evil_portal` tự bật AP tạm khi Start, nên dùng trực tiếp qua USB NCM
  mà không cần `ap on`. Stop/disable/lỗi khởi tạo khôi phục mode/kênh trước
  action, không thay đổi ý định bật/tắt AP quản trị.
- `handleApRequest` trong `main.cpp` chỉ nhận `ap on`/`ap off` khi module WiFi
  idle và không có lease radio nào → không bao giờ đụng action đang chạy.
- **NCM không bao giờ bị payload WiFi chiếm.** `PortalHandler::onApInterface()`
  (`src/core/web_dashboard.cpp`) chỉ bắt request có `socket->localIP() ==
  WiFi.softAPIP()`. Request đến qua NCM đi thẳng tới dashboard thật. Nhờ vậy
  operator vẫn Stop được payload dù AP đang bị portal che.
- Vì vậy **NCM không phải client của captive portal**: dùng NCM để *điều khiển
  và xem log*, còn *kiểm chứng client* thật thì phải có một thiết bị nối vào AP.

### Thay đổi vòng đời AP (2026-10-07)

- API hiện tại: `wifiAp::beginPortal(ssid, channel)` / `endPortal()`, thay API
  clone-only cũ. SSID rỗng dùng tên đã cấu hình; kênh 0 giữ kênh radio, hoặc
  chọn kênh 1 nếu radio đang tắt. Mật khẩu cấu hình được giữ nguyên.
- Host regression: `python3 tools/test_wifi_ap_lifecycle.py` chạy backend thật
  với radio double; 7 ca PASS gồm AP-off/STA, AP đã bật, channel-only, lỗi
  bring-up, tham số không hợp lệ, Start lồng và Stop lặp lại.
- Smoke dùng `wifi_module.cpp` + `ModuleRuntime` thật, double phần cứng/DNS/HTTP:
  4 ca PASS (Start từ STA qua điều khiển USB; rollback lỗi DNS; giữ AP quản trị
  sau disable; portal không tham số). Không phát RF hoặc đo transport USB thật.
- `pio test -e native`: 248/248 PASS; build release và lab SUCCESS.
- Đã flash bản lab sửa AP qua CH343 `/dev/ttyACM0` (Windows COM10),
  `pio run -e attak-iot-firmware-lab -t upload --upload-port /dev/ttyACM0`:
  SUCCESS, ghi firmware 1.540.400 byte và hash verified; giữ nguyên LittleFS.
  Boot xác nhận AP-off, 5 module off, không thấy panic trong 8 giây; lệnh
  Serial `wifi disable` id=902 trả `ok:true`. Đã trả CH343 về Windows sau flash.
- Chưa nghiệm thu action Evil Twin hoặc dashboard USB trên revision mới:
  chỉ CH343 được kết nối; log boot không có dòng `USB NCM ready`.
  Kết quả §5 là của revision trước, không suy ra RF/USB thật từ host smoke.

## 2. Board đã dùng để nghiệm thu

```
Chip       ESP32-S3 rev 0.2 (QFN56), PSRAM 8 MB (AP_3v3), crystal 40 MHz
MAC        90:70:69:f7:d7:90
Cổng flash /dev/ttyACM0 — USB VID:PID 1A86:55D3 (CH343), 622,5 kbit/s ghi
Chú ý      Đây là cổng UART phụ. Cổng native (NCM) là cổng USB-C còn lại.
```

## 3. Flash (đã chạy thật)

```sh
cd attak-iot-firmware
PORT=/dev/ttyACM0 pio run -e attak-iot-firmware-lab -t uploadfs   # LittleFS (chứa /example.html)
PORT=/dev/ttyACM0 pio run -e attak-iot-firmware-lab -t upload    # firmware, hash verified
```

- `esptool.py` nằm ở `~/.platformio/packages/tool-esptoolpy/esptool.py`, phải gọi
  bằng `python3` (file không có quyền exec).
- **Backup flash không chạy được**: đọc 16 MB hỏng "Corrupt data" ở ~1% cả ở
  921600 lẫn 460800 → cầu CH343 trong WSL không ổn định. Người dùng chốt bỏ
  backup (đã có commit) nên `read_flash` bị bỏ qua.
- Firmware nạp vào là **bản lab** (`-DENABLE_DISRUPTIVE`). Bản release không
  chứa wire id `wifi_evil_portal`.

## 4. Điều khiển qua Serial (đường điều khiển thứ hai)

Serial 115200 nhận đúng JSON lệnh như WebSocket, nên dùng được ngay cả khi AP
đã bị portal chiếm.

```sh
python3 - <<'PY'
import serial, time, json
s = serial.Serial("/dev/ttyACM0", 115200, timeout=0.15)
s.dtr = False; s.rts = True; time.sleep(0.15); s.rts = False   # reset
time.sleep(5)                                                   # chờ boot
s.write((json.dumps({"id": 1, "module": "wifi", "cmd": "enable"}) + "\n").encode())
s.write((json.dumps({"id": 2, "module": "wifi", "cmd": "action",
                     "action": "wifi_evil_portal",
                     "params": {"ssid": "BanMi", "channel": 6}}) + "\n").encode())
time.sleep(2); print(s.read(4096).decode("utf-8", "replace"))
s.write((json.dumps({"id": 3, "module": "wifi", "cmd": "disable"}) + "\n").encode())
PY
```

Lệnh dùng nhiều:

| Mục đích | Lệnh |
|---|---|
| Bật/tắt AP | `ap on` / `ap off` (chỉ khi module idle) |
| Bật module | `{"module":"wifi","cmd":"enable"}` |
| Chạy evil portal | `{"module":"wifi","cmd":"action","action":"wifi_evil_portal","params":{"ssid":"BanMi","channel":6}}` |
| Dừng payload | `{"module":"wifi","cmd":"disable"}` |

## 5. Kết quả nghiệm thu evil twin trên board (2026-10-06)

| Ca | Kết quả quan sát |
|---|---|
| Boot | Sạch, 4 module `ok=0`, `wifiAp: AP not started (requested off)` |
| `ap on` | `wifiAp: AP "AttakIoT" up at 192.168.4.1` + `ap on: ok` |
| `ssid=VanTot, channel=6` | `wifiAp: AP cloned as "VanTot" on channel 6 (operator)` |
| `ssid=BanMi` (không channel) | `... on channel 1 (kept)` — giữ kênh của AP |
| Stop (`disable`) | `wifiAp: AP name restored` |
| `ssid` 33 byte | `invalid_params` |
| `ssid` chứa BEL / newline | `invalid_params` |
| `channel` 0 hoặc 14 | `invalid_params` |
| tham số lạ (`{"x":1}`) | `invalid_params` |
| không tham số | `ok:true`, **không** clone, portal vẫn chạy |

**Lỗi phát hiện khi nghiệm thu và đã sửa:** log in `(channel set)` ngay cả khi
operator không truyền `channel`, và kênh hiệu dụng được xác định *sau* khi
restart AP (`softAP()` mới luôn về kênh mặc định 1). Đã sửa trong
`wifiAp::beginClone()`: đọc kênh hiện tại **trước** `WiFi.mode(WIFI_OFF)`, đặt
kênh đó sau khi dựng lại, và log phân biệt `(operator)` / `(kept)`. Sửa lại xong
đã flash và đo lại trên board (bảng trên).

## 6. Bật USB NCM trên WSL (bước phía Windows)

Board đang cắm cổng CH343 nên WSL **chưa** thấy interface NCM (chỉ có `eth0`).
Cần:

1. Rút cáp, cắm vào **cổng USB-C native** (cổng còn lại).
2. PowerShell: `usbipd list` → lấy BUSID của ESP32-S3.
3. PowerShell **Administrator**: `usbipd bind --busid <BUSID>` rồi
   `usbipd attach --wsl --busid <BUSID>`.
4. Trong WSL phải xuất hiện interface mới (thường `eth1`) và có DHCP từ thiết
   bị; dashboard ở `http://192.168.7.1`.

Sau khi NCM lên:
- `ip -br addr` thấy interface mới; ping `192.168.7.1`.
- Mở dashboard, kiểm tra frame `catalog` có `wifi_evil_portal` với 2 tham số
  (`ssid`, `channel`).
- Chạy `wifi_evil_portal` ngay trên dashboard: chỉnh tham số không bắt buộc,
  tier `disruptive` phải có hộp xác nhận.
- Mọi POST credential sẽ hiện trong khung log của dashboard (frame
  `evil_portal`), không cần Serial nữa.

## 7. Kiểm chứng phía client captive portal (còn nợ)

Cần **một thiết bị client nối vào AP** — NCM không thay được vai trò này.

Cách nhanh nhất: điện thoại/laptop nối tay vào Wi-Fi `BanMi` (portal đang chạy,
kênh 6, `192.168.4.1`, mật khẩu theo `/config.json` — mặc định `attakiot123`).

Cần quan sát và ghi lại:

1. Trình duyệt tự bật trang đăng nhập không (probe `302` + `no-store`).
2. Trang nạp đúng nội dung `/example.html` (tiêu đề "Đăng nhập Wi-Fi"), không
   phải trang mặc định tối giản.
3. Nhập một cặp email/mật khẩu bất kỳ → bấm "Kết nối" → dashboard (qua NCM
   hoặc Serial) nhận đúng body POST đó.
4. Danh sách Wi-Fi của client thấy tên `BanMi` (twin) thay vì `AttakIoT`.
5. Khi `disable`, AP trở lại tên `AttakIoT`.

Nếu không muốn dùng điện thoại: bật *Location services* trong Windows rồi mở
PowerShell **Administrator** để `netsh wlan` điều khiển được WLAN — cả hai bước
đều cần quyền người dùng, agent không tự làm được.

## 8. Danh sách AC còn nợ

| AC | Trạng thái | Ghi chú |
|---|---|---|
| Clone SSID + kênh, khôi phục khi Stop | **xong (board)** | §5 |
| Validate tham số trên board | **xong (board)** | §5 |
| Flash firmware + LittleFS | **xong** | §3 |
| DNS wildcard (DNSServer `*` → AP IP) | chưa | cần client |
| Probe `302` + `Cache-Control: no-store` | chưa | cần client |
| Phục vụ `/example.html` từ LittleFS (fallback) | chưa | cần client |
| POST credential lên log dashboard | chưa | cần client |
| Client thấy tên twin trong danh sách Wi-Fi | chưa | cần client |
| Dashboard qua USB NCM | chưa | chờ bước usbipd §6 |
| Carrier/chip RF, phát 802.11 thô, shadow dashboard | chưa | ngoài phạm vi đợt này |

# attak-iot-firmware

Firmware độc lập cho một công cụ kiểm thử IoT: quan sát/record/replay RF,
đọc/ghi NFC, capture/replay IR và quan sát 2.4 GHz — điều khiển qua web dashboard
(headless, không màn hình vật lý). Chỉ phát hoặc ghi trên thiết bị được phép thử.

Tham khảo kiến trúc của [Bruce Device firmware](https://github.com/BruceDevices/firmware)
nhưng là một dự án riêng, không fork — xem lý do trong
`docs/planning/map.md`.

## Phần cứng

- Board: **ESP32-S3-N16R8** (16MB flash / 8MB octal PSRAM, dual USB-C)
- Module: CC1101 (sub-GHz), NRF24L01 (2.4GHz), PN532 (NFC, I2C mode), IR RX/TX rời
- Pin mapping: [`include/board_pins.h`](include/board_pins.h)

Tài liệu vận hành đường quản trị (USB NCM vs AP), cách flash/điều khiển thật
đã chạy và kết quả nghiệm thu evil twin trên board:
[`NCM-AP.md`](NCM-AP.md).

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
- RGB (GPIO48) sáng khoảng 10% (`26/255`), báo aggregate health chỉ theo category đang bật.

**Layer 1 / Phase 1 F1–F4: đã tích hợp và kiểm chứng phần mềm, hardware AC còn chờ.**

- F1: schema params từ catalog; tối đa 8 giá trị, string tối đa 64 byte UTF-8,
  dữ liệu được sở hữu trong queue. JSON phải hợp lệ toàn bộ; reject sai kiểu,
  ngoài biên, key lạ, null và NUL trong string. `required` yêu cầu có giá trị;
  string rỗng vẫn hợp lệ nếu schema không có ràng buộc khác.
- F2: Continuous stream qua `action_output`, tối đa 1024 byte/sample và cadence
  tối thiểu 100 ms; ticket/sequence chống frame cũ. Stream không thay kết quả
  thành công trước đó. Dừng vẫn gửi `disable`, dùng slot Stop riêng hiện có.
- F3: mỗi runtime giữ một record nhị phân tối đa 4096 byte RAM; replace atomic,
  `needsBuffer` trả `buffer_empty` khi rỗng, disable xóa buffer. IR capture lưu
  timing microsecond và value 64-bit lossless; RF và IR replay đọc buffer nhị phân.
- F4: serialize action CC1101/NRF24 và hoãn poll của radio khác trên SPI; lease
  giữ tới khi cleanup thật. WiFi-exclusive có grace 250 ms, hard window 30 s;
  hết hạn firmware disable owner, đợi cleanup rồi restore AP **nếu đã yêu cầu**,
  retry mỗi 1 s. Dashboard qua USB vẫn nhận lệnh Dừng khi AP vắng; chỉ client
  trên AP bị ngắt và phải reconnect.
  exclusive handler chỉ prime runtime trong dispatch, I/O phải ở poll sau suspend.
- Dashboard tạo form theo catalog, kiểm tra biên byte UTF-8, khóa Replay khi
  buffer rỗng; không có action fallback hard-coded. Catalog production có 17
  action: ba action cũ và 14 action mới trong bảng dưới. Kết quả mới có `kind`,
  được render bằng text, không chèn HTML từ thiết bị.

### Payload trong mã nguồn

`scan`, `read_uid`, `capture` giữ nguyên wire ID. Tham số dưới đây chỉ bắt buộc
khi có dấu `*`; những trường còn lại có thể bỏ để dùng mặc định.

| Module | Action | Kind | Params |
|---|---|---|---|
| CC1101 | `rf_scan` | OneShot | `startMhz*`, `endMhz*`, `stepKhz*` |
| CC1101 | `rf_record` | Record | `freqMhz` (433.92), `windowMs` (2000) |
| CC1101 | `rf_replay` | Replay | `repeat` (1), `gapMs` (200); cần RF record |
| CC1101 | `rf_spectrum` | Continuous | `startMhz` (433), `endMhz` (434), `stepKhz` (25) |
| CC1101 | `rf_custom_tx` | OneShot | `freqMhz*`, `payloadHex*`, `repeat` (1) |
| NRF24 | `nrf_scan` | Continuous | `startChannel`, `endChannel`, `dwellUs` (300) |
| PN532 | `nfc_read_dump` | Record | Không có |
| PN532 | `nfc_clone_uid` | OneShot | Không có; cần NFC dump |
| PN532 | `nfc_write_ndef` | OneShot | `text*` |
| PN532 | `nfc_erase` | OneShot | Không có |
| IR | `ir_replay` | Replay | Không có; cần IR capture |
| IR | `ir_tvbgone` | OneShot | Không có |
| IR | `ir_custom_tx` | OneShot | `protocol*`; `code`, `bits`, `frequency`, `raw` tùy protocol |
| WiFi | `wifi_sniff` | Continuous | `channel` (1), `hop` (false) |
| CC1101 | `rf_jammer` 🔴 | Continuous | `freqMhz` (433.92), `mode` (`full`\|`intermittent`), `onMs` (100), `offMs` (100) |
| NRF24 | `nrf_jammer` 🔴 | Continuous | `startChannel` (0), `endChannel` (125), `dwellMs` (100) |
| WiFi | `wifi_beacon` 🔴 | Continuous | `ssid`, `intervalMs` (100) |
| WiFi | `wifi_deauth` 🔴 | Continuous | `mode` (`target`\|`flood`), `bssid` (target), `client`, `reason` (1), `intervalMs` (100) |
| WiFi | `wifi_evil_portal` 🔴 | Continuous | `ssid` (clone SSID), `channel` (1–13), `deauth`, `bssid` (AP đích), `client`, `reason` (1), `intervalMs` (100) |

- RF: chỉ các dải 300–348 / 387–464 / 779–928 MHz; sweep phải nằm trong một
  dải, `startMhz < endMhz`, tối đa 128 điểm. `stepKhz`: scan 10–5000,
  spectrum 25–5000. RSSI là giá trị quy đổi, chưa hiệu chuẩn trên phần cứng.
  Record tối đa 1024 duration, tổng 100 ms; cửa sổ 200–30000 ms.
  Replay 1–10 lần, gap 20–2000 ms; custom hex 1–32 byte, bit MSB-first,
  1 ms/bit. Phát bằng RMT bất đồng bộ, không bit-bang cả burst trong `loop()`.
- NRF: channel 0–125 (2400–2525 MHz), dwell 150–5000 µs; mỗi probe mở phiên
  RX mới để xóa RPD latch. Đây là đếm RPD/quan sát năng lượng, không giải mã gói.
- NFC: dump Classic 1K/4K với các key mặc định công khai; sector không đọc được
  được đánh dấu `holes`, dump vượt RAM có `truncated`. Type 2 gồm Ultralight,
  NTAG213/215/216. NDEF Text UTF-8 tối đa 64 byte, còn phải vừa dung lượng thẻ;
  `nfc_write_ndef` không ghi NDEF lên Classic. Clone chỉ UID sang thẻ magic
  writable-UID cùng family/độ dài UID: Classic 4 byte, Type 2 4/7 byte; không
  mở khóa Gen1 hay sao chép toàn bộ nội dung thẻ. Giữ manufacturer/BCC/lock
  tương ứng, verify lại UID. Erase Classic giữ block 0 và sector trailer;
  Type 2 xóa vùng NDEF. DESFire và công nghệ khác trả `nfc_tag_error`.
  Write/erase từ chối CC read-only, static/dynamic lock, control TLV malformed
  hoặc vùng reserved/lock nằm trong user area; control TLV của static/dynamic
  lock ngoài user area được giữ nguyên. Layout prefix phải nằm trong 16 byte
  đầu user area. Key không mặc định và layout không mô hình hóa có thể bị từ chối.
  Ghi/erase nhiều unit **không atomic**: lỗi hoặc Stop có thể để lại dữ liệu
  ghi một phần; không có rollback trên thẻ.
- `nfc_read_dump` chạy hữu hạn nhưng dùng **Record**, khác bảng roadmap ban đầu:
  cần giữ dump nhị phân cho clone, thay vì dùng JSON result làm record.
- IR: NEC 32 bit, SONY 12/15/20, RC5 12, SAMSUNG 32, PANASONIC 48 và RAW.
  Protocol có mã cần `code` là hex string và `bits`; RAW cần `raw` là danh sách
  µs phân cách dấu phẩy, chỉ RAW dùng `frequency` (30000–60000 Hz).
  RAW string tối đa 64 byte; capture/replay tối đa 512 timing, burst 500 ms.
  TV-B-Gone có **6 mã power-toggle tự viết** cho LG/Toshiba/Samsung/Sony/
  Philips/Panasonic, không phải database universal và không bảo đảm chỉ tắt TV.
  RMT channel 1 cho RF, 2 cho IR; để channel 0 cho status LED.
- WiFi: channel 1–13; hop mỗi 500 ms. Ring 15 frame usable, báo dropped;
  mỗi sample tối đa 6 frame, prefix raw tối đa 32 byte/frame và metadata
  type/subtype/RSSI/channel. Không giải mã credentials; chỉ chạy sau khi arbiter
  suspend AP. Cleanup đợi callback đang chạy kết thúc rồi mới reset ring/nhả lease.

**Chưa nghiệm thu phần cứng.** Các mốc WiFi 15 giây và liveness chip 2 giây là
mục tiêu cần đo trên board thật (cần ESP32-S3-N16R8, 2 thẻ NFC khác UID, remote
IR, AP thử có BSSID phân biệt, cổng Serial). Xem bảng AC trong
[`docs/planning/spec.md`](docs/planning/spec.md) §10.

**Disruptive (cờ build `ENABLE_DISRUPTIVE`).** Năm payload 🔴 ở bảng trên
(T14/T21/T51/T52/T53) chỉ được biên dịch khi build kèm `-DENABLE_DISRUPTIVE`.
Env mặc định `attak-iot-firmware` **không** chứa chúng trong catalog lẫn binary;
env `attak-iot-firmware-lab` bật cờ:

```sh
pio run -e attak-iot-firmware-lab -t uploadfs
pio run -e attak-iot-firmware-lab -t upload
```

- Tier `disruptive`: descriptor + handler bọc `#ifdef ENABLE_DISRUPTIVE`, dashboard
  hỏi xác nhận trước khi gửi.
- `rf_jammer`: carrier OOK liên tục trên CC1101 qua PATABLE/GDO0; `full` giữ mức
  liên tục, `intermittent` khoá on/off theo `onMs`/`offMs` do `poll()` (không
  bit-bang cả burst trong `loop()`). Qua arbiter `SharedSpi` như action CC1101 khác.
- `nrf_jammer`: constant carrier `CONT_WAVE` qua `RF24::startConstCarrier()`, nhảy
  kênh theo `dwellMs` trong `[startChannel, endChannel]`.
- WiFi dùng **chính AP của thiết bị** (`esp_wifi_80211_tx(WIFI_IF_AP, ...)`, không
  teardown): `wifi_beacon` phát beacon với SSID quay vòng hoặc `ssid` cố định;
  `wifi_deauth` gửi deauth tới BSSID đích (`target`) hoặc quét rồi lần lượt mọi
  AP (`flood`, tối đa 24 AP); `wifi_evil_portal` dựng captive portal (DNS spoof +
  trang đăng nhập) trên AP và stream mỗi POST thu được. Ba payload này không
  `radioExclusive`, nên `wifi_sniff` vẫn giữ nguyên mô hình exclusive cũ.
- **Evil twin (`wifi_evil_portal`)**:
  - Qua dashboard USB NCM, bật module WiFi rồi chạy action; **không cần Serial
    `ap on` trước**. Action tự bật AP tạm; **AP portal luôn mở (không mật khẩu)**
    vì ai ở gần cũng phải nối được, và không bao giờ dùng lại mật khẩu AP quản
    trị. Không có `ssid` thì lấy tên AP đã cấu hình. Stop/disable trả lại
    mode/kênh trước action: AP vốn tắt sẽ tắt lại, AP quản trị vốn bật được
    khôi phục **kèm mật khẩu gốc**. Lỗi khởi tạo cũng rollback. Dashboard USB
    `192.168.7.1` vẫn phục vụ riêng; portal chỉ bắt traffic trên AP.
  - Trang phục vụ là `/example.html` trong LittleFS ([`data/example.html`](data/example.html));
    sửa file đó rồi `pio run -t buildfs && pio run -t uploadfs` để nạp lại.
    Thiếu/rỗng/quá 16 KB ⇒ dùng trang mặc định nội tuyến. Form `POST` về chính
    nó; mỗi lần POST được giải mã thành `user`/`pass` và ghi vào nhật ký dashboard
    (loại sự kiện **Thu thập**, dạng `Portal #n: <user> / <pass>`), kèm `body` thô
    phòng khi form dùng tên trường không nhận dạng được.
  - Các URL probe captive của OS (Apple `hotspot-detect.html`, Android
    `generate_204`, Windows `connecttest.txt`/`ncsi.txt`, …) trả `302` về trang
    đăng nhập kèm `Cache-Control: no-store`, nên máy client tự bật trang.
  - Tham số tùy chọn: `ssid` **clone SSID** (1–32 byte in được) đổi tên AP của
    thiết bị thành bản giả của AP đích; `channel` (1–13) chuyển kênh. AP sau khi
    clone **vẫn mở** (không mang mật khẩu AP quản trị); AP quản trị có mật khẩu
    sẽ được restart một lần thay vì tái sử dụng, còn AP vốn đã mở thì chỉ đổi
    kênh và không rớt client. Khi Stop, tên gốc + kênh gốc + mật khẩu gốc được
    trả lại. Muốn đuổi client khỏi AP đích thì bật `deauth` ngay trong portal (xem
    dưới), hoặc chạy song song `wifi_deauth`.
  - **Deauth đi kèm (hoàn thiện evil twin).** Evil twin chỉ hiệu quả khi client
    rời AP thật, nên `wifi_evil_portal` có thể tự đuổi client khỏi AP đích trong
    lúc phục vụ trang đăng nhập. Bật bằng `deauth=true`; **`bssid` (AP đích) là
    bắt buộc** — thiếu hoặc sai định dạng thì `invalid_params`, không bao giờ
    phát broadcast. `client` là tuỳ chọn: bỏ trống = đuổi mọi client của AP đích,
    điền MAC = chỉ đuổi máy đó. `reason` (1–65535, mặc định 1) và `intervalMs`
    (20–5000, mặc định 100) đều được kẹp về khoảng hợp lệ. Khung deauth đi cùng
    interface AP của portal và trên **kênh của clone**, nên hãy clone đúng kênh
    AP đích thì khung mới tới nơi; mỗi nhịp gửi 3 khung liên tiếp vì một khung
    management đơn lẻ hay rớt. Không `delay()`, `loop()` không bị chặn;
    Stop/disable dừng deauth ngay và xoá sạch target. Frame `evil_portal` (lúc
    bắt đầu và mỗi lần bắt credential) có thêm `deauth`, `deauthBssid`,
    `deauthSent` để theo dõi.

BLE, OTA và battery vẫn ngoài phạm vi đợt này. Không đánh dấu toàn bộ roadmap
hoàn tất. Phần cứng của các payload disruptive **chưa nghiệm thu** (xem ghi chú
hardware ở trên).

## Phạm vi sử dụng

Chỉ dùng trên thiết bị/mạng **của bạn** hoặc khi đã được chủ sở hữu cho phép bằng
văn bản. Nhóm `disruptive` (jammer, beacon spam, deauth, evil portal) **gây nhiễu
và phá hoại kết nối** — bất hợp pháp ở nhiều quốc gia nếu không có môi trường thử
nghiệm được kiểm soát (phòng chắn sóng, mạng lab của bạn). Đây là bài tập lớn
phi thương mại, không dùng trên mạng công cộng.

## Kiểm thử

```sh
pio test -e native                  # logic portable, không cần board
pio run -e attak-iot-firmware       # build firmware (không có disruptive)
pio run -e attak-iot-firmware -t buildfs # build dashboard LittleFS
pio run -e attak-iot-firmware-lab   # build firmware kèm -DENABLE_DISRUPTIVE
```

**Kiểm chứng USB NCM (2026-10-06, bản hiện tại):**

- `pio test -e native`: **244/244 PASS**, 28 suite; có DHCP USB và AP intent.
- Build default **SUCCESS**: RAM **153148/327680**, flash **1499997/3342336 byte**.
- Build lab **SUCCESS**: RAM **155452/327680**, flash **1535873/3342336 byte**.
- LittleFS **SUCCESS**, chứa `/dashboard.js` và `/index.html`.
- ELF default có `usbd_app_driver_get_cb`, `netd_*` và network callbacks là
  symbol **strong (`T`)**: application NCM driver đã được link vào firmware.
- Host smoke ASan/UBSan chạy NCM driver thật: NTB bounds/negotiation,
  reentrant renewal và ZLP/malformed recovery. Smoke `usb_network.cpp` thật
  kiểm tra TX bounded/chained, producer hoàn tất sau reset/replug không phát
  frame cũ, chỉ worker gọi defer, readiness/RX khi transition bị supersede,
  và hai cờ eligibility để lwIP route IPv4. RTOS/USB/netif APIs là fixture.
- Smoke `wifi_ap.cpp` thật với WiFi fixture: boot AP off, explicit on/off,
  suspend/restore và báo lỗi teardown. Chromium chạy dashboard thật với HTTP/
  WebSocket fixture: USB giữ Stop khi AP suspend; AP vẫn cảnh báo; **0 page error**.
- **Nạp và chẩn đoán board thật (2026-10-06):** upload LittleFS/firmware thành
  công, hash verified. Windows báo Code 10 / `0xC0000483` vì NTB OUT divisor
  bằng 1; driver `UsbNcm.sys` trên máy yêu cầu lũy thừa hai >= 4. Đổi divisor
  sang 4: live `GET_NTB_PARAMETERS` đạt điều kiện, Windows nhận thiết bị
  **OK**, adapter **Up / 12 Mbps**. Không cài driver ngoài.
- **Sửa MAC phía USB:** iMACAddress là MAC adapter host, không dùng lại làm MAC
  lwIP/DHCP của board. Giữ MAC host từ efuse; MAC board đặt bit locally
  administered và đảo bit cuối. Probe ARP thật đã bắt được lỗi hai MAC trùng
  trước sửa. Bản sửa đã build/upload, RAM **153148**, flash **1500013 byte**;
  UART xác nhận boot **AP not started (requested off)**. Probe USB thật xác nhận
  MAC host `90:70:69:f7:d7:90`, MAC board `92:70:69:f7:d7:91` và ARP reply.
- **Bố cục NTB của Windows:** host đặt datagram trước NDP; bộ nhận cũ từ chối
  mọi datagram bắt đầu trước byte 28, nên bỏ ARP/DHCP hợp lệ. Probe thật với
  datagram ở byte 14 và NDP ở cuối tái hiện mất reply trước sửa. Bộ nhận nay
  kiểm tra datagram không chồng NTH/NDP và nằm trong block length, chấp nhận
  cả NDP-trước/dữ-liệu-trước. Smoke biên dịch validator thật với ASan/UBSan:
  **7/7 PASS** (hai layout, chồng header/NDP, vượt biên khai báo, zero length).
  Bản sửa đã upload **SUCCESS / hash verified**, RAM **153148**, flash
  **1500033 byte**. Probe cùng NTB layout Windows nay nhận ARP reply trên board.
- **Nghiệm thu đường quản trị Windows:** adapter **Up**, dashboard `/` và
  `/dashboard.js` **HTTP 200**, `/ws` nhận catalog, module status và
  `transport_info=usb`. Chromium mở trang thật: **Đã kết nối**, log điều khiển
  qua USB/AP đang tắt, **0 page error**. Sau chuyển USB Windows→WSL→Windows,
  HTTP/WebSocket vẫn pass. Đây là kiểm tra re-enumeration bằng phần mềm,
  chưa thay bài thử rút/cắm nhiều lần hay phiên dài.
- **IP Windows giữ theo lựa chọn người dùng:** `192.168.7.3/24` static,
  DHCP tắt trên adapter ATTAK; không thay WiFi/default route. DHCP tự động
  Windows chưa nghiệm thu, không dùng IP static làm bằng chứng DHCP pass.

**Kiểm chứng disruptive (2026-10-06):** `pio test -e native` **238/238 PASS**
(26 suite, env native bật `-DENABLE_DISRUPTIVE` để test descriptor/handler logic);
`pio run -e attak-iot-firmware-lab` **SUCCESS** (RAM 123780, flash 1499809 byte);
`pio run -e attak-iot-firmware` **SUCCESS** (RAM 121476, flash 1464229 byte). Wire
id `rf_jammer`/`nrf_jammer`/`wifi_beacon`/`wifi_deauth`/`wifi_evil_portal` **chỉ**
có trong binary lab — bản mặc định chỉ còn symbol nội bộ ESP-IDF trùng chuỗi con.
Không có phần cứng: RF/NRF carrier, phát 802.11 thô và captive portal **chưa**
nghiệm thu trên board.

**Kiểm chứng cutover payload (2026-10-05):**

- `pio test -e native`: **219/219 PASS**, 23 suite.
- Firmware build **SUCCESS**: RAM **120620/327680 byte (36,8%)**,
  flash **1461737/3342336 byte (43,7%)**. Đây không phải đo heap/stack peak.
- LittleFS build **SUCCESS**, chứa dashboard thật.
- Host smoke compile source module thật: RF **84/84**, IR **86/86**,
  NFC **138/138**; NRF/WiFi **ALL CHECKS PASSED**, gồm RPD latch, callback
  lúc enable, failure từng stage và callback producer concurrent khi cleanup.
  Backend SPI/Wire/RMT/WiFi là fixture, **không phải chạy chip thật**.
- Chromium với catalog/dashboard thật kiểm tra RF params, NRF Stop=disable,
  generic result chứa HTML không thực thi, `nfc_tag_error` và mobile 390px;
  **0 page error**. Transport/hardware status là fixture.

Lịch sử kiểm chứng Layer 1 trên host: **131/131 test PASS** (16 suite), build firmware và
`pio run -e attak-iot-firmware -t buildfs` **SUCCESS**. Smoke chạy `main.cpp` thật
với backend fixture: parser→FIFO→dispatch, Record→Replay admission→disable,
Continuous→stream→Stop, SPI exclusion qua cleanup, deadline wrap và AP restore
retry. Smoke `wifi_ap.cpp` thật với WiFi API fixture kiểm tra failed bring-up,
STA/AP teardown, failed restore và không allocation khi restore cached credentials.
Chromium nhận frame runtime thật, kiểm tra form/UTF-8, buffer gate, frame cũ,
Stop/reconnect; desktop/mobile không overflow, **0 page error**.

Giới hạn tooling: GCC/PlatformIO build pass; clangd còn cascade Xtensa/newlib
tại Serial `std::string` và nhận sai các member `std::atomic` trong RF/WiFi.
Probe mới xác nhận NFC module/helpers sạch; một file của probe khác timeout,
không coi là clean. Không bỏ concurrency safety hoặc thêm suppression để làm
LSP xanh. Warning ArduinoJson `containsKey` và FastLED còn từ dependencies;
không tuyên bố LSP/compiler warning hoàn toàn sạch.

## Vận hành

```sh
pio run -t uploadfs     # nạp data/ (dashboard static files) vào LittleFS
pio run -t upload       # nạp firmware
pio device monitor
```

### Dashboard qua USB NCM (Windows 11)

1. Nạp firmware và LittleFS qua cổng **UART/CH343** như trên.
2. Cắm cáp **có truyền dữ liệu** từ laptop Windows 11 vào cổng **USB native**
   của board (GPIO19 D− / GPIO20 D+). Cổng CH343 không thể tạo mạng NCM.
3. Windows nhận card mạng USB NCM bằng driver tích hợp `UsbNcm.sys`.
   Để IPv4 ở chế độ tự động; mở **`http://192.168.7.1/`** bằng trình duyệt.

USB là mạng cục bộ `192.168.7.0/24`, firmware cấp địa chỉ laptop qua DHCP.
Không quảng bá default gateway hay DNS để không thay đường Internet của laptop.
HTTP/WebSocket `/ws` và giao diện vẫn chạy trên ESP32; không cần ứng dụng desktop.
AP quản trị **tắt khi boot**, scan/sniff WiFi không cần AP để điều khiển qua USB.
Chưa cam kết Windows 10, ECM, hoặc nhận NCM thực tế trước nghiệm thu board.

AP dự phòng chỉ bật khi gửi `ap on` qua Serial CH343, tắt bằng `ap off`;
không lưu trạng thái qua reboot. Phải disable module WiFi và đợi cleanup/radio
idle trước khi đổi AP. Khi AP bật, dùng SSID/password trong
[`src/core/storage.h`](src/core/storage.h) hoặc `/config.json` trên LittleFS,
rồi mở `http://192.168.4.1/`. AP không tự bật khi USB lỗi hoặc rút cáp.

Riêng bản lab, `wifi_evil_portal` tự bật AP theo vòng đời action mà không đổi
trạng thái yêu cầu AP quản trị. Beacon/deauth vẫn cần `ap on` trước. Kiểm tra
vòng đời/rollback trên host bằng `python3 tools/test_wifi_ap_lifecycle.py`;
radio double không thay thế nghiệm thu AP và USB trên ESP32 thật.

Nghiệm thu phần cứng: Windows nhận NCM và DHCP không cấu hình tay; mở trang và
WebSocket; rút/cắm lại USB; dùng Dừng trong WiFi-exclusive; xác nhận Internet
laptop vẫn dùng đường cũ và không thấy SSID quản trị khi boot.

### Scan I2C độc lập (GPIO4/5)

`src/diagnostics/i2c_scan.cpp` dùng SDA **GPIO4**, SCL **GPIO5**, bus 100 kHz,
Serial 115200 baud; quét địa chỉ 7-bit không reserved mỗi 3 giây.
Env mặc định/lab không biên dịch scanner.

```sh
pio run -e i2c-scan -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Bản scan tạm thay firmware dashboard, không ghi LittleFS. Nạp lại firmware chính
bằng `pio run -e attak-iot-firmware -t upload --upload-port /dev/ttyACM0`.
Nếu không thấy địa chỉ: kiểm tra SDA/SCL, nguồn, GND chung, mode I2C của module
và pull-up SDA/SCL lên 3.3 V (không kéo GPIO ESP32 lên 5 V).

**Đã chạy trên board (2026-10-06):** build/upload SUCCESS, hash verified;
Serial qua CH343 `/dev/ttyACM0` lặp lại `Found I2C device at 0x3C`,
`Scan complete: 1 device(s), 0 error(s)`. Đây chỉ là ACK địa chỉ;
chưa xác định loại chip và không thấy PN532 ở địa chỉ 7-bit `0x24`.

### Serial console (kênh điều khiển)

Mở Serial ở **115200 baud**. Firmware in một dòng hướng dẫn rồi nhận **đúng JSON
lệnh như WebSocket** (một dòng, tối đa 512 byte), ví dụ:

```json
{"id":1,"module":"wifi","cmd":"action","action":"wifi_deauth","params":{"mode":"target","bssid":"AA:BB:CC:DD:EE:FF"}}
{"id":2,"module":"wifi","cmd":"disable"}
```

Mỗi lệnh được dispatch qua cùng đường `dispatchCommand` và trả về một dòng
`command_result` JSON; snapshot `[F0]` vẫn in khi trạng thái đổi. Serial là đường
điều khiển độc lập với USB NCM và AP. Dashboard USB cũng dùng được nút Dừng khi
AP bị tắt hoặc captive portal chiếm HTTP trên AP. Dòng quá dài bị bỏ, không cắt ngắn.
Hai lệnh quản trị ngoài JSON là `ap on` và `ap off` (mỗi lệnh một dòng).

### Bring-up F0

- Mở Serial ở **115200 baud**, rồi bật từng module từ dashboard; boot vẫn để
  tất cả module off. Firmware ghi snapshot `[F0]` khi revision đổi, gồm uptime
  `ms`, `en`/`ok`, trạng thái/lỗi action, cleanup, sequence và health detail.
- Serial dùng buffer cố định, ghi tối đa 32 byte mỗi vòng khi UART còn chỗ;
  không đợi monitor, không ghi heartbeat hoặc payload thu được. Khi Serial bị
  nghẽn, các thay đổi trung gian có thể được gộp thành snapshot mới nhất; đây
  không phải trace dùng để chứng minh mọi mốc thời gian.
- CC1101/NRF24/PN532 cần health probe thật; IR `ok=1` chỉ xác nhận RX driver
  đã khởi tạo, phải capture từ remote để chứng minh cảm biến nhận tín hiệu.
- Disable báo `enabled=false`, `connected=false`, `detail="off"` ngay, nhưng
  giữ `cleanupPending` cho tới khi backend giải phóng xong.
- F0 chỉ xanh sau khi board thật xác nhận từng chip, hai radio chung SPI,
  USB NCM/dashboard (và AP dự phòng khi yêu cầu), đo WiFi 15 giây/liveness 2 giây.
  Test native, build và smoke
  bằng transport giả lập **không thay thế** nghiệm thu này.

**Flash F0 thực tế (2026-10-05):** đã nạp firmware và LittleFS qua CH343
`/dev/ttyACM0`, esptool verify hash thành công. Board được nhận ESP32-S3,
flash 16 MB/PSRAM 8 MB. UART xác nhận AP `AttakIoT` lên tại `192.168.4.1`;
boot mặc định tất cả module off. Theo người dùng, **CC1101 chưa lắp**, các
module khác đã lắp; chưa bật/probe chúng nên không coi `ok=0` khi off là lỗi chip.

Backup flash gốc và log boot nằm trong `.pio/f0-backup-g1LZD3/`
(`original-16mb.bin`, `boot-serial.log`); giữ backup trước khi ghi đè filesystem.
Boot thiếu `/config.json` nên dùng defaults; FastLED có warning generic clockless
fallback, chưa kiểm chứng LED/timing. Kiểm tra dashboard/action trên board còn
chờ kết nối máy tính hoặc điện thoại vào AP; chưa nghiệm thu UID/capture và 15s/2s.


## License

GPL-2.0 — bắt buộc vì phụ thuộc [`nrf24/RF24`](https://github.com/nRF24/RF24)
(GPL-2.0-only) cho driver NRF24. Xem [LICENSE](LICENSE) và phần license audit
trong `docs/planning/map.md`.

## Kế hoạch

Xem `docs/planning/intent.md` và `docs/planning/map.md` (wayfinder
map — quyết định kiến trúc, ticket còn mở).
Lộ trình mới và dependency giữa các tầng nằm trong [`PLAN.md`](PLAN.md);
rubric nghiệm thu subagent nằm trong [`REVIEW.md`](REVIEW.md).

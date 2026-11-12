# Báo cáo trạng thái module — attak-iot-firmware

**Ngày:** 2026-11-12
**Phạm vi:** cây đang phát triển `~/school_proj/Attk_IOT/attak-iot-firmware` (live checkout).
Cây `/mnt/d/Năm 4/Attk_IOT/attak-iot-firmware` là snapshot 2026-10-01: IR ở đó vẫn là
stub và WiFi chưa nối vào `main.cpp`, nên mọi kết luận dưới đây dựa trên live checkout
và xác nhận thực tế trên board của người thực hiện.

## 1. Tóm tắt

| Module | Trạng thái trên board | Bằng chứng phần mềm | Giới hạn đã biết |
| --- | --- | --- | --- |
| **IR** | **Đã chạy tốt** | 4 action; `test_ir_tx`, `test_ir_record` pass | TV-B-Gone mới có 6 mã đại diện |
| **WiFi** | **Đã chạy tốt** | 6 action; 7 suite test WiFi pass | Payload disruptive chỉ có trong build lab |
| CC1101 | Chưa thử được | 5 action; driver 1294 dòng; native test pass | Chưa nghiệm thu trên chip/ăng-ten |
| NRF24 | Chưa thử được | scan + jammer; `test_nrf_spectrum`, `test_jam_plan` pass | Chung SPI với CC1101, cần board |
| PN532 | Chưa thử được | I2C bring-up trong snapshot build SUCCESS; live checkout 5 action | Phiên này không có `/dev/ttyACM*`, ticket 07 còn checkbox cuối |

**Kết luận: 2/5 module đã chạy tốt (IR, WiFi); 3/5 module còn chờ nghiệm thu phần cứng
(CC1101, NRF24, PN532).** Phần mềm chạy đúng (native test + build) không thay thế nghiệm
thu trên chip — đúng gate F0 trong `PLAN.md`.

Bằng chứng tự động của live checkout (chạy lại trong phiên này):

```
pio test -e native  →  279 test cases: 279 succeeded in 00:00:24.089
```

## 2. Module IR — đã chạy tốt

**Chức năng**

- `capture` — thu burst IR bằng `IRrecv` (buffer 514 timing, deadline 10 s), lưu vào
  record buffer RAM.
- `ir_replay` — phát lại raw timing đã capture.
- `ir_custom_tx` — phát theo protocol + code + số bit + tần số, hoặc raw timings.
- `ir_tvbgone` — phát tuần tự bộ power code TV-B-Gone.

**Cách hoạt động:** TX đi qua peripheral RMT (channel 2 — tránh channel 0 của FastLED
và channel 1 của CC1101 theo bảng channel đã cố định), `rmt_write_items(...,
wait_tx_done=false)` nên `poll()` chỉ trả giá copy item; Stop hủy burst đang chạy và mọi
burst kế tiếp. RX dùng timer + GPIO ISR của IRremoteESP8266, không chiếm RMT.

**Kiểm chứng:** `test_ir_tx`, `test_ir_record` pass trong 279 test case; xác nhận chạy
trên board bởi người thực hiện.

**Giới hạn:** bảng TV-B-Gone mới có 6 mã hand-picked (LG/Toshiba/Samsung/Sony/Philips/
Panasonic), chưa phải bộ đầy đủ 137 NA + 139 EU mã raw timing. Bảng tham chiếu nằm trong
cây Bruce (AGPL-3.0) / upstream CC BY-SA 2.5 nên không copy trực tiếp được vào repo
GPL-2.0 này; muốn đủ phải tự sinh bảng từ dữ liệu protocol/capture.

## 3. Module WiFi — đã chạy tốt

**Chức năng**

- `scan` — quét SSID kèm lớp bảo mật (không chỉ open/closed).
- `wifi_sniff` — bắt frame promiscuous, hop kênh có tham số, ring buffer không cấp phát
  trong callback.
- Build lab (`-DENABLE_DISRUPTIVE`): `wifi_beacon`, `wifi_deauth` (target/flood),
  `wifi_evil_portal`, `wifi_evil_twin` (clone AP + portal + deauth có target).

**Kiểm chứng:** 7 suite pass — `test_wifi_scan_result`, `test_wifi_sniff`,
`test_wifi_attack`, `test_evil_twin`, `test_evil_twin_plan`, `test_wifi_ap_policy`,
`test_wifi_ap_request`; xác nhận chạy trên board bởi người thực hiện.

**Giới hạn:** payload disruptive bị loại khỏi build release bởi `ENABLE_DISRUPTIVE`;
beacon/deauth cần AP bật trước; evil twin bắt buộc BSSID AP đích.

## 4. Ba module chưa thử được

| Module | Đã có trong code | Thiếu gì để nghiệm thu |
| --- | --- | --- |
| **CC1101** | `rf_scan`, `rf_record`, `rf_replay`, `rf_spectrum`, `rf_custom_tx` (+ `rf_jammer` lab); dùng chung SPI với NRF24 qua radio arbiter | Cắm module + ăng-ten, xác nhận chip ID/register qua SPI, thử scan/replay thật |
| **NRF24** | `nrf_scan`, `nrf_jammer` (lab); liveness bằng `isChipConnected()` | Board + module; kiểm tra CE/CS GPIO9/14, dòng PA+LNA, không chạy đồng thời CC1101 |
| **PN532** | Snapshot: I2C bring-up `Adafruit_PN532` trên GPIO4/5, probe firmware, hot-plug reset/wakeup (build SUCCESS, `i2c-scan` env SUCCESS). Live checkout: `read_uid`, `nfc_read_dump`, `nfc_clone_uid`, `nfc_write_ndef`, `nfc_erase` | Cắm module ở chế độ I2C (`SEL0=ON, SEL1=OFF`), 3.3 V, pull-up; xác nhận log boot `pn532: connected (…)`, rồi thử từng action |

PN532 là module duy nhất trong nhóm này vừa được implement trong phiên: firmware biên
dịch và native test xanh, nhưng **chưa có board gắn** (`/dev/ttyACM*` không tồn tại),
nên checkbox "Verified on the physical board" của ticket 07 vẫn để mở.

## 5. Cách tái lập kiểm chứng phần mềm

```sh
# Live checkout (ext4)
pio test -e native                 # 279/279 pass
pio run -e attak-iot-firmware      # release
pio run -e attak-iot-firmware-lab  # lab (disruptive)

# Snapshot trên /mnt/d
tools/check.sh                     # 13/13 native + release + i2c-scan
pio run -e i2c-scan -t upload      # chẩn đoán dây I2C (không phải liveness)
```

Lưu ý: bản live checkout dùng để chạy test có thay đổi chưa commit (`PLAN.md`,
`data/*`, `src/modules/wifi_module.cpp`); kết quả test phản ánh đúng trạng thái làm việc
tại thời điểm báo cáo.

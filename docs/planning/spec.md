# attak-iot-firmware — Dashboard và chức năng quan sát trên phần cứng thật

Status: approved; implementation-plan-ready
Revision: 2026-10-04

**Phê duyệt:** Phạm vi và hành vi đã được người dùng chốt qua Q1–Q21, xác nhận kết luận grill và yêu cầu tạo plan. Hợp đồng kỹ thuật dưới đây cụ thể hóa các quyết định đó; không phải tuyên bố mã đã triển khai hoặc phần cứng đã nghiệm thu.

**Nguồn sự thật:** File này là spec hiện hành cho đợt dashboard/quan sát, thay thế yêu cầu v1 cũ tại cùng đường dẫn. `intent.md`, `map.md` và tickets cũ giữ bối cảnh/lịch sử; khi khác phạm vi hoặc hành vi đợt này, dùng spec này. Plan triển khai tại `docs/superpowers/plans/2026-10-04-dashboard-completion.md` phải map đầy đủ R01–R21 và AC01–AC12 trước khi thực thi.

**Quyền thực hiện hiện tại:** Chỉ cập nhật tài liệu; không sửa mã ứng dụng, commit hoặc flash thiết bị.

## 1. Vấn đề và mục tiêu

Dashboard hiện có sidebar/detail/log nhưng chỉ biết 4 module, bỏ qua enabled/output, tự sinh dữ liệu giả khi không kết nối và diễn giải uptime như Unix time. Firmware đã có JSON status mở rộng, parser lệnh và WiFi scan nhưng chưa nối thành một luồng điều khiển hoàn chỉnh; bốn driver module rời còn stub.

Mục tiêu là một dashboard đáng tin cậy, điều khiển được thiết bị headless qua AP riêng và chứng minh kết quả thật từ browser đến ESP32-S3. Mỗi chức năng phải có luồng thành công, lỗi và hủy được kiểm tra; build/native tests hoặc mock UI không đủ để nghiệm thu phần cứng.

**Baseline có bằng chứng:** Trong lần khảo sát trước, `pio test -e native` đạt 12/12; đây chỉ là bằng chứng JSON logic hiện có. Chưa có bằng chứng build ESP32, driver thật hoặc nghiệm thu phần cứng cho đợt này. Không coi các ghi chú “đã ship” cũ là chứng minh driver hoạt động.

## 2. Phạm vi và ràng buộc

### 2.1 Trong phạm vi

- Overview và trang thao tác cho `cc1101`, `nrf24`, `pn532`, `ir`, `wifi`; giữ pattern sidebar/detail/event log.
- Kết nối thật, reconnect, stale, đồng bộ nhiều browser, enable/disable và phản hồi lệnh.
- WiFi: scan một lượt theo lệnh, bảng SSID/BSSID/RSSI/kênh/bảo mật.
- PN532: đọc một UID theo lệnh.
- IR: capture một thông điệp theo lệnh, decode nếu hỗ trợ và raw timing nếu UNKNOWN.
- CC1101/NRF24: bring-up, kiểm tra kết nối/liveness, không có payload phát sóng.
- RGB báo aggregate health, log có giới hạn/lọc/xóa, xuất log và kết quả riêng.
- Laptop điều khiển đầy đủ; điện thoại xem được.

### 2.2 Ngoài phạm vi đợt này

RF record/replay, IR replay, NRF24 jammer, deauth, evil portal, promiscuous capture, NFC key/dump/ghi/clone, BLE, OTA, đổi AP credentials qua UI, STA provisioning/captive portal, battery management, PCB/vỏ máy, dashboard login/phân quyền và lưu lịch sử payload trên ESP32.

Roadmap v2 cũ không bị đánh dấu hoàn thành hoặc xóa lịch sử. Các chức năng ngoài phạm vi cần đặc tả và nghiệm thu riêng; không tạo nút giả, backend no-op hay kết quả mô phỏng để thay thế.

### 2.3 Global constraints cho bước plan

- Board: ESP32-S3-N16R8, flash 16MB, octal PSRAM 8MB; giữ `board = esp32-s3-devkitc-1`, `board_build.flash_size = 16MB`, `board_build.arduino.memory_type = qio_opi`, `-DBOARD_HAS_PSRAM`.
- PlatformIO + Arduino; dashboard HTML/CSS/JavaScript thuần trên LittleFS; WebSocket `/ws`; không thêm SPA/framework hoặc backend ngoài ESP32.
- Application C++17 cho firmware và native: parser dùng `std::string_view`; khi triển khai thêm `build_unflags = -std=gnu++11` và `-std=gnu++17` trong env firmware, giữ nguyên core/platform và các flag N16R8. Đây là lựa chọn implementation, không mở rộng tính năng.
- Pin mapping: SPI SCK12/MOSI11/MISO13; CC1101 CS10/GDO8; NRF24 CS14/CE9; PN532 SDA4/SCL5; IR RX6/TX7; RGB48. `include/board_pins.h` là nguồn pin.
- CC1101/NRF24 dùng chung một SPI bus; PN532 dùng I2C. Không copy code AGPL/Bruce; dependency phải tương thích license dự án.
- AP riêng, một người vận hành, nhiều tab được quyền điều khiển; không đăng nhập, không public lên Internet. Người kết nối AP được xem là tin cậy.
- Config AP/deviceName tiếp tục từ `DeviceConfig` và LittleFS `/config.json`; không thêm UI đổi mật khẩu hoặc reset cấu hình.
- Module boot off; off không chạy action/liveness I/O mới, chỉ được cleanup có giới hạn của tác vụ đã hủy. Payload chỉ ở RAM, mất khi disable/reboot; export là tải xuống browser, không lưu payload vào LittleFS.
- Mất browser cuối cùng không dừng module, không xóa payload, không chạy lại action khi reconnect.
- Chỉ thao tác trên thiết bị/mạng/thẻ được phép sử dụng; không bổ sung chức năng gây nhiễu hoặc phát lại trong đợt này.
- Stop hủy logic và xóa payload khi được xử lý; backend có thể cleanup sau. Status có `cleanupPending`, mọi browser hiển thị “Đang giải phóng tài nguyên” và khóa action đến khi cleanup xong; enable không xóa nhãn cleanup.
- Queue bảo đảm 8 lệnh thường và một Stop đang chờ cho mỗi category (5 slot riêng), tổng tối đa 13. FIFO theo thứ tự tiếp nhận chung; Stop không vượt hàng, Stop lặp không chiếm slot của category khác.
- WiFi 15 giây và chip liveness 2 giây là mục tiêu kỹ thuật ban đầu, cần đo trên board; không tự nới hoặc nghiệm thu pass nếu chưa đạt/được duyệt sửa spec. NFC 5 giây và IR 10 giây là timeout đã chốt.

## 3. Thuật ngữ và state model

| Thuật ngữ | Nghĩa |
|---|---|
| Transport | Kết nối browser ↔ ESP32; độc lập health module. |
| Enabled | Ý định bật module của người vận hành; không chứng minh phần cứng khỏe. |
| Health | Off, ready hoặc error, suy ra từ enabled/connected. |
| Action | Một scan/read_uid/capture có điểm bắt đầu và kết thúc; không là chế độ chạy định kỳ. |
| Command result / ack | Thiết bị đã áp dụng enable/disable hoặc đã nhận khởi động action; không phải kết quả action. |
| Payload | Một kết quả action thành công gần nhất của module. |
| Kết quả cũ | Payload của lần thành công trước khi action mới đang chạy hoặc đã timeout/lỗi. |
| Stale | Browser không có status mới đủ gần; không đồng nghĩa phần cứng ngắt. |
| Uptime | Millisecond uint32 của ESP32, không phải thời gian thực/Unix epoch. |

### 3.1 Health và thao tác

| Trạng thái | enabled | connected | Quy tắc |
|---|---|---|---|
| off | false | false | Chưa kiểm tra/đã tắt; không I/O action/liveness mới, có thể cleanup; không payload. |
| ready | true | true | Module khỏe; nhận action phù hợp nếu không running/cleanupPending. |
| error | true | false | Giữ ý định bật; kiểm tra lại kết nối định kỳ; không nhận action mới. |

- Boot không tự kiểm tra hoặc chạy action trên module rời; chỉ thiết lập hạ tầng/AP và state off.
- Enable module đang off: thực hiện bring-up/liveness có giới hạn; lỗi vẫn giữ enabled=true và detail lỗi. Enable module đã bật không khởi tạo lại, không hủy action/payload.
- Disable khi được xử lý: hủy logic, xóa payload/tuổi kết quả, đưa về off/idle và xác nhận Stop; backend có thể cleanup sau. `cleanupPending=true` đến khi backend thực sự giải phóng; enable không xóa flag. Disable đã off vẫn thành công, không tạo tác vụ và không làm cleanup đang có biến mất.
- Khi mất health trong action: kết thúc action với hardware_error, giữ payload thành công trước, tiếp tục kiểm tra lại liveness khi enabled.
- Phần cứng phục hồi: chuyển health về ready, giữ trạng thái lỗi của action trước cho tới lệnh mới; không tự chạy lại action.
- WiFi category off không tắt AP/radio phục vụ dashboard. IR connected chỉ thể hiện driver RX được khởi tạo, không thể chứng minh cảm biến vật lý có mặt.

### 3.2 Vòng đời action

`idle → running → succeeded | timeout | error`. Lệnh action mới hợp lệ có thể bắt đầu từ bất kỳ trạng thái kết thúc nào nếu health ready. Disable từ mọi trạng thái đưa về off/idle và vô hiệu hóa kết quả đến muộn.

- Action mới khi running hoặc cleanupPending trả busy; không thay tác vụ và không lịch chạy sau. UI khóa action và hiện “Đang giải phóng tài nguyên” khi flag true, kể cả module off hoặc đã enable lại.
- Bắt đầu action giữ payload cũ; UI gắn nhãn “Kết quả lần trước”.
- Thành công thay payload nguyên tử, tăng sequence kể cả nội dung giống lần trước, cập nhật uptime kết quả.
- Timeout/lỗi giữ payload và metadata thành công cũ; trình bày lỗi lần mới riêng, không báo payload cũ là kết quả mới.
- Một lần thành công không có mạng WiFi là kết quả mới array rỗng, phải thay payload cũ.
- Disable là ngoại lệ xóa payload. Cancellation phải chặn completion của phiên cũ kể cả đã enable/chạy action phiên mới; kiểm tra phiên/generation, không chỉ nhìn enabled.

## 4. Yêu cầu sản phẩm và traceability

| ID | Quyết định | Yêu cầu | Nghiệm thu |
|---|---|---|---|
| R01 | Q1=B | Demo end-to-end thật; không nghiệm thu bằng scaffold/native tests riêng. | AC12 |
| R02 | Q2=A | Chỉ phạm vi §2.1; roadmap còn lại tách đợt riêng. | AC01, AC12 |
| R03 | Q3=A | AP riêng, nhiều tab cùng quyền, cùng state; không khóa quyền một tab. | AC03 |
| R04 | Q4=B | Browser cuối ngắt không dừng module/action. | AC04 |
| R05 | Q5=A | WiFi bật về ready, mỗi lệnh scan chạy một lượt. | AC06 |
| R06 | Q6=A | NFC một UID, deadline 5 giây, không thấy thẻ khác lỗi PN532. | AC07 |
| R07 | Q7=A | IR một thông điệp, deadline 10 giây, bỏ frame repeat như một lần nhấn mới. | AC08 |
| R08 | Q8=A | Lỗi giữ enabled, kiểm tra lại health; phục hồi không tự action. | AC02 |
| R09 | Q9=B | Giữ một payload thành công gần nhất, ghi nhãn cũ khi chạy lại/lỗi. | AC09 |
| R10 | Q10=B | WiFi có BSSID, không gộp AP cùng SSID. | AC06 |
| R11 | Q11=B | Xuất riêng log và payload có metadata; Clear log không xóa payload. | AC10 |
| R12 | Q12=A | Laptop vận hành chính; điện thoại chỉ cần xem được. A sau cùng thay B trước đó. | AC11 |
| R13 | Q13=A | Running từ chối action mới; disable hủy/xóa, bỏ kết quả muộn. | AC03, AC09 |
| R14 | Q14=A | Ack timeout 3 giây → chưa xác định, không tự retry và không đoán từ payload cũ. | AC05 |
| R15 | Q15=A | Checklist board + ảnh/kết quả xuất + log thành công/lỗi/hủy; không bắt buộc video. | AC12 |
| R16 | Q16=B | WiFi 15 giây/liveness 2 giây là mục tiêu ban đầu; đo thật, không tự nới hoặc pass giả. | AC02, AC06, AC12 |
| R17 | Q17=A | Stop xác nhận hủy logic trước cleanup vật lý; xóa payload và bỏ completion muộn. | AC03, AC09 |
| R18 | Q18=B | Lệnh thường không chiếm dung lượng tiếp nhận Stop; giữ FIFO chung. | AC03 |
| R19 | Q19=A | Chức năng/lỗi phần cứng cơ bản dùng board; edge protocol/boundary dùng injection có nhãn. | AC12 |
| R20 | Q20=B | Bảo đảm một Stop/category; Stop lặp không chiếm slot bảo đảm của category khác. | AC03 |
| R21 | Q21=A | Cleanup được publish để mọi tab thấy; enable lại không bỏ flag, action khóa/busy đến cleanup xong. | AC03, AC11 |

## 5. Kiến trúc và quyền sở hữu

- Browser sở hữu state hiển thị, pending command, thời điểm nhận, event log và download. Không optimistic-toggle enabled; firmware là nguồn state module.
- Callback AsyncTCP/WebSocket chỉ kiểm tra frame/parse/enqueue; không truy cập driver hoặc mutate state module. Đồng bộ enqueue/dequeue; không giữ lock trong I/O, serialize hay gửi socket.
- `loop()` sở hữu dispatch, lifecycle module, poll và publish. Command không chờ action hoàn tất; driver không sleep/chặn trong toàn bộ cửa sổ 5/10 giây.
- Mỗi module sở hữu driver, trạng thái action và payload RAM. SPI chung có một chủ cấu hình; giao dịch theo thứ tự trong loop với CS đúng.
- Queue chứa client/session id và command: 8 vị trí lệnh thường + 5 vị trí Stop, mỗi category tối đa một Stop đang queued; tổng tối đa 13. Cùng một FIFO, không có priority vượt hàng. Dequeue giải phóng dung lượng loại tương ứng; Stop lặp khi slot category đang dùng trả queue_full, không chiếm slot khác. Dequeue action thấy running/cleanup trả busy ngay, không giữ chờ.
- Browser đóng không làm mất lệnh đã enqueue: firmware vẫn xử lý, phản hồi chỉ gửi nếu client còn tồn tại. Client mới không được nhận ack thuộc client cũ.
- Publish lấy snapshot state do loop sở hữu; tránh copy sâu string hoặc encode payload ở mọi vòng poll. Serialize payload một lần khi có kết quả; heartbeat không đổi nội dung/metadata kết quả.
- Trách nhiệm file: `data/index.html` markup/style, `data/dashboard.js` state/transport/render; `src/core/web_dashboard.*` queue/transport; `src/core/ws_command*` command parsing; `src/core/module_status*` contract/codec; `src/main.cpp` orchestration; `src/modules/*` driver; `src/core/status_led.*` RGB. Plan xác định chính xác helper pure logic và covering tests, không tạo lớp registry/framework nếu switch 5 module đủ dùng.

## 6. Hợp đồng WebSocket

### 6.1 Frame status

Giữ các tên trường hiện có và mở rộng ngay trong đợt này; frontend/firmware phải nâng cùng nhau, không duy trì đường legacy.

| Field | Type | Quy tắc |
|---|---|---|
| module | string enum | cc1101/nrf24/pn532/ir/wifi |
| enabled | boolean | Ý định bật; false khi boot/off. |
| connected | boolean | Health khi enabled; false khi off. |
| detail | string | Health explanation; không dùng để trộn lỗi action và chip health. |
| output | string | Rỗng nếu không payload; nếu có là JSON serialized theo §7. |
| lastUpdateMs | uint32 | Uptime lúc state/health được cập nhật, không phải giờ lịch. |
| actionState | string enum | idle/running/succeeded/timeout/error |
| actionError | string enum | Rỗng nếu idle/running/succeeded; lỗi/timeout theo §6.4. |
| cleanupPending | boolean | Backend chưa giải phóng sau hủy/deadline; boot false, chỉ backend xác minh hoàn tất mới clear. Không đổi health/actionState thành running. |
| resultSequence | uint32 | Tăng mỗi thành công trong một boot, không tăng bởi heartbeat/timeout/disable. |
| resultUpdateMs | uint32 | Uptime lúc thành công; xóa về 0 khi không payload. Giá trị 0 riêng lẻ không chứng minh không payload. |

`actionError` cụ thể hóa lỗi async sau ack, để UI phân biệt không thấy thẻ/scan lỗi/IR quá dài mà không làm health detail sai. Đây là bổ sung kỹ thuật so với bản plan trước.

Ví dụ status đang chờ NFC lần mới, vẫn giữ kết quả trước:

```json
{"module":"pn532","enabled":true,"connected":true,"detail":"ready","output":"{\"kind\":\"nfc_uid\",\"uid\":\"04:AB:01:02\"}","lastUpdateMs":15000,"actionState":"running","actionError":"","cleanupPending":false,"resultSequence":1,"resultUpdateMs":12000}
```

- Metadata action boot: idle, actionError rỗng, cleanupPending=false, sequence=0, resultUpdateMs=0, output rỗng. Disable không reset sequence trong cùng boot; clear output/resultUpdateMs nhưng không giả clear cleanup. Sequence wrap dùng uint32; frontend xét thay đổi, không yêu cầu lớn hơn số cũ.
- Browser validate trường bắt buộc, enum, boolean thật, uint32 nguyên; status sai không ghi đè state. Field thêm không biết được bỏ qua; không ép chuỗi "false" thành true.
- Firmware publish snapshot đủ 5 module mỗi 1 giây. State thay đổi đánh dấu dirty, gộp publish với trần 10 lần/giây/module; không broadcast mỗi vòng loop. Snapshot đầu cho client trong vòng 1 giây từ open.

### 6.2 Command

Các frame request/response trong §6 là ví dụ độc lập, không phải chuỗi thao tác chạy liên tiếp; phản hồi busy chỉ áp dụng khi có action khác đang running.

```json
{"id":17,"module":"wifi","cmd":"action","action":"scan"}
{"id":18,"module":"wifi","cmd":"disable"}
{"id":19,"module":"wifi","cmd":"action","action":"scan"}
```

- `id`: integer 1..4294967295; browser không dùng lại id còn unresolved trong socket hiện tại. Id chỉ có nghĩa cùng client/socket; hai tab có thể dùng cùng số.
- `module`: enum §6.1; `cmd`: enable/disable/action. Action bắt buộc là string không rỗng khi cmd=action; enable/disable không thực hiện field action dư.
- Allowlist action: wifi/scan, pn532/read_uid, ir/capture. CC1101/NRF24 không có action; action đúng kiểu nhưng không hỗ trợ trả unsupported_action, không invalid_command.
- Browser gửi tối đa 512 byte UTF-8 mỗi command. Server nhận một text message hoàn chỉnh; oversized/binary/fragmented/malformed không được thực thi.
- Unknown/missing module/cmd/id, kiểu sai, id=0 hoặc action rỗng → invalid_command. Không enqueue command invalid.
- Enqueue enable/action dùng 8 vị trí thường; disable chỉ dùng slot Stop của module, không dùng slot module khác. Nếu slot tương ứng đầy trả queue_full; tải lệnh thường đầy không ngăn một Stop cho từng category. Không drop, không optimistic báo đã tắt; không auto-retry.

### 6.3 Command result

```json
{"type":"command_result","id":17,"module":"wifi","ok":true,"error":""}
{"type":"command_result","id":18,"module":"wifi","ok":true,"error":""}
{"type":"command_result","id":19,"module":"wifi","ok":false,"error":"busy"}
```

- Chỉ gửi về đúng client đã yêu cầu. Browser xử lý theo type trước, không áp dụng result như status.
- id/module hợp lệ phản hồi đúng giá trị yêu cầu. Nếu request không parse/correlate được: dùng id=0, module="", ok=false, error=invalid_command; browser chỉ log lỗi giao thức, không resolve pending tùy đoán. Frame không hợp lệ không được ghép thành lệnh.
- ok=true enable/disable: state đã được áp dụng. Enable phần cứng lỗi: ok=false/hardware_error nhưng enabled vẫn true, connected=false. Enable lặp không reset state.
- ok=true action: tác vụ đã bắt đầu hoặc hoàn tất ngay; thành công payload vẫn chỉ chứng minh bởi status action/result mới. Timeout/lỗi xảy ra sau đó qua actionState/actionError, không gửi ack lần hai.
- Browser chờ ack 3 giây. Hết thời hạn hoặc socket đóng → “Chưa xác định kết quả lệnh”; không kết luận success/failure, không gửi lại tự động. Ack đến muộn trên đúng socket có thể cập nhật record unresolved; không được làm chạy lại action hoặc ghi đè state bằng optimistic value.
- Sau reconnect, status chỉ chứng minh state hiện tại; không dùng output cũ làm ack cho command trước. Cho thao tác mới khi có status fresh; luôn phản ánh running/busy thật của firmware.

### 6.4 Errors và precedence

Command errors:

| Code | Điều kiện |
|---|---|
| invalid_command | Sai schema/frame/module/cmd/id hoặc thiếu action. |
| unsupported_action | Action đúng kiểu nhưng không thuộc allowlist module. |
| queue_full | Không có chỗ enqueue command hợp lệ. |
| module_off | Action hỗ trợ nhưng module đang off. |
| busy | Action hỗ trợ, module bật và đang running hoặc cleanup chưa xong. |
| hardware_error | Module bật không có health để bắt đầu action hoặc enable thất bại. |

Precedence: validation/frame → unsupported_action → admission vào dung lượng tương ứng; tại dispatch action: off → running/cleanup → hardware health → start. Enable/disable không bị busy; disable không cần health tốt và không chờ backend cleanup xong để ack. State/liveness được đọc ở loop, không truy cập state driver từ callback.

Async `actionError`: scan_failed/scan_timeout/read_timeout/capture_timeout/capture_too_long/hardware_error. Timeout codes đi cùng actionState=timeout; các lỗi còn lại đi cùng error. Disable đặt idle và actionError rỗng; không gửi tác vụ hủy thành kết quả thành công. UI ghi sự kiện hủy từ chuyển running → off/idle.

## 7. Hợp đồng từng chức năng

### 7.1 WiFi scan

- Enable chỉ ready, không scan định kỳ. Scan async theo lệnh; phải giữ AP/dashboard hoạt động, không teardown AP. Không coi comment hiện tại là bằng chứng core giữ AP đúng.
- Tối đa một scan đang chạy. Deadline kỹ thuật 15 giây từ khi nhận khởi động: nếu chưa hoàn tất, action timeout với scan_timeout, không để browser mất kết nối khiến scan treo vô hạn. Deadline này là lựa chọn kỹ thuật cho plan, không phải timeout NFC/IR.
- Scan thất bại → scan_failed; 0 mạng → succeeded với array rỗng. Disable vô hiệu phiên scan và giải phóng buffer an toàn; không mặc định scanDelete là API hủy radio đang chạy. Không nhận kết quả muộn hoặc chồng scan lên phiên core chưa kết thúc.
- Chọn tối đa 32 AP có RSSI mạnh nhất, giữ AP cùng SSID nếu khác BSSID; nếu bằng RSSI, dùng BSSID để thứ tự ổn định. truncated=true nếu bỏ kết quả vì giới hạn, UI ghi rõ giới hạn.
- BSSID uppercase colon-separated; RSSI integer dBm, channel integer, secure boolean. SSID rỗng vẫn giữ rỗng trong dữ liệu; UI hiển thị “Mạng ẩn”. Không dùng dấu phẩy tách SSID.

Output decoded từ string:

```json
{"kind":"wifi_scan","networks":[{"ssid":"Lab AP","bssid":"02:00:00:00:00:01","rssi":-48,"channel":6,"secure":true}],"truncated":false}
```

### 7.2 PN532 UID

- I2C đúng pin/jumper. Enable kiểm tra chip thật; không thấy chip khác không thấy thẻ.
- read_uid chờ tối đa 5 giây, nhận một UID rồi kết thúc; không liên tục thu nhiều thẻ. Chia thao tác đọc thành poll/I/O timeout ngắn để vẫn xử lý disable và WebSocket.
- Không thấy thẻ sau thời hạn → read_timeout, health vẫn ready nếu chip hoạt động; không ghi payload giả.
- UID từ byte thật: uppercase hex, mỗi byte đủ 2 ký tự, ngăn bằng `:`. Không đọc key, memory dump, ghi hoặc clone.

```json
{"kind":"nfc_uid","uid":"04:AB:01:02"}
```

### 7.3 IR capture

- Init RX6; TX7 giữ mapping nhưng không có chức năng truyền/replay đợt này. Không bắt buộc backend RMT cụ thể khi chưa xác minh driver; detail nói init RX không phải physical presence detection.
- Capture chờ tối đa 10 giây, thành công khi nhận một thông điệp không-repeat. Repeat-only tiếp tục chờ đến deadline; không biến repeat thành nút mới.
- Payload tối đa 512 raw timing, đơn vị microsecond. Vượt giới hạn → capture_too_long, không cắt silently, không overwrite kết quả trước.
- protocol là tên decode thật hoặc UNKNOWN; value là chuỗi hex chuẩn hóa `0x` + uppercase hex nếu decode có numeric value thật, null nếu không có. Không chuyển uint64 sang JS Number. rawTimingsUs gồm durations mark/space của thông điệp, không gồm leading idle gap.
- Các trường cần cho export phải được chuẩn hóa khi tạo payload, không serialize con trỏ/raw buffer đã được thư viện tái sử dụng.

```json
{"kind":"ir_capture","protocol":"UNKNOWN","value":null,"rawTimingsUs":[9000,4500,560,560]}
```

### 7.4 CC1101/NRF24 và kiểm tra health

- Shared SPI khởi tạo một lần, CS tách biệt; không phát carrier, không gửi packet thử lên môi trường để chứng minh connected.
- CC1101 identity/liveness theo register/driver semantics đã xác minh với thư viện/chip đang dùng; không kiểm tra máy móc “mọi register phải non-zero”. NRF24 kiểm tra chip thật, không chỉ tạo object thành công.
- PN532 kiểm tra firmware/chip response. IR chỉ kiểm tra trạng thái driver có thể biết; không hứa phát hiện việc rút cảm biến IR qua handshake không tồn tại.
- Liveness module bật có deadline quan sát 2 giây để phản ánh chip mất/phục hồi, trừ IR physical presence không đo được. Plan chọn nhịp và API I/O có timeout để đáp ứng; không auto action khi phục hồi.

### 7.5 RGB health

- Aggregate đủ 5 category; chỉ xét category enabled.
- Không category nào enabled → LED tắt.
- Có category enabled và tất cả category enabled connected → xanh.
- Ít nhất một category enabled không connected → vàng.
- Không dùng trạng thái capture/read timeout như lỗi health khi chip vẫn connected. Module off connected stale không được làm LED healthy. Chỉ ghi LED khi mức aggregate đổi.

## 8. Dashboard, timestamp, log và export

### 8.1 Transport và UI

- Transport states connecting/live/reconnecting/offline. Chỉ một socket và một reconnect timer; backoff 1/2/4/8 giây, trần 8 giây, reset sau open.
- Socket URL theo location protocol ws/wss; không cố định hostname/gateway. Không auto mock hoặc dữ liệu ngẫu nhiên khi không có ESP32.
- Socket đóng hoặc không có status module trong 5 giây → stale, giữ dữ liệu cũ và khóa thao tác thiết bị. Không đổi connected thành false để giả lỗi phần cứng; khi trở lại chỉ unlock module có status fresh.
- `receivedAt` dùng monotonic clock browser cho tuổi status. `receivedAtIso` là wall-clock browser lúc nhận payload mới/snapshot đầu; không đổi bởi heartbeat. Không chuyển millis ESP32 thành Date; không so sánh uptime và Date.now để tính tuổi.
- Frontend phát hiện kết quả mới bằng sequence thay đổi; reconnect phải đối chiếu cả payload/resultUpdateMs và reset freshness. ESP32 reboot/off xóa payload phải được phản ánh ngay, không giữ bản sao cũ như payload thiết bị hiện tại. Resync đầu tiên ghi rõ thời điểm nhận snapshot, không khẳng định thời điểm thu.
- Trình bày health, tác vụ đang chạy/lỗi/timeout và payload cũ tách biệt. Transport live không tự đồng nghĩa có dữ liệu fresh cho cả 5 category.
- Overview thống kê off/ready/error/stale; detail chọn module và action phù hợp; không hiện nút action chưa có backend.
- Dữ liệu từ thiết bị/SSID/detail dùng DOM textContent hoặc escaping kiểm soát; không interpolate vào innerHTML. Payload malformed không execute và không thay kết quả đã validate.
- Nhãn tiếng Việt thống nhất, button thật, focus nhìn thấy, laptop dùng Tab/Enter được; trạng thái không chỉ dựa màu.
- Laptop 768px/1280px: mọi thao tác/log/export đầy đủ. Điện thoại 360px: xem Overview/status/kết quả/log không overflow toàn trang; bảng được scroll trong panel. Không bắt buộc nghiệm thu điều khiển/export trên điện thoại.

### 8.2 Log

- Browser RAM tối đa 200 event FIFO; lọc theo module, mới nhất hiển thị trước.
- Event khi health/enabled/actionState/actionError/cleanupPending/resultSequence/payload thay đổi hoặc transport/command có sự kiện. Heartbeat chỉ đổi timestamp không là event; payload giống nhau nhưng sequence mới vẫn là lần thu mới.
- Clear chỉ xóa log browser; không disable, không xóa payload. Log browser còn sau disable module, mất khi reload/đóng trang. Không gọi log browser là lịch sử bền vững trên thiết bị.

### 8.3 Export

- Hai nút riêng “Xuất log” và “Xuất kết quả”; tải JSON bằng browser, không thêm endpoint lưu payload vào filesystem.
- Export log chứa exportedAtIso và array events theo thứ tự từ cũ đến mới; mỗi event có receivedAtIso, module hoặc null cho transport, loại sự kiện và nội dung.
- Export kết quả của module được chọn: module, enabled, connected, actionState, actionError, cleanupPending, resultSequence, resultUpdateMs, receivedAtIso, exportedAtIso, stale, previousResult và payload decoded theo §7. previousResult=true khi đang running hoặc action mới lỗi/timeout mà có payload trước.
- Không payload → khóa “Xuất kết quả”, không tạo file kết quả rỗng/giả. Có payload nhưng stale/offline vẫn cho export với nhãn đúng; download không là thao tác thiết bị.
- Không dùng giờ browser làm giờ thu trên ESP32. Chuỗi Unicode/nháy/BSSID/UID/raw timing phải giữ nguyên nghĩa khi mở JSON; object URL được revoke sau tải.

## 9. Giới hạn và các trường hợp biên

| Giới hạn | Giá trị/hành vi |
|---|---|
| Inbound command | 512 byte UTF-8; không phải giới hạn outbound status/payload. |
| Command queue | 8 lệnh thường + 5 Stop riêng theo category = tối đa 13, FIFO chung; đầy slot tương ứng trả queue_full. |
| Pending ack | 3 giây; unknown outcome, không auto-retry. |
| Status snapshot | Đủ 5 module mỗi 1 giây. |
| Dirty publish | Trần 10 lần/giây/module, coalesce giữa các thay đổi. |
| Stale | 5 giây không nhận status module hoặc socket đóng. |
| Reconnect | 1/2/4/8 giây, trần 8 giây. |
| WiFi result | 32 AP; truncated rõ, không gộp theo SSID. |
| WiFi action deadline | Mục tiêu ban đầu 15 giây; scan_timeout nếu chưa hoàn tất. Phải đo trên board trước nghiệm thu, không tự nới. |
| NFC action deadline | 5 giây; read_timeout nếu không có thẻ. |
| IR action deadline | 10 giây; capture_timeout nếu không có thông điệp không-repeat. |
| IR raw buffer | 512 timing; lỗi khi vượt, không cắt silently. |
| Browser event log | 200 event FIFO. |
| Chip liveness | Mục tiêu ban đầu 2 giây khi bật; đo mất/phục hồi chip, không áp dụng physical IR presence; thay đổi phải được duyệt. |

- Deadline so bằng elapsed uint32 wrap-safe; không so `millis() >= start + timeout` như tuyệt đối. Sequence/uptime wrap không được làm UI mất kết quả mới hoặc suy ra ngày lịch.
- Queue full, chip missing, malformed JSON và oversized IR không được làm treo loop hoặc tràn buffer.
- Action bị hủy/deadline hết mà backend chưa giải phóng: vô hiệu phiên cũ, cleanup an toàn và publish cleanupPending=true. Có thể cleanup khi off nhưng không khởi động action/liveness mới. Action khi bật mà cleanup chưa xong trả busy, UI khóa nút và hiện nhãn; chỉ clear flag sau backend xác minh hoàn tất. Không phải auto-retry.
- Không có allocation/serialization/copy sâu avoidable ở mỗi poll; reserve/bound kết quả, không tăng bộ nhớ vô hạn theo số scan/tab/log.

## 10. Nghiệm thu và bằng chứng

Các AC dưới đây là điều kiện hoàn thành, không phải checklist đã chạy. Mỗi AC cần ghi pass/fail/chưa có thiết bị cùng bằng chứng; thiếu hardware không được coi là pass.

| ID | Kịch bản bắt buộc | Kết quả mong đợi |
|---|---|---|
| AC01 | Boot board, mở browser AP; chọn đủ 5 category. | Boot off, không action nền/mock; action đúng allowlist và không có nút giả ngoài phạm vi. |
| AC02 | Enable từng module, thiếu chip, cắm lại; CC1101/NRF24 cắm đồng thời; đo thời gian health. | Health thật, lỗi giữ enabled, phục hồi không auto action; SPI không xung đột, IR không giả presence. Ghi phép đo so với mục tiêu 2 giây; nếu không đạt, trình bằng chứng/đề xuất sửa spec, không tự nới hoặc pass giả. |
| AC03 | Hai tab cùng id; 8 lệnh thường queued; Stop cả 5 category; Stop lặp; action running/cleanup; disable/enable khi cleanup. | FIFO chung, ack đúng client; 8 lệnh thường không ngăn 5 Stop, lặp không chiếm slot khác; busy không chạy muộn. Stop ack sau hủy logic/xóa, cleanup có nhãn ở mọi tab, enable không xóa flag, completion cũ không sống lại. |
| AC04 | Đóng browser cuối trong action; mở lại; mất/kết nối lại socket. | Module/action không bị tự dừng; UI stale/reconnect, snapshot thật ≤1 giây sau open, không replay command cũ. |
| AC05 | Làm mất/chậm ack trên test transport; quan sát status/payload cũ. | Sau 3 giây unknown outcome, không auto-retry, output cũ không chứng minh action mới; late ack chỉ cập nhật đúng record. |
| AC06 | Scan thật, AP trùng SSID khác BSSID, tên nháy/Unicode, 0 mạng/lỗi/deadline/disable; đo scan. | Một scan/lệnh, AP còn hoạt động, đúng cột/metadata, tối đa 32/truncated, 0 mạng khác lỗi, bỏ completion muộn. Đo mục tiêu deadline 15 giây; không đạt thì trình bằng chứng/đề xuất sửa spec, không tự nới. |
| AC07 | Hai thẻ UID khác nhau; cùng thẻ đọc hai lần; không thẻ 5 giây; PN532 mất; disable khi chờ. | UID byte thật, sequence tăng dù UID giống; timeout khác hardware_error; cancel không giữ payload thiết bị. |
| AC08 | Hai nút remote khác nhau; repeat-only; UNKNOWN; 10 giây không tín hiệu; quá 512 timing; disable. | Một thông điệp/action, repeat không là kết quả mới, raw đúng đơn vị, timeout/overflow không overwrite payload trước, không truyền IR. |
| AC09 | Thành công → chạy lại → lỗi/timeout → thành công giống trước → disable/re-enable; heartbeat. | Kết quả trước có nhãn, metadata không trẻ hóa; mỗi thành công tăng sequence; disable xóa và completion muộn không khôi phục. |
| AC10 | Xuất log và kết quả WiFi/NFC/IR; Clear log; export stale/offline. | Hai file có schema/metadata đúng, payload nguyên nghĩa, Clear không xóa payload; không ghi payload LittleFS, không giả giờ thu. |
| AC11 | Laptop 768px/1280px, Tab/Enter; phone 360px; markup; >200 event; RGB; cleanup ở hai tab. | Laptop đủ thao tác, phone xem được, DOM an toàn, log bounded/không heartbeat spam, RGB đúng health; nhãn cleanup không giả tác vụ cũ đang chạy và action khóa đến cleanup xong. |
| AC12 | Native tests + ESP32 build + demo board; hai tab 10 phút; lưu bằng chứng/timing. | Board chứng minh chức năng/lỗi cơ bản/hủy; injection chứng minh edge protocol/boundary có nhãn riêng. Checklist + ảnh/kết quả xuất + log; không heap giảm dần, không video bắt buộc. Mốc 15 giây/2 giây cần đo hoặc spec sửa được duyệt, không pass chỉ vì build/tests. |

**Quy tắc kiểm tra:**

- Giữ native tests hành vi còn phù hợp; loại test chỉ echo/copy field hoặc pin wording/implementation khi chạm seam đó. Permanent tests mới chỉ cho hành vi/boundary/transition/precedence có thể gây lỗi người dùng; không dùng wiring/source text để thay smoke lifecycle.
- Board thật bắt buộc cho scan/read/capture, lỗi cơ bản không thẻ/không tín hiệu/mất chip và hủy. Injection được dùng cho ack loss/malformed/escaping, queue saturation và boundary khó tạo (IR >512/scan failure); ghi rõ không chứng minh radio/thẻ/IR thật. Cleanup fixture sau smoke.
- Plan phải có smoke thật cho luồng đã đổi, regression fail-before/pass-after khi khả thi, build ESP32 và covering tests; không hứa “đã hoạt động” khi chưa có bằng chứng.
- Heap đầu/cuối từ Serial trong 10 phút: nếu giảm dần qua các vòng tác vụ hoặc có reset thì điều tra và sửa trước nghiệm thu, không lấy một lần scan thành công làm bằng chứng ổn định.
- Driver API/core WiFi/liveness identity phải được xác minh trong bước triển khai với dependency thực cài; không hardcode API/backend theo suy đoán.

Lệnh nghiệm thu dự kiến, chạy từ thư mục firmware; upload chỉ khi được giao triển khai và có board đúng:

```sh
pio test -e native
pio run -e attak-iot-firmware
pio run -e attak-iot-firmware -t upload
pio run -e attak-iot-firmware -t uploadfs
pio device monitor
```

## 11. Handoff sang plan

1. Dùng file này làm spec duy nhất của đợt; đọc Q1–Q21/traceability và AC trước khi chia task.
2. Cập nhật plan đã có, không tạo kế hoạch cạnh tranh. Mỗi R01–R21 và AC01–AC12 map tới task/test/smoke cụ thể.
3. Đồng bộ actionError/cleanupPending, queue 8+5/FIFO, target WiFi 15 giây/liveness 2 giây cần đo, precedence lỗi, id theo client và schema export. Baseline không đạt target phải trình bằng chứng/xin duyệt sửa spec, không tự nới hoặc pass giả.
4. Map file/callers trước khi đổi API module; quyết định return/status ownership đủ để báo busy/hardware_error mà không copy payload mỗi loop. Không giữ interface void nếu phải suy đoán outcome của command.
5. Tickets 05–08 là tiêu chí cũ; plan phải thay tiêu chí bằng lifecycle off/on và AC của spec này trước khi thực thi. Ticket 09 phải bỏ giả định aggregate mọi chip kể cả off.
6. Giữ phần cứng/pin/license/config hiện có; không bổ sung dependency hoặc refactor ngoài đường đi của chức năng này.
7. Điều kiện nghiệm thu: ESP32-S3-N16R8, 4 module đúng pin, hai thẻ thử khác UID, remote, AP thử có thể phân biệt BSSID, cổng Serial và laptop. Thiếu thiết bị phải ghi “chưa nghiệm thu phần cứng”, không hoàn tất giả.
8. Không còn câu hỏi sản phẩm mở trong phạm vi đã chốt. Các lựa chọn implementation được giải bằng code/dependency/hardware evidence ở bước plan/triển khai; không mở lại roadmap hoặc thêm chức năng ngoài phạm vi.

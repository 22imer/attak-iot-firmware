# REVIEW.md — Rubric nghiệm thu việc của subagent

> Dùng để review mỗi ticket trong [`PLAN.md`](PLAN.md) trước khi merge. Reviewer
> (người hoặc một subagent review độc lập) đi qua **cổng toàn cục §1**, rồi
> **checklist theo loại ticket §2**, ghi kết quả theo **mẫu §4**. Bất kỳ mục
> 🔴 Blocker nào chưa đạt → **không merge**.

---

## 1. Cổng toàn cục (mọi ticket đều phải qua)

### 1.1. Build & test 🔴
- [ ] `pio test -e native` xanh, gồm test mới của ticket.
- [ ] `pio run -e attak-iot-firmware` build pass.
- [ ] File logic portable mới đã được thêm vào `build_src_filter` của env `native`
      trong `platformio.ini` (nếu không, test không thực sự build nó).
- [ ] Không hồi quy: test Phase A (`test_ws_command_json`, `test_action_catalog`,
      `test_module_runtime`, …) vẫn pass.

### 1.2. License / tham khảo Bruce 🔴
- [ ] KHÔNG có đoạn mã copy nguyên từ Bruce (AGPL-3.0). Kiểm tra: tên biến/cấu trúc
      hàm trùng khít bất thường, comment/See-also dẫn sang mã Bruce, khối lớn giống
      hệt. Nghi ngờ → yêu cầu viết lại.
- [ ] Có ghi chú file/khu vực Bruce đã tham khảo (học, không copy).
- [ ] Không thêm lib companion AGPL (PN532Killer/Chameleon/whywilson…).

### 1.3. Hợp đồng kiến trúc (PLAN §3) 🔴
- [ ] Payload khai báo qua `ActionDescriptor` trong `action_catalog.cpp`; parser KHÔNG
      bị thêm allowlist tay.
- [ ] `ActionId` mới chỉ thêm vào cuối enum (không chèn giữa — vỡ giá trị số).
- [ ] Dùng `ModuleRuntime` cho vòng đời; không có state machine action song song tự chế.
- [ ] Không chặn `loop()`: thao tác chip có budget/deadline, không busy-wait vô hạn.
- [ ] Record/replay dùng buffer RAM (T03); không ghi LittleFS ngoài ý định.
- [ ] Action `radioExclusive` đi qua radio arbiter (T04); module KHÔNG tự tắt AP.

### 1.4. Bảo mật & an toàn 🔴
- [ ] Dữ liệu thiết bị trên dashboard chỉ qua `textContent`/prop `text` (không `innerHTML`).
- [ ] Không phơi file nhạy cảm qua web (giữ guard `/config.json`).
- [ ] Action `disruptive`: descriptor + handler bọc `#ifdef ENABLE_DISRUPTIVE`; UI có
      confirm theo `tier`; mặc định không bật.
- [ ] Lệnh có `params`: validate kiểu + biên; không tin dữ liệu client.

### 1.5. Giao thức & tương thích 🟠
- [ ] Frame mới (`action_output`…) có `type` rõ, được validate ở client trước khi dùng.
- [ ] Wire id action ổn định (không đổi id của action đã phát hành).
- [ ] `command_result`/ack vẫn đúng client/session.

## 2. Checklist theo loại ticket

### 2.1. Ticket framework (T01–T04)
- [ ] T01: param sai kiểu/thiếu/biên → lỗi đúng mã; action không param vẫn chạy; test đủ nhánh. 🔴
- [ ] T02: start→stream→stop sạch; disable giữa chừng = stop+cleanup; không rò resource. 🔴
- [ ] T03: Replay khi buffer rỗng bị từ chối; tắt module xóa buffer; Record ghi đè đúng. 🔴
- [ ] T04: sau action độc quyền, AP khôi phục + dashboard tự reconnect; không chạy song
      song 2 action chung SPI bus; state machine có test native. 🔴

### 2.2. Ticket OneShot (rf_scan, rf_custom_tx, nfc_*, ir_tvbgone, ir_custom_tx)
- [ ] Trả đúng 1 kết quả, có payload JSON hợp lệ; timeout/err map đúng `ActionState`.
- [ ] Renderer dashboard riêng cho payload; hiển thị kết quả cũ có nhãn khi chạy lại.
- [ ] Format payload (hex UID, timing, mã IR…) có test native.

### 2.3. Ticket Record/Replay (rf_record/replay, ir_replay)
- [ ] Record lưu RAM; Replay đọc đúng buffer; `needsBuffer` chặn khi rỗng.
- [ ] Replay chỉ phát trên thiết bị của mình (tier `active_own`); không auto-loop ngoài ý muốn.

### 2.4. Ticket Continuous (rf_spectrum, nrf_scan, wifi_sniff)
- [ ] Stream qua `action_output`; Stop dừng sạch; tần suất/payload nhẹ (không flood WS).
- [ ] Đi qua arbiter nếu `radioExclusive`.

### 2.5. Ticket Disruptive (rf_jammer, nrf_jammer, wifi_beacon, wifi_deauth, wifi_evil_portal) 🔴
- [ ] Bọc `#ifdef ENABLE_DISRUPTIVE`; build mặc định (env `attak-iot-firmware`,
      không định nghĩa) KHÔNG chứa chúng trong catalog lẫn binary; `-lab`/native bật cờ.
- [ ] UI confirm trước khi gửi; log ghi rõ.
- [ ] `rf_jammer`/`nrf_jammer` qua arbiter `SharedSpi` (không chồng CC1101/NRF24).
      WiFi dùng **AP hiện tại** (`WIFI_IF_AP`, không teardown) theo PLAN §2.4 — không
      cần khôi phục AP sau khi dừng; `wifi_sniff` vẫn là action exclusive cũ.
- [ ] Kênh điều khiển dự phòng bằng Serial console đủ để Dừng khi AP bị chiếm.
- [ ] README/disclaimer nhắc chỉ dùng hợp pháp/được phép (ISSUE #16).

## 3. Red flags (thấy là trả lại)

- Khối mã dài giống hệt Bruce, hoặc `#include` lib AGPL companion.
- `while(...)` đọc chip không có lối thoát theo thời gian → treo `loop()`.
- `innerHTML`/`insertAdjacentHTML` với dữ liệu thiết bị.
- Module tự gọi `WiFi.softAP`/teardown thay vì qua arbiter.
- Action mới validate bằng allowlist tay trong parser thay vì catalog.
- Disruptive không có build flag hoặc không có confirm.
- Ghi LittleFS cho buffer record khi ticket nói RAM-only.
- Thêm test nhưng QUÊN thêm file vào `build_src_filter` (test không build file đó).

## 4. Mẫu kết quả review (reviewer điền)

```
## Review <Txx> — <action id>  (commit <hash>)
Verdict: ✅ merge | 🔧 cần sửa | ⛔ trả lại

Cổng toàn cục:
- Build/test: …
- License: …
- Kiến trúc: …
- Bảo mật: …
- Giao thức: …

Checklist loại ticket (<loại>): …

Findings (mức độ · vị trí · mô tả · đề xuất):
1. 🔴 … 
2. 🟠 …
3. 🟡 …

Hardware AC còn nợ (defer): …
```

## 5. Phần không kiểm được nếu thiếu phần cứng

Nếu reviewer không có board, các AC phần cứng (phát RF/IR thật, deauth/jam thật,
đo mốc thời gian, heap 10 phút) được **ghi nợ rõ ràng** và chỉ nghiệm thu khi có
board — KHÔNG đánh dấu pass dựa trên suy đoán. Phần logic portable vẫn phải pass
đầy đủ qua test native.

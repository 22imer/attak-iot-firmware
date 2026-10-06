# ATTAK-IOT Firmware — Intent

## Mục tiêu

Xây dựng một công cụ pentest IoT đa năng: sniff/replay RF (sub-GHz), đọc/clone NFC, capture/replay IR, sniff/jam 2.4GHz — điều khiển qua **web dashboard**, không cần màn hình vật lý (headless).

Dự án firmware **riêng**, độc lập — chỉ tham khảo kiến trúc/cách làm của Bruce Device firmware (`firmware/` trong repo này, AGPL-3.0), không fork, không copy code AGPL của Bruce.

## Phần cứng

- Board chính: **ESP32-S3-N16R8** (16MB flash / 8MB octal PSRAM, dual USB-C)
- Module rời đã mua sẵn: CC1101 (sub-GHz RF), NRF24L01 (2.4GHz), PN532 (NFC, jumper I2C/SPI — dùng chế độ I2C), IR RX/TX rời (TSOP + LED)
- Không thiết kế/fab PCB tùy chỉnh — đấu dây module rời vào board có sẵn

## Kiến trúc đã chốt

- Framework: PlatformIO + Arduino
- Kết nối: USB NCM trên cổng native, dashboard HTTP/WebSocket tại `192.168.7.1` cho laptop Windows 11; DHCP tự động, không cần WiFi AP. AP dự phòng tắt mặc định, chỉ bật bằng Serial `ap on`.
- CC1101 + NRF24 dùng chung 1 SPI bus; PN532 qua I2C riêng
- Pin mapping đầy đủ: xem [Decisions so far trong map](map.md)
- License: chấp nhận GPL-2.0 (do dùng lib RF24 cho NRF24) — dự án cá nhân/bài tập lớn, phi thương mại

## Phạm vi

**v1 baseline**: scaffold/UI và JSON codecs đã có; bốn driver module rời còn stub trong lần khảo sát, không coi “đã ship” là bằng chứng kết nối phần cứng thật.

**v2 roadmap lịch sử**: mở rộng payload cho 5 category theo [map.md § v2](map.md); không đồng nghĩa toàn bộ roadmap thuộc đợt hiện tại.

**Đợt hiện tại đã chốt**: hoàn thiện dashboard và chức năng quan sát: WiFi scan, NFC UID, IR capture, kiểm tra CC1101/NRF24, RGB/log/export; nghiệm thu đầu-cuối trên board thật. Scope và hợp đồng chính thức tại [spec.md](spec.md); replay và các tính năng roadmap còn lại tách đợt riêng.

## Trạng thái lập kế hoạch

[`spec.md`](spec.md) đã cập nhật theo Q1–Q21, trạng thái **approved; implementation-plan-ready**. Plan duy nhất tại `docs/superpowers/plans/2026-10-04-dashboard-completion.md` (10 task, map đủ R01–R21/AC01–AC12) đã được triển khai trong mã nguồn: host verification xanh (native 50/50, build ESP32 + ảnh LittleFS, browser smoke). **Chưa flash và chưa nghiệm thu phần cứng** (AC02/AC06/AC07/AC08/AC11 phần board, mốc 15 giây/2 giây, heap 10 phút).
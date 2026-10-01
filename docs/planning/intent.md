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
- Kết nối: WiFi AP mode (giống `BruceNet`), dashboard qua WebSocket
- CC1101 + NRF24 dùng chung 1 SPI bus; PN532 qua I2C riêng
- Pin mapping đầy đủ: xem [Decisions so far trong map](map.md)
- License: chấp nhận GPL-2.0 (do dùng lib RF24 cho NRF24) — dự án cá nhân/bài tập lớn, phi thương mại

## Phạm vi

**v1 (đã ship)**: driver skeleton cho cả 4 module — kết nối được + hiển thị trạng thái qua dashboard.

**v2 (đang làm)**: mở lại "tính năng pentest cụ thể" đã defer ở v1 — 5 category tấn công (RF record+replay, NRF24 jammer, PN532 đọc UID, IR capture+replay, WiFi scan — dùng sóng onboard ESP32-S3, category thứ 5 ngoài 4 module gốc), đổi kiến trúc polling sang model bật/tắt theo yêu cầu, trang dashboard mới "Attacks". Chi tiết quyết định: [map.md § v2](map.md).

## Trạng thái lập kế hoạch

Đang chạy qua `/wayfinder` — map và các quyết định chi tiết tại [`docs/planning/map.md`](map.md). Ticket mở: kiến trúc project/scaffold, prototype UI dashboard.
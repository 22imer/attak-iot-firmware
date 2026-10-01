Type: research
Status: resolved

## Question

Xác định pin mapping GPIO không xung đột trên ESP32-S3 DevKitC-1 (N8R2/N16R8) cho:

1. CC1101 + NRF24 chia sẻ 1 SPI bus (SCK/MISO/MOSI chung; CS + GDO0/GDO2 riêng cho CC1101; CE + CS riêng cho NRF24) — theo pattern `acquireSPIBus` của Bruce ở `firmware/boards/m5stack-sticks3/` và `firmware/boards/reaper/`.
2. PN532 qua I2C (SDA/SCL).
3. IR RX/TX (2 GPIO) — xác nhận IRremoteESP8266 (bản fork Bruce dùng) hỗ trợ đầy đủ RMT peripheral trên ESP32-S3 cho cả RX lẫn TX; nếu không, đề xuất lib thay thế.
4. 1 status LED (tuỳ chọn, dùng chân onboard nếu DevKitC-1 có sẵn).

Tham khảo pinout chính thức Espressif cho ESP32-S3-DevKitC-1. Tránh strapping pins (GPIO0, 3, 45, 46), tránh USB-CDC pins (19/20) nếu firmware dùng USB debug, tránh SPI flash/PSRAM pins theo biến thể N8R2/N16R8 nếu khác nhau giữa 2 bản.

Output mong muốn: bảng pin mapping đầy đủ (module → GPIO), kèm nguồn trích dẫn (datasheet/pinout chính thức).

## Answer

Board xác định lại là **ESP32-S3-N16R8** (16MB flash/8MB octal PSRAM), không phải Espressif DevKitC-1 chính hãng và không phải "YD-ESP32-S3" (nhận diện tạm thời ban đầu, sau đó bị thay bằng ảnh product-listing chính xác hơn) — dựa trên ảnh product-listing board người dùng cung cấp. Bảng pin mapping đầy đủ + nguồn trích dẫn: [research/01-pin-mapping-findings.md](../research/01-pin-mapping-findings.md).

Tóm tắt: SPI chung (SCK=12, MOSI=11, MISO=13), CC1101 (CS=10, GDO0=8), NRF24 (CS=14, CE=9), PN532 I2C (SDA=4, SCL=5), IR (RX=6, TX=7), status dùng RGB LED có sẵn onboard tại **GPIO48** (sửa từ GPIO47 sau khi xác nhận board thật). Né GPIO0/3/45/46 (strapping), GPIO19/20 (USB native), GPIO43/44 (UART flash qua CH343), GPIO26/33/34/35/36/37 (có thể bị PSRAM octal chiếm dụng nội bộ).

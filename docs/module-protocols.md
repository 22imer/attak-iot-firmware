# Phân loại Module và Giao thức Sóng Truyền Dữ liệu (attak-iot-firmware)

Tài liệu này giải thích chi tiết bản chất vật lý, dải tần số, loại sóng điện từ, phương pháp điều chế và giao thức truyền dữ liệu tương ứng của từng module phần cứng được tích hợp trên thiết bị **attak-iot-firmware** (nền tảng vi điều khiển ESP32-S3-N16R8).

---

## 1. Bảng Tổng Hợp Phổ Sóng và Giao Thức

| Module | Tần số / Bước sóng | Loại sóng | Phương thức điều chế | Giao thức / Chuẩn truyền dữ liệu | Thiết bị thực tế tương đương |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **CC1101** | Sub-1 GHz<br>(315, 433.92, 868, 915 MHz) | Sóng vô tuyến RF tầm trung–xa | ASK, OOK, 2-FSK, GFSK, 4-FSK, MSK | Fixed Code (PT2262, EV1527), Rolling Code (Keeloq), Wireless M-Bus, Oregon Scientific | Chìa khóa ô tô (RKE), điều khiển cửa cuốn, cổng tự động, chuông không dây, cảm biến trạm thời tiết |
| **NRF24L01+** | 2.400 – 2.525 GHz (126 kênh) | Sóng vô tuyến RF vi ba (Microwave) | GFSK (250 kbps, 1 Mbps, 2 Mbps) | Nordic Enhanced ShockBurst™ (ESB), giao thức độc quyền proprietary 2.4GHz | Chuột/bàn phím không dây (Logitech Unifying), tay cầm Drone/Flycam RC, đồ chơi điều khiển từ xa |
| **PN532** | 13.56 MHz (HF) | Sóng điện từ trường gần (Near-Field Magnetic Induction) | ASK 10% / 100%, Manchester, Modified Miller | ISO/IEC 14443 Type A/B, ISO/IEC 18092 (NFCIP-1), ISO 15693, FeliCa, MIFARE Classic/Ultralight | Thẻ thang máy, thẻ gửi xe, thẻ phòng khách sạn, thẻ căn cước e-ID, vé xe buýt/Metro, Apple Pay / Google Wallet |
| **IR (Hồng ngoại)** | 940 nm (Sóng mang 36 – 56 kHz, chuẩn 38 kHz) | Sóng quang học hồng ngoại gần (Near-Infrared - NIR) | Điều chế độ rộng xung (PWM), khoảng cách xung (PDM), Bi-Phase | NEC, Sony SIRC, Philips RC-5 / RC-6, Samsung, Sharp, Kaseikyo, Raw Timing | Remote TV, điều khiển điều hòa / máy lạnh, quạt điện, máy chiếu, loa thanh |
| **Wi-Fi / BLE** *(ESP32-S3)* | 2.412 – 2.484 GHz (Wi-Fi)<br>2.402 – 2.480 GHz (BLE) | Sóng vô tuyến RF vi ba | DSSS, CCK, OFDM (BPSK, QPSK, 16/64-QAM); GFSK cho BLE | IEEE 802.11 b/g/n (Wi-Fi 4), Bluetooth 5.0 Low Energy (GAP/GATT/Advertising) | Router Wi-Fi gia đình, smartphone, laptop, smartwatch, thiết bị nhà thông minh Smart Home |

---

## 2. Chi Tiết Từng Module Phần Cứng

### 2.1. Module CC1101 — Sóng Vô Tuyến Tần Số Dưới 1 GHz (Sub-GHz RF)

* **Loại sóng:** Sóng vô tuyến tần số thấp/trung (Sub-1 GHz Radio Frequency). Sóng dải này có bước sóng dài (khoảng 30 cm – 1 m), khả năng đâm xuyên tường và vật cản tốt, ít bị suy hao trong môi trường dân dụng hơn dải 2.4 GHz.
* **Dải tần hỗ trợ:**
  * 300 – 348 MHz (phổ biến: 315 MHz tại Mỹ/Châu Á cho remote xe hơi, cửa cuốn).
  * 387 – 464 MHz (phổ biến nhất: **433.92 MHz** dải ISM miễn cấp phép tại Việt Nam/Châu Âu).
  * 779 – 928 MHz (phổ biến: 868 MHz tại Châu Âu, 915 MHz tại Mỹ).
* **Cơ chế điều chế (Modulation):**
  * **OOK / ASK (On-Off Keying / Amplitude Shift Keying):** Bật/tắt biên độ sóng mang theo bit 0 và 1. Đơn giản, cực kỳ phổ biến trong các thiết bị giá rẻ.
  * **2-FSK / GFSK / 4-FSK:** Dịch chuyển tần số có lọc Gaussian giúp giảm băng thông lân cận, chống nhiễu tốt.
* **Giao thức truyền dữ liệu:**
  * **Fixed Code (Mã cố định):** Sử dụng các IC mã hóa cổ điển như PT2262, PT2272, EV1527, HS1527, SC2260. Mỗi lần bấm nút, thiết bị phát cùng 1 chuỗi bit (thường 24-bit). Rất dễ bị tấn công Replay (bắt lại và phát lại).
  * **Rolling Code / Hopping Code:** Sử dụng giải thuật mã hóa (như Microchip HCS301 Keeloq). Mỗi lần bấm, một counter nội bộ tăng lên và băm ra một mã hoàn toàn mới nhằm chống Replay Attack.
* **Thiết bị tương đương trong thực tế:**
  * Remote điều khiển cửa cuốn nhà ở, cổng tự động.
  * Chìa khóa điều khiển từ xa ô tô (RKE - Remote Keyless Entry dạng nút bấm).
  * Chuông cửa không dây gia đình.
  * Cảm biến đo thời tiết không dây (nhiệt độ, độ ẩm ngoài trời truyền về trạm trung tâm).
  * Đồng hồ công tơ điện/nước không dây AMR (Wireless M-Bus).

---

### 2.2. Module NRF24L01+ — Sóng Vô Tuyến Vi Ba 2.4 GHz Băng Thông Cao

* **Loại sóng:** Sóng vô tuyến dải siêu cao tần vi ba (Microwave RF) trong băng tần ISM 2.4 GHz (2400 – 2525 MHz).
* **Phân kênh:** Chia thành 126 kênh RF độc lập (khoảng cách 1 MHz mỗi kênh, từ kênh 0 đến 125).
* **Cơ chế điều chế:**
  * **GFSK (Gaussian Frequency Shift Keying):** Tốc độ dữ liệu cấu hình được gồm 250 kbps, 1 Mbps hoặc 2 Mbps.
* **Giao thức truyền dữ liệu:**
  * **Nordic Enhanced ShockBurst™ (ESB):** Giao thức đóng gói phần cứng chuyên dụng của hãng Nordic Semiconductor. Hỗ trợ tự động kiểm tra lỗi CRC (8-bit hoặc 16-bit), tự động gửi phản hồi xác nhận (Auto-ACK), tự động truyền lại gói tin khi rớt (Auto-Retransmit), và địa chỉ hóa mạng (multiceiver hỗ trợ tới 6 data pipe).
  * **Proprietary RF:** Rất nhiều hãng sản xuất sử dụng chip NRF24 hoặc chip tương thích (BK2425, LT8900) với giao thức riêng không theo chuẩn Wi-Fi hay Bluetooth.
* **Thiết bị tương đương trong thực tế:**
  * Chuột và bàn phím không dây dùng đầu cắm USB Nano Receiver (như chuẩn Logitech Unifying 2.4GHz không Bluetooth).
  * Tay cầm điều khiển máy bay không người lái (Drone, Quadcopter, máy bay mô hình RC dùng giao thức Bayang, Syma, V2x2).
  * Tay cầm chơi game không dây cho PC/Console thế hệ cũ.
  * Các mạng cảm biến công nghiệp truyền số liệu tốc độ cao ở khoảng cách ngắn dưới 100m.

---

### 2.3. Module PN532 — Sóng Điện Từ Cảm Ứng Trường Gần (NFC / RFID HF)

* **Loại sóng:** Không truyền bức xạ không gian dạng sóng radio truyền thống mà truyền qua **cảm ứng điện từ trường gần (Near-Field Magnetic Induction)** ở tần số cao HF (High Frequency) **13.56 MHz**.
* **Đặc tính vật lý:**
  * Tầm hoạt động rất ngắn (dưới 4 – 10 cm).
  * Cuộn cảm trên thẻ đóng vai trò cuộn thứ cấp của máy biến áp, nhận năng lượng không dây từ đầu đọc qua hiện tượng tự cảm (đối với thẻ thụ động - Passive Tag).
* **Cơ chế điều chế:**
  * **ASK (Amplitude Shift Keying):** Điều chế biên độ 10% hoặc 100%.
  * Mã hóa bit: NRZ, Manchester, hoặc Modified Miller.
* **Tiêu chuẩn & Giao thức:**
  * **ISO/IEC 14443 Type A & Type B:** Chuẩn thẻ thông minh không tiếp xúc phổ biến nhất (Proximity Integrated Circuit Card - PICC), tốc độ 106 kbps đến 848 kbps.
  * **ISO/IEC 18092 (NFCIP-1):** Chuẩn giao tiếp NFC hai chiều giữa 2 thiết bị (Peer-to-Peer) hoặc giả lập thẻ (Card Emulation).
  * **MIFARE Protocols:** Họ thẻ của NXP (Mifare Classic 1K/4K sử dụng thuật toán mã hóa độc quyền Crypto-1, Mifare Ultralight, NTAG213/215/216).
  * **ISO/IEC 15693:** Chuẩn thẻ nhận diện tầm xa hơn (Vicinity Cards, tối đa ~1 mét trong môi trường công nghiệp).
  * **JIS X 6319-4 (Sony FeliCa):** Chuẩn thẻ tốc độ cao phổ biến tại Nhật Bản và Hồng Kông (thẻ Suica, Pasmo, Octopus).
* **Thiết bị tương đương trong thực tế:**
  * Thẻ từ thang máy chung cư, thẻ kiểm soát cửa ra vào công ty, thẻ gửi xe thông minh.
  * Thẻ phòng khách sạn (RFID keycard).
  * Thẻ sinh viên, thẻ cư dân tích hợp chip MIFARE.
  * Căn cước công dân gắn chip (e-ID) và Hộ chiếu điện tử (e-Passport) theo chuẩn ICAO 9303.
  * Thẻ ngân hàng không tiếp xúc (EMV Contactless: Visa payWave, Mastercard PayPass).
  * Thanh toán di động Apple Pay, Google Wallet, Samsung Pay trên smartphone.

---

### 2.4. Module IR — Sóng Quang Học Hồng Ngoại (Infrared Light)

* **Loại sóng:** Sóng ánh sáng quang học nằm ngoài dải nhìn thấy của mắt người (Near-Infrared - NIR), bước sóng xấp xỉ **940 nm**.
* **Đặc tính truyền dẫn:** Truyền thẳng (Line-of-Sight), không xuyên qua tường hoặc vật cản mờ đục, có thể bị phản xạ bởi gương hoặc bề mặt phẳng sáng bóng.
* **Cơ chế điều chế (Carrier Modulation):**
  * Để chống nhiễu từ ánh sáng mặt trời và bóng đèn trong nhà, LED hồng ngoại được băm xung (pulsed) ở tần số sóng mang, phổ biến nhất là **38 kHz** (ngoài ra có 36 kHz, 40 kHz, 56 kHz).
  * Đầu thu (như TSOP4838) tích hợp sẵn bộ lọc quang học 940 nm và bộ lọc dải thông điện tử 38 kHz để giải điều chế thành mức logic 0/1.
* **Giao thức truyền dữ liệu:**
  * **NEC Protocol:** Giao thức thông dụng nhất. Sử dụng mã hóa khoảng cách xung (Pulse Distance Modulation): Header 9ms + 4.5ms, bit 0 dài 1.125ms, bit 1 dài 2.25ms; gồm 8-bit địa chỉ, 8-bit đảo địa chỉ, 8-bit lệnh và 8-bit đảo lệnh.
  * **Sony SIRC (Serial Infrared Remote Control):** Điều chế độ rộng xung (Pulse Width Modulation), độ dài 12-bit, 15-bit hoặc 20-bit.
  * **Philips RC-5 / RC-6:** Sử dụng mã hóa Bi-Phase (Manchester coding) ở sóng mang 36 kHz.
  * **Giao thức điều hòa (Air Conditioner Raw Packets):** Khác với TV chỉ gửi mã nút bấm ngắn, remote điều hòa gửi toàn bộ trạng thái (nhiệt độ, tốc độ gió, hướng gió, chế độ) trong một chuỗi xung dài hàng trăm bit (thường dùng chế độ Raw Timing).
* **Thiết bị tương đương trong thực tế:**
  * Điều khiển từ xa (Remote) của TV, Android Box, đầu thu K+, máy chiếu.
  * Điều khiển máy lạnh / máy điều hòa không khí (Panasonic, Daikin, LG, Casper...).
  * Remote quạt trần, quạt điều hòa, máy lọc không khí.
  * Thiết bị tắt TV hàng loạt (TV-B-Gone).

---

### 2.5. Radio Onboard ESP32-S3 — Sóng Vô Tuyến Wi-Fi & Bluetooth LE

* **Loại sóng:** Sóng vô tuyến vi ba (Microwave RF) tần số 2.4 GHz tích hợp trực tiếp trên SoC ESP32-S3.
* **Dải tần:**
  * **Wi-Fi:** 2.412 GHz – 2.484 GHz (Kênh 1 đến kênh 14).
  * **Bluetooth:** 2.402 GHz – 2.480 GHz (40 kênh BLE, bước 2 MHz).
* **Cơ chế điều chế:**
  * **Wi-Fi:** DSSS (DBPSK, DQPSK), CCK cho chuẩn 802.11b; OFDM (BPSK, QPSK, 16-QAM, 64-QAM) cho 802.11g/n.
  * **BLE:** GFSK (Gaussian Frequency Shift Keying).
* **Chuẩn & Giao thức truyền dữ liệu:**
  * **IEEE 802.11 b/g/n (Wi-Fi 4):**
    * Hỗ trợ chế độ Access Point (AP - phát mạng `AttakIoT` cho người dùng kết nối vào Web Dashboard).
    * Station (STA - kết nối vào mạng Wi-Fi khác).
    * Promiscuous Mode (bắt gói tin thô trên không, phân tích Beacon frame, Probe Request, Deauthentication frame).
  * **Bluetooth 5.0 Low Energy (BLE):**
    * GAP (Generic Access Profile) & GATT (Generic Attribute Profile).
    * Broadcast Advertisements (iBeacon, Eddystone, định danh thiết bị IoT xung quanh).
* **Thiết bị tương đương trong thực tế:**
  * Thiết bị phát Wi-Fi gia đình / văn phòng (Modem Wi-Fi, Router Wi-Fi).
  * Kết nối mạng trên máy tính, điện thoại, máy tính bảng.
  * Thiết bị nhà thông minh (công tắc Wi-Fi Sonoff, bóng đèn Tuya Smart, camera an ninh IP).
  * Vòng đeo tay thông minh (Mi Band, Apple Watch), tai nghe không dây TWS, thẻ định vị AirTag/SmartTag.

---

## 3. Bản Đồ So Sánh Các Tầng Truyền Thông (OSI Mapping)

```
+----------------------------------------------------------------------------------------------------+
|                                    TẦNG GIAO THỨC TRUYỀN DỮ LIỆU                                  |
+--------------------+--------------------------------+--------------------+-------------------------+
| Module             | Tầng Vật lý (Phy / RF / Light) | Tầng Liên kết (MAC)| Tầng Ứng dụng / Thiết bị|
+--------------------+--------------------------------+--------------------+-------------------------+
| CC1101             | RF Sub-GHz (315/433/868 MHz)   | OOK / FSK Bitstream| Cửa cuốn, Remote xe hơi |
| NRF24L01+          | RF 2.4 GHz ISM (126 Channels)  | Enhanced ShockBurst| Chuột không dây, Drone  |
| PN532              | Từ trường 13.56 MHz (Induction)| ISO 14443 / FeliCa | Thẻ thang máy, Căn cước |
| IR (Hồng ngoại)    | Quang học 940 nm (Carrier 38k) | Pulse Distance/PWM | Remote TV, Điều hòa     |
| ESP32-S3 (WiFi/BLE)| RF 2.4 GHz (OFDM / DSSS)       | 802.11 MAC / BLE LL| Dashboard AP, Mạng LAN  |
+--------------------+--------------------------------+--------------------+-------------------------+
```

---

## 4. Ý Nghĩa Trong Nghiên Cứu An Ninh Mạng IoT (Security Context)

Sự kết hợp của 5 khối truyền thông trên một bo mạch duy nhất biến **attak-iot-firmware** thành một công cụ nghiên cứu toàn diện (Multi-tool), bao quát trọn vẹn các bề mặt tấn công vật lý / không dây của hệ sinh thái IoT:

1. **Sub-GHz (CC1101):** Khai thác các hệ thống tự động hóa cơ điện tòa nhà, an ninh tư gia, cửa cuốn và chìa khóa xe hơi thế hệ cũ thiếu cơ chế mã hóa hoặc dùng thuật toán yếu.
2. **2.4 GHz Proprietary (NRF24L01+):** Khai thác lỗ hổng MouseJack (chèn phím không dây qua dongle chuột/bàn phím), gây nhiễu kênh điều khiển drone hoặc thiết bị ngoại vi không dùng Bluetooth.
3. **NFC / RFID (PN532):** Kiểm thử hệ thống kiểm soát truy cập (Access Control), đọc/sao chép UID thẻ thang máy, phân tích cấu trúc bộ nhớ và lỗ hổng khóa Crypto-1 của thẻ MIFARE Classic.
4. **Infrared (IR):** Điều khiển hoặc giả lập tín hiệu hồng ngoại cho thiết bị nghe nhìn, thiết bị gia dụng và điều hòa không khí.
5. **Wi-Fi / BLE (ESP32-S3):** Quét mạng, giám sát khung truyền (packet sniffing), kiểm thử xác thực mạng LAN không dây và các thiết bị BLE xung quanh.

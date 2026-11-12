#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"

// Standalone I2C wiring diagnostic (env:i2c-scan). Probes every reserved-free
// 7-bit address once, then repeats. This is NOT a PN532 liveness check: the
// chip sleeps at power-up and need not ACK until reset + SAMConfig, so a clean
// scan only proves the bus and pull-ups are electrically sane. Liveness is
// getFirmwareVersion() in src/modules/pn532_module.cpp.
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.printf("\nI2C scan: SDA=%d, SCL=%d, 100 kHz\n",
                  static_cast<int>(PIN_PN532_SDA), static_cast<int>(PIN_PN532_SCL));
    if (!Wire.begin(static_cast<int>(PIN_PN532_SDA), static_cast<int>(PIN_PN532_SCL),
                    100000)) {
        Serial.println("I2C initialization failed");
        while (true) delay(1000);
    }
    Wire.setTimeOut(20);
}

void loop() {
    unsigned found = 0;
    unsigned errors = 0;
    Serial.println("Scanning I2C...");
    // Skip reserved 7-bit addresses (0x00-0x07 and 0x78-0x7F).
    for (uint8_t address = 0x08; address < 0x78; ++address) {
        Wire.beginTransmission(address);
        const uint8_t result = Wire.endTransmission();
        if (result == 0) {
            Serial.printf("Found I2C device at 0x%02X\n", address);
            ++found;
        } else if (result != 2) {
            Serial.printf("I2C error %u at 0x%02X\n", result, address);
            ++errors;
        }
    }
    if (found == 0) {
        Serial.println("No I2C devices found (PN532 may be asleep)");
    }
    Serial.printf("Scan complete: %u device(s), %u error(s)\n\n", found, errors);
    delay(3000);
}

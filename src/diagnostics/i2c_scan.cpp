#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("\nI2C scanner: SDA=%d, SCL=%d, 100 kHz\n",
                PIN_PN532_SDA, PIN_PN532_SCL);
  if (!Wire.begin(PIN_PN532_SDA, PIN_PN532_SCL, 100000)) {
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
  if (found == 0) Serial.println("No I2C devices found");
  Serial.printf("Scan complete: %u device(s), %u error(s)\n\n", found, errors);
  delay(3000);
}

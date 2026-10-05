#include "spi_bus.h"

#include "board_pins.h"

namespace spiBus {

namespace {
bool started = false;
} // namespace

void begin() {
    if (started) return;

    pinMode(PIN_CC1101_CS, OUTPUT);
    digitalWrite(PIN_CC1101_CS, HIGH);
    pinMode(PIN_NRF24_CS, OUTPUT);
    digitalWrite(PIN_NRF24_CS, HIGH);
    pinMode(PIN_NRF24_CE, OUTPUT);
    digitalWrite(PIN_NRF24_CE, LOW);

    SPI.begin(static_cast<int>(PIN_SPI_SCK), static_cast<int>(PIN_SPI_MISO), static_cast<int>(PIN_SPI_MOSI), -1);
    started = true;
}

SPIClass &instance() { return SPI; }

} // namespace spiBus

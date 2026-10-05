// Single owner of the shared SPI bus (CC1101 + NRF24). Both radios transact on
// spiBus::instance(); begin() is idempotent and safe from every module's begin().
#pragma once

#include <SPI.h>

namespace spiBus {
// Configures SCK/MOSI/MISO once, leaves SS unmapped (-1) because each radio
// drives its own CS, and parks both CS high / CE low.
void begin();
SPIClass &instance();
} // namespace spiBus

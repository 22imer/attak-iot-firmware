// UID display/serialization helpers. Pure: no Arduino, no I2C.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace nfcUid {

// Uppercase hex, two digits per byte, colon-separated: "04:AB:01:02".
std::string formatHex(const uint8_t *uid, size_t length);

// {"kind":"nfc_uid","uid":"04:AB:01:02"}
std::string toJson(const uint8_t *uid, size_t length);

} // namespace nfcUid

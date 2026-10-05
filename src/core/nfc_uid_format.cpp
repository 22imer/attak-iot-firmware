#include "nfc_uid_format.h"

namespace nfcUid {

namespace {
const char kHexDigits[] = "0123456789ABCDEF";
}

std::string formatHex(const uint8_t *uid, size_t length) {
    std::string out;
    out.reserve(length * 3);
    for (size_t i = 0; i < length; ++i) {
        if (i != 0) out.push_back(':');
        out.push_back(kHexDigits[(uid[i] >> 4) & 0x0F]);
        out.push_back(kHexDigits[uid[i] & 0x0F]);
    }
    return out;
}

std::string toJson(const uint8_t *uid, size_t length) {
    return std::string("{\"kind\":\"nfc_uid\",\"uid\":\"") + formatHex(uid, length) + "\"}";
}

} // namespace nfcUid

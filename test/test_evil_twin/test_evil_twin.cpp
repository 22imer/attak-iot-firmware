#include <unity.h>

#include <string>

#include "core/evil_twin.h"

void setUp() {}
void tearDown() {}

// The page served to AP clients must exist, stay inside the flash cap and carry
// no NUL (a truncated response would be served otherwise). The built-in
// fallback must satisfy the same rules, so a missing /example.html is harmless.
void test_page_usability_rules() {
    TEST_ASSERT_TRUE(evilTwin::pageUsable("<html></html>"));
    TEST_ASSERT_FALSE(evilTwin::pageUsable(""));           // empty file: fall back
    TEST_ASSERT_FALSE(evilTwin::pageUsable(std::string(evilTwin::kMaxPageBytes + 1, 'a')));
    TEST_ASSERT_TRUE(evilTwin::pageUsable(std::string(evilTwin::kMaxPageBytes, 'a')));
    TEST_ASSERT_FALSE(evilTwin::pageUsable(std::string("<html>\0</html>", 15)));

    const std::string fallback = evilTwin::defaultPage();
    TEST_ASSERT_TRUE(evilTwin::pageUsable(fallback));
    TEST_ASSERT_TRUE(fallback.find("<form") != std::string::npos); // must still ask for credentials
    TEST_ASSERT_TRUE(fallback.find("method=\"post\"") != std::string::npos);
}

// OS captive probes must be answered with a redirect (evilTwin::isProbePath),
// while the login page itself and the form POST must not be mistaken for one.
void test_probe_paths_are_recognised() {
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/generate_204"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/gen_204"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/hotspot-detect.html"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/connecttest.txt"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/ncsi.txt"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/library/test/success.html"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/canonical.html"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/success.txt"));
    // Clients append a query string and vary case.
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/Generate_204?foo=bar"));
    TEST_ASSERT_TRUE(evilTwin::isProbePath("/HOTSPOT-DETECT.HTML"));

    TEST_ASSERT_FALSE(evilTwin::isProbePath("/"));
    TEST_ASSERT_FALSE(evilTwin::isProbePath("/index.html"));
    TEST_ASSERT_FALSE(evilTwin::isProbePath("/example.html"));
    TEST_ASSERT_FALSE(evilTwin::isProbePath("/dashboard.js"));
    TEST_ASSERT_FALSE(evilTwin::isProbePath("/generate_204_ok")); // not a probe: still the page
}

// An evil twin rebroadcasts a name taken from a scan, so the SSID rules are the
// only thing standing between the operator and a softAP() that fails (or an AP
// name with control bytes in the Serial log).
void test_clone_ssid_rules() {
    TEST_ASSERT_TRUE(evilTwin::cloneSsidUsable("VanTot"));
    TEST_ASSERT_TRUE(evilTwin::cloneSsidUsable("Mang khong dau"));
    TEST_ASSERT_TRUE(evilTwin::cloneSsidUsable(std::string(evilTwin::kMaxSsidBytes, 'a')));
    TEST_ASSERT_TRUE(evilTwin::cloneSsidUsable("Wi-Fi_2.4G#1")); // punctuation is legal in an SSID

    TEST_ASSERT_FALSE(evilTwin::cloneSsidUsable(""));
    TEST_ASSERT_FALSE(evilTwin::cloneSsidUsable(std::string(evilTwin::kMaxSsidBytes + 1, 'a')));
    TEST_ASSERT_FALSE(evilTwin::cloneSsidUsable(std::string("Van\nTot")));
    TEST_ASSERT_FALSE(evilTwin::cloneSsidUsable(std::string("Van\tTot")));
    TEST_ASSERT_FALSE(evilTwin::cloneSsidUsable(std::string("Van\0Tot", 7)));
    TEST_ASSERT_FALSE(evilTwin::cloneSsidUsable(std::string(1, '\x7f')));
}

// The clone channel must be one the radio can actually be moved to; the caller
// treats an unusable value as "keep the current channel".
void test_clone_channel_rules() {
    TEST_ASSERT_TRUE(evilTwin::channelUsable(1));
    TEST_ASSERT_TRUE(evilTwin::channelUsable(6));
    TEST_ASSERT_TRUE(evilTwin::channelUsable(13));
    TEST_ASSERT_FALSE(evilTwin::channelUsable(0));
    TEST_ASSERT_FALSE(evilTwin::channelUsable(14));
    TEST_ASSERT_FALSE(evilTwin::channelUsable(-1));
}

// The login form in data/example.html posts email/password; the parser must
// turn that URL-encoded body into the pair the operator reads in the log.
void test_credentials_from_login_form() {
    evilTwin::Credentials creds;
    TEST_ASSERT_TRUE(evilTwin::parseFormCredentials("email=an%40gmail.com&password=P%40ssw0rd", creds));
    TEST_ASSERT_TRUE(creds.hasUser);
    TEST_ASSERT_TRUE(creds.hasPass);
    TEST_ASSERT_EQUAL_STRING("an@gmail.com", creds.user);
    TEST_ASSERT_EQUAL_STRING("P@ssw0rd", creds.pass);
}

// An operator is free to rename the inputs, so the usual spellings of each
// role are accepted case-insensitively and unrelated fields are ignored.
void test_credentials_field_names_and_noise() {
    evilTwin::Credentials creds;
    TEST_ASSERT_TRUE(evilTwin::parseFormCredentials("submit=Nh%E1%BB%8Bp+d%E1%BB%A5ng+nh%E1%BA%ADp&USERNAME=abc&pwd=xyz", creds));
    TEST_ASSERT_EQUAL_STRING("abc", creds.user);
    TEST_ASSERT_EQUAL_STRING("xyz", creds.pass);

    // An empty value is still a captured attempt, not a missing one.
    evilTwin::Credentials blank;
    TEST_ASSERT_TRUE(evilTwin::parseFormCredentials("email=&password=", blank));
    TEST_ASSERT_TRUE(blank.hasUser);
    TEST_ASSERT_EQUAL_STRING("", blank.user);

    // Nothing recognisable: the capture still streams, just without credentials.
    evilTwin::Credentials none;
    TEST_ASSERT_FALSE(evilTwin::parseFormCredentials("foo=bar&baz", none));
    TEST_ASSERT_FALSE(none.hasUser);
    TEST_ASSERT_FALSE(none.hasPass);
    TEST_ASSERT_FALSE(evilTwin::parseFormCredentials("", none));
}

// Untrusted input arriving over the air must stay printable and bounded: an
// over-long field truncates, a malformed escape or control byte drops just
// that field, and the rest of the body still parses.
void test_credentials_are_bounded_and_escaped() {
    evilTwin::Credentials creds;
    const std::string longUser(evilTwin::kMaxCredentialBytes + 20, 'a');
    TEST_ASSERT_TRUE(evilTwin::parseFormCredentials("email=" + longUser + "&password=pw", creds));
    TEST_ASSERT_EQUAL_STRING(std::string(evilTwin::kMaxCredentialBytes, 'a').c_str(), creds.user);
    TEST_ASSERT_EQUAL_STRING("pw", creds.pass);

    evilTwin::Credentials partial;
    TEST_ASSERT_TRUE(evilTwin::parseFormCredentials("email=%ZZ&password=ok&user=fallback", partial));
    TEST_ASSERT_TRUE(partial.hasUser);    // %ZZ dropped that field; the later valid one still lands
    TEST_ASSERT_EQUAL_STRING("fallback", partial.user);
    TEST_ASSERT_EQUAL_STRING("ok", partial.pass);

    evilTwin::Credentials ctl;
    TEST_ASSERT_TRUE(evilTwin::parseFormCredentials("email=a%0Ab&password=ok", ctl));
    TEST_ASSERT_FALSE(ctl.hasUser);       // decoded newline must not reach the log
    TEST_ASSERT_EQUAL_STRING("ok", ctl.pass);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_page_usability_rules);
    RUN_TEST(test_probe_paths_are_recognised);
    RUN_TEST(test_clone_ssid_rules);
    RUN_TEST(test_clone_channel_rules);
    RUN_TEST(test_credentials_from_login_form);
    RUN_TEST(test_credentials_field_names_and_noise);
    RUN_TEST(test_credentials_are_bounded_and_escaped);
    return UNITY_END();
}

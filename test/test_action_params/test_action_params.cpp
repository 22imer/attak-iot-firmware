#include <unity.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "core/action_catalog.h"
#include "core/action_params.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// ActionParams container: bounds, ownership, atomic rejection, stale slots.
// ---------------------------------------------------------------------------

void test_reset_declares_absent_slots_and_clamps() {
    ActionParams params;
    params.reset(3);
    TEST_ASSERT_EQUAL_UINT32(3, params.size());
    for (size_t i = 0; i < params.size(); ++i) TEST_ASSERT_FALSE(params.present(i));

    // Absent/out-of-range slots read back as the type's zero value.
    TEST_ASSERT_EQUAL_INT64(0, params.integer(0));
    TEST_ASSERT_TRUE(std::fabs(params.number(1)) < 1e-9);
    TEST_ASSERT_FALSE(params.boolean(2));
    TEST_ASSERT_EQUAL_STRING("", params.string(2));
    TEST_ASSERT_EQUAL_UINT32(0, params.stringLength(2));
    TEST_ASSERT_EQUAL_INT64(0, params.integer(99));

    params.reset(kMaxActionParams + 5); // hard cap
    TEST_ASSERT_EQUAL_UINT32(kMaxActionParams, params.size());
}

void test_reset_clears_stale_slots() {
    ActionParams params;
    params.reset(4);
    TEST_ASSERT_TRUE(params.setInteger(3, 42));
    TEST_ASSERT_TRUE(params.present(3));

    params.reset(1); // shrink: slot 3 is now out of range
    TEST_ASSERT_EQUAL_UINT32(1, params.size());
    TEST_ASSERT_FALSE(params.present(3));

    params.reset(4); // grow again: the old value must not resurrect
    TEST_ASSERT_FALSE(params.present(3));
    TEST_ASSERT_EQUAL_INT64(0, params.integer(3));
}

void test_setters_record_type_and_own_data() {
    ActionParams params;
    params.reset(4);
    TEST_ASSERT_TRUE(params.setInteger(0, -7));
    TEST_ASSERT_TRUE(params.setNumber(1, 2.5));
    TEST_ASSERT_TRUE(params.setBoolean(2, true));
    TEST_ASSERT_TRUE(params.setString(3, "hi", 2));

    TEST_ASSERT_EQUAL(ParamType::Integer, params.type(0));
    TEST_ASSERT_EQUAL_INT64(-7, params.integer(0));
    TEST_ASSERT_EQUAL(ParamType::Number, params.type(1));
    TEST_ASSERT_TRUE(std::fabs(params.number(1) - 2.5) < 1e-9);
    TEST_ASSERT_EQUAL(ParamType::Boolean, params.type(2));
    TEST_ASSERT_TRUE(params.boolean(2));
    TEST_ASSERT_EQUAL(ParamType::String, params.type(3));
    TEST_ASSERT_EQUAL_STRING("hi", params.string(3));
    TEST_ASSERT_EQUAL_UINT32(2, params.stringLength(3));
    TEST_ASSERT_TRUE(params.present(3));
}

void test_setters_reject_out_of_range_index() {
    ActionParams params;
    params.reset(1);
    TEST_ASSERT_FALSE(params.setInteger(1, 5));
    TEST_ASSERT_FALSE(params.setNumber(9, 1.0));
    TEST_ASSERT_FALSE(params.setBoolean(2, true));
    TEST_ASSERT_FALSE(params.setString(1, "x", 1));
}

void test_setnumber_rejects_non_finite() {
    ActionParams params;
    params.reset(1);
    TEST_ASSERT_FALSE(params.setNumber(0, std::numeric_limits<double>::quiet_NaN()));
    TEST_ASSERT_FALSE(params.setNumber(0, std::numeric_limits<double>::infinity()));
    TEST_ASSERT_FALSE(params.present(0));
}

void test_setstring_is_byte_bounded_and_atomic() {
    ActionParams params;
    params.reset(1);
    TEST_ASSERT_TRUE(params.setString(0, "ok", 2));

    char atCap[kMaxParamStringBytes];
    std::memset(atCap, 'a', sizeof(atCap));
    TEST_ASSERT_TRUE(params.setString(0, atCap, sizeof(atCap)));
    TEST_ASSERT_EQUAL_UINT32(kMaxParamStringBytes, params.stringLength(0));

    char over[kMaxParamStringBytes + 1];
    std::memset(over, 'b', sizeof(over));
    TEST_ASSERT_FALSE(params.setString(0, over, sizeof(over)));
    // Rejected writes must not disturb the previous value.
    TEST_ASSERT_EQUAL_UINT32(kMaxParamStringBytes, params.stringLength(0));
    TEST_ASSERT_EQUAL_CHAR('a', params.string(0)[0]);
    TEST_ASSERT_EQUAL_CHAR('\0', params.string(0)[kMaxParamStringBytes]);

    const char embedded[] = {'a', '\0', 'b'};
    TEST_ASSERT_FALSE(params.setString(0, embedded, sizeof(embedded)));
    TEST_ASSERT_EQUAL_CHAR('a', params.string(0)[0]);
}

// ---------------------------------------------------------------------------
// Descriptor schema validation.
// ---------------------------------------------------------------------------

void test_valid_descriptor_passes() {
    const ParamSpec specs[] = {
        {"a", "A", ParamType::Integer, true, -5, 5, 0},
        {"b", "B", ParamType::Number, false, 0.0, 1.5, 0},
        {"c", "C", ParamType::Boolean, false, 0, 0, 0},
        {"d", "D", ParamType::String, false, 0, 0, 0}, // maxLength 0 => default
    };
    ActionDescriptor d{"x", "X", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe,
                       false, false, specs, 4};
    TEST_ASSERT_TRUE(validateActionDescriptor(d));

    d.paramCount = 0;
    d.params = nullptr;
    TEST_ASSERT_TRUE(validateActionDescriptor(d)); // any real no-param descriptor
}

void test_descriptor_rejects_count_overrun_and_null_array() {
    ActionDescriptor d{"x", "X", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe,
                       false, false, nullptr, kMaxActionParams + 1};
    TEST_ASSERT_FALSE(validateActionDescriptor(d));

    d.paramCount = 1;
    d.params = nullptr;
    TEST_ASSERT_FALSE(validateActionDescriptor(d));
}

void test_descriptor_rejects_duplicate_and_null_keys() {
    ParamSpec specs[] = {
        {"a", "A", ParamType::Integer, false, 0, 1, 0},
        {"a", "B", ParamType::Integer, false, 0, 1, 0},
    };
    ActionDescriptor d{"x", "X", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe,
                       false, false, specs, 2};
    TEST_ASSERT_FALSE(validateActionDescriptor(d)); // duplicate name

    specs[1].name = "b";
    specs[1].label = nullptr;
    TEST_ASSERT_FALSE(validateActionDescriptor(d)); // null label

    specs[1].label = "B";
    specs[1].name = nullptr;
    TEST_ASSERT_FALSE(validateActionDescriptor(d)); // null name
}

void test_descriptor_rejects_illegal_type_and_bounds() {
    ParamSpec specs[] = {{"a", "A", static_cast<ParamType>(99), false, 0, 1, 0}};
    ActionDescriptor d{"x", "X", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe,
                       false, false, specs, 1};
    TEST_ASSERT_FALSE(validateActionDescriptor(d));

    specs[0].type = ParamType::Number;
    specs[0].minimum = std::numeric_limits<double>::quiet_NaN();
    TEST_ASSERT_FALSE(validateActionDescriptor(d));

    specs[0].minimum = 0.0;
    specs[0].maximum = std::numeric_limits<double>::infinity();
    TEST_ASSERT_FALSE(validateActionDescriptor(d));

    specs[0].maximum = -1.0; // minimum > maximum
    TEST_ASSERT_FALSE(validateActionDescriptor(d));

    specs[0].type = ParamType::String;
    specs[0].minimum = 0.0;
    specs[0].maximum = 0.0;
    specs[0].maxLength = kMaxParamStringBytes + 1;
    TEST_ASSERT_FALSE(validateActionDescriptor(d));

    specs[0].maxLength = 0; // 0 means "use the default cap"
    TEST_ASSERT_TRUE(validateActionDescriptor(d));
}

// ---------------------------------------------------------------------------
// parseActionParams against a test-local descriptor (never in the catalog).
// ---------------------------------------------------------------------------

namespace {

const ParamSpec kStrictSpecs[] = {
    {"count", "Count", ParamType::Integer, true, 0, 100, 0},
    {"ratio", "Ratio", ParamType::Number, false, 0.0, 1.0, 0},
    {"flag", "Flag", ParamType::Boolean, false, 0, 0, 0},
    {"text", "Text", ParamType::String, false, 0, 0, 16},
};
constexpr ActionDescriptor kStrict{"params_test", "Params", ActionId::Scan, ActionKind::OneShot,
                                   LegalTier::Observe, false, false, kStrictSpecs, 4};

const ParamSpec kOptionalSpecs[] = {
    {"n", "N", ParamType::Integer, false, -5, 5, 0},
};
constexpr ActionDescriptor kOptional{"optional_test", "Optional", ActionId::Scan,
                                     ActionKind::OneShot, LegalTier::Observe, false, false,
                                     kOptionalSpecs, 1};

const ParamSpec kTextSpecs[] = {
    {"text", "Text", ParamType::String, false, 0, 0, 16},
};
constexpr ActionDescriptor kTextOnly{"text_only", "TextOnly", ActionId::Scan, ActionKind::OneShot,
                                     LegalTier::Observe, false, false, kTextSpecs, 1};

constexpr ActionDescriptor kNoParams{"no_params", "None", ActionId::Scan, ActionKind::OneShot,
                                     LegalTier::Observe, false, false, nullptr, 0};

int err(CommandError e) { return static_cast<int>(e); }

} // namespace

void test_no_param_descriptor_accepts_omitted_and_empty_object() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::None), err(parseActionParams("", kNoParams, out)));
    TEST_ASSERT_EQUAL(err(CommandError::None), err(parseActionParams("{}", kNoParams, out)));
    TEST_ASSERT_EQUAL_UINT32(0, out.size());
}

void test_params_must_be_an_object_when_present() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("null", kNoParams, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("[]", kNoParams, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("5", kNoParams, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("{bad", kNoParams, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"x":1})", kNoParams, out)));
}

void test_required_optional_and_unknown_keys() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("{}", kStrict, out))); // missing required
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams),
                      err(parseActionParams(R"({"nope":1,"count":1})", kStrict, out))); // unknown key
    TEST_ASSERT_EQUAL(err(CommandError::None), err(parseActionParams(R"({"count":1})", kStrict, out)));
    TEST_ASSERT_TRUE(out.present(0));
    TEST_ASSERT_FALSE(out.present(1)); // optional omitted stays absent
}

void test_typed_values_survive_the_json_lifetime() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::None),
                      err(parseActionParams(R"({"count":7,"ratio":0.25,"flag":true,"text":"hello"})",
                                            kStrict, out)));
    // parseActionParams' JsonDocument is already destroyed here: the values must
    // still be readable from ActionParams' own storage.
    TEST_ASSERT_EQUAL_INT64(7, out.integer(0));
    TEST_ASSERT_TRUE(std::fabs(out.number(1) - 0.25) < 1e-9);
    TEST_ASSERT_TRUE(out.boolean(2));
    TEST_ASSERT_EQUAL_STRING("hello", out.string(3));
    TEST_ASSERT_EQUAL_UINT32(5, out.stringLength(3));
}

void test_type_strictness_rejects_numeric_strings_and_bools() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":"5"})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":true})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1.5})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,"ratio":"0.5"})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,"ratio":true})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,"flag":1})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,"flag":"true"})", kStrict, out)));
}

void test_explicit_typed_null_is_rejected() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":null})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,"text":null})", kStrict, out)));
}

void test_numeric_ranges_and_non_finite() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::None), err(parseActionParams(R"({"count":100})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::None), err(parseActionParams(R"({"count":0})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":101})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":-1})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,"ratio":1.0001})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,"ratio":1e999})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":-6})", kOptional, out)));
    TEST_ASSERT_EQUAL(err(CommandError::None), err(parseActionParams(R"({"n":-5})", kOptional, out)));
}

void test_integer_bounds_do_not_silently_round() {
    // maximum is 2^53; 2^53+1 is not representable as double, so comparing the
    // value via double would wrongly round it down into range.
    const ParamSpec specs[] = {{"n", "N", ParamType::Integer, true, 0, 9007199254740992.0, 0}};
    ActionDescriptor d{"precision", "P", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe,
                       false, false, specs, 1};
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::None),
                      err(parseActionParams(R"({"n":9007199254740992})", d, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams),
                      err(parseActionParams(R"({"n":9007199254740993})", d, out)));
    // Above int64 entirely: must be rejected, never clamped.
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams),
                      err(parseActionParams(R"({"n":9223372036854775808})", d, out)));
}

void test_string_byte_cap_counts_utf8_and_rejects_nul() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::None), err(parseActionParams(R"({"text":"0123456789abcdef"})", kTextOnly, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams),
                      err(parseActionParams(R"({"text":"0123456789abcdefg"})", kTextOnly, out)));
    // 8 two-byte code points = 16 UTF-8 bytes (the cap), 9 code points = 18.
    TEST_ASSERT_EQUAL(err(CommandError::None),
                      err(parseActionParams("{\"text\":\"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\"}", kTextOnly, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams),
                      err(parseActionParams("{\"text\":\"\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\u00e9\"}", kTextOnly, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams),
                      err(parseActionParams(R"({"text":"a\u0000b"})", kTextOnly, out)));
}

// deserializeJson() alone accepts a valid prefix plus trailing bytes and JSON5
// extensions; parseActionParams must reject all of them as InvalidParams.
void test_malformed_json_syntax_is_invalid_params() {
    ActionParams out;
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1}x)", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams),
                      err(parseActionParams(R"({"count":1} {"count":2})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1}//tail)", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({/*c*/"count":1})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("{'count':1}", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("{count:1}", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":1,})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"count":01})", kStrict, out)));
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams(R"({"text":"\x"})", kTextOnly, out)));

    // Strictly valid JSON keeps today's schema results (no precedence change).
    TEST_ASSERT_EQUAL(err(CommandError::None),
                      err(parseActionParams(R"({"count":1,"ratio":0.25,"flag":true,"text":"ok"})", kStrict, out)));
}

void test_invalid_descriptor_is_rejected_by_the_parser() {
    ActionParams out;
    ActionDescriptor bad{"bad", "Bad", ActionId::Scan, ActionKind::OneShot, LegalTier::Observe,
                         false, false, nullptr, kMaxActionParams + 1};
    TEST_ASSERT_EQUAL(err(CommandError::InvalidParams), err(parseActionParams("{}", bad, out)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_reset_declares_absent_slots_and_clamps);
    RUN_TEST(test_reset_clears_stale_slots);
    RUN_TEST(test_setters_record_type_and_own_data);
    RUN_TEST(test_setters_reject_out_of_range_index);
    RUN_TEST(test_setnumber_rejects_non_finite);
    RUN_TEST(test_setstring_is_byte_bounded_and_atomic);
    RUN_TEST(test_valid_descriptor_passes);
    RUN_TEST(test_descriptor_rejects_count_overrun_and_null_array);
    RUN_TEST(test_descriptor_rejects_duplicate_and_null_keys);
    RUN_TEST(test_descriptor_rejects_illegal_type_and_bounds);
    RUN_TEST(test_no_param_descriptor_accepts_omitted_and_empty_object);
    RUN_TEST(test_params_must_be_an_object_when_present);
    RUN_TEST(test_required_optional_and_unknown_keys);
    RUN_TEST(test_typed_values_survive_the_json_lifetime);
    RUN_TEST(test_type_strictness_rejects_numeric_strings_and_bools);
    RUN_TEST(test_explicit_typed_null_is_rejected);
    RUN_TEST(test_numeric_ranges_and_non_finite);
    RUN_TEST(test_integer_bounds_do_not_silently_round);
    RUN_TEST(test_string_byte_cap_counts_utf8_and_rejects_nul);
    RUN_TEST(test_malformed_json_syntax_is_invalid_params);
    RUN_TEST(test_invalid_descriptor_is_rejected_by_the_parser);
    return UNITY_END();
}

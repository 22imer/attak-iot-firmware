// Bounded, portable parameter values for dashboard `action` commands.
//
// Deliberately plain: fixed-capacity storage with no JSON-backed lifetime, no
// heap-owned command strings and no Arduino.h dependency. The parser copies
// each validated value into these slots, so a WsCommand can be queued after the
// request document has been freed. The WebSocket schema caps every action at
// kMaxActionParams parameters and every string at kMaxParamStringBytes UTF-8
// bytes (embedded NUL is rejected).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "command_types.h"

// What an inbound JSON value must be, per the descriptor's ParamSpec.
enum class ParamType : uint8_t { Integer, Number, Boolean, String };

// Schema capacities: hard upper bounds the parser enforces even if a descriptor
// mis-declares more (see validateActionDescriptor).
constexpr size_t kMaxActionParams = 8;
constexpr size_t kMaxParamStringBytes = 64;

// One declarative parameter of an action. `minimum`/`maximum` apply to the
// numeric types (Integer/Number) and `maxLength` to String; 0 for a String
// means "the schema default" (kMaxParamStringBytes).
struct ParamSpec {
    const char *name;    // key inside the "params" object
    const char *label;   // dashboard display label
    ParamType type;
    bool required;       // false => may be omitted, then stays absent
    double minimum;      // inclusive lower bound (Integer/Number)
    double maximum;      // inclusive upper bound (Integer/Number)
    size_t maxLength;    // inclusive UTF-8 byte cap (String); 0 => default
};

// Storage for one parameter slot. Not a variant reference: every field is
// owned by the slot, so no pointer into the parsed JSON survives.
struct ActionParamValue {
    ParamType type{};
    bool present = false;
    int64_t integer = 0;
    double number = 0.0;
    bool boolean = false;
    size_t textLength = 0;
    char text[kMaxParamStringBytes + 1] = {};
};

// Validated parameters for one accepted action, indexed by the descriptor's
// ParamSpec order. Omitted optional values stay absent.
class ActionParams {
  public:
    // Declares `count` spec slots (clamped to kMaxActionParams), all absent.
    void reset(size_t count);

    size_t size() const { return count_; }

    bool present(size_t index) const;
    ParamType type(size_t index) const;

    // Const typed access indexed by spec position. Out-of-range/absent slots
    // return the type's zero value ("" for String) so callers never need to
    // re-check presence when the descriptor marks a parameter required.
    int64_t integer(size_t index) const;
    double number(size_t index) const;
    bool boolean(size_t index) const;
    const char *string(size_t index) const;
    size_t stringLength(size_t index) const;

    // Parser/builder side. Each setter rejects an out-of-range index; setString
    // additionally rejects embedded NUL and anything above kMaxParamStringBytes.
    bool setInteger(size_t index, int64_t value);
    bool setNumber(size_t index, double value);
    bool setBoolean(size_t index, bool value);
    bool setString(size_t index, const char *data, size_t length);

  private:
    ActionParamValue values_[kMaxActionParams]{};
    size_t count_ = 0;
};

// Validates a descriptor's parameter schema before any JSON is applied:
// at most kMaxActionParams specs; a non-null params array whenever paramCount is
// non-zero; non-null name/label; no duplicate names; a legal ParamType; finite
// minimum<=maximum for numeric specs; and a String maxLength within
// [0, kMaxParamStringBytes] (0 selects the kMaxParamStringBytes default).
// Defined in action_params.cpp (no ArduinoJson dependency).
struct ActionDescriptor;
bool validateActionDescriptor(const ActionDescriptor &descriptor);

// Validation seam shared by the WebSocket parser and native schema tests.
// `json` is the text of the `params` member: an
// object, or empty to mean the member was omitted. An explicit `null`, an array,
// a scalar or malformed text is InvalidParams, as is a declared key whose value
// is an explicit typed null. Returns CommandError::InvalidParams (leaving `out`
// undefined) for a non-object, an unknown key, a missing required value, a
// type/range violation or an over-cap string; returns CommandError::None after
// filling `out`. Defined in ws_command_json.cpp (ArduinoJson). ActionDescriptor
// is forward-declared to keep this header dependency-free.
CommandError parseActionParams(std::string_view json, const ActionDescriptor &descriptor, ActionParams &out);

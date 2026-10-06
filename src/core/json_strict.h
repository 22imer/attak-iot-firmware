// Strict, bounded, allocation-free JSON validation (RFC 8259, no extensions).
//
// Why this exists: ArduinoJson's deserializeJson() accepts a valid JSON prefix
// followed by trailing garbage, and several non-RFC extensions (comments,
// single-quoted strings, etc.). Every ingress frame must be exactly one
// complete JSON value, so parsers gate with this before handing the text to
// deserializeJson(). Callers impose their own root/schema policy afterwards
// (parseWsCommand()/parseActionParams() require an object; action-output
// payloads accept any JSON value, primitive roots included).
//
// Contract of isStrictJsonValue(json, length, maxBytes):
//  * json != nullptr and 1 <= length <= maxBytes;
//  * the whole [json, json+length) is exactly one JSON value, surrounded only
//    by JSON whitespace (space, tab, LF, CR) — no trailing bytes, no second
//    root, no comments, no unquoted keys, no single quotes, no trailing comma;
//  * numbers follow the RFC grammar only: no leading '+', no '.5', no '1.',
//    no '01', no '0x..', no NaN/Infinity;
//  * strings use only the seven two-character escapes and \uXXXX with four hex
//    digits; raw control bytes (< 0x20) are rejected; valid UTF-8 (raw or
//    escaped) passes through unchanged.
//
// Nesting is capped at kMaxNesting, which is at or above ArduinoJson's own
// deserialize nesting limit, so a hostile 1 KiB payload cannot exhaust the
// stack through deep recursion. Header-only inline: no allocation, no cpp to
// add to the native build filter, safe on ESP32 and host alike.
#pragma once

#include <cstddef>

namespace json_strict_detail {

constexpr std::size_t kMaxNesting = 32;

inline bool isWhitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

inline void skipWhitespace(const unsigned char *&p, const unsigned char *end) {
    while (p < end && isWhitespace(*p)) ++p;
}

inline bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }

inline bool isHexDigit(unsigned char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// *p == '"'. Returns true with p just past the closing quote.
inline bool parseString(const unsigned char *&p, const unsigned char *end) {
    ++p; // consume the opening quote
    while (p < end) {
        const unsigned char c = *p++;
        if (c == '"') return true;
        if (c == '\\') {
            if (p >= end) return false;
            const unsigned char esc = *p++;
            if (esc == 'u') {
                if (end - p < 4) return false;
                if (!isHexDigit(p[0]) || !isHexDigit(p[1]) || !isHexDigit(p[2]) || !isHexDigit(p[3])) {
                    return false;
                }
                p += 4;
            } else if (esc != '"' && esc != '\\' && esc != '/' && esc != 'b' && esc != 'f' &&
                       esc != 'n' && esc != 'r' && esc != 't') {
                return false;
            }
        } else if (c < 0x20) {
            return false; // unescaped control byte
        }
    }
    return false; // unterminated
}

// RFC 8259 number: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
inline bool parseNumber(const unsigned char *&p, const unsigned char *end) {
    if (p < end && *p == '-') ++p;
    if (p >= end) return false;
    if (*p == '0') {
        ++p;
    } else if (*p >= '1' && *p <= '9') {
        ++p;
        while (p < end && isDigit(*p)) ++p;
    } else {
        return false; // leading '+' or '.' or a missing integer part
    }
    if (p < end && *p == '.') {
        ++p;
        if (p >= end || !isDigit(*p)) return false;
        while (p < end && isDigit(*p)) ++p;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) ++p;
        if (p >= end || !isDigit(*p)) return false;
        while (p < end && isDigit(*p)) ++p;
    }
    return true;
}

inline bool parseLiteral(const unsigned char *&p, const unsigned char *end, const char *literal) {
    while (*literal != '\0') {
        if (p >= end || *p != static_cast<unsigned char>(*literal)) return false;
        ++p;
        ++literal;
    }
    return true;
}

inline bool parseValue(const unsigned char *&p, const unsigned char *end, std::size_t depth);
inline bool parseArray(const unsigned char *&p, const unsigned char *end, std::size_t depth);
inline bool parseObject(const unsigned char *&p, const unsigned char *end, std::size_t depth);

// *p == '['. depth is this container's nesting level.
inline bool parseArray(const unsigned char *&p, const unsigned char *end, std::size_t depth) {
    ++p; // consume '['
    skipWhitespace(p, end);
    if (p < end && *p == ']') {
        ++p;
        return true;
    }
    for (;;) {
        if (!parseValue(p, end, depth)) return false;
        skipWhitespace(p, end);
        if (p >= end) return false;
        if (*p == ',') {
            ++p;
            skipWhitespace(p, end);
            continue; // a ']' here is a trailing comma -> parseValue rejects it
        }
        if (*p == ']') {
            ++p;
            return true;
        }
        return false;
    }
}

// *p == '{'. depth is this container's nesting level.
inline bool parseObject(const unsigned char *&p, const unsigned char *end, std::size_t depth) {
    ++p; // consume '{'
    skipWhitespace(p, end);
    if (p < end && *p == '}') {
        ++p;
        return true;
    }
    for (;;) {
        if (p >= end || *p != '"') return false; // keys must be double-quoted
        if (!parseString(p, end)) return false;
        skipWhitespace(p, end);
        if (p >= end || *p != ':') return false;
        ++p;
        skipWhitespace(p, end);
        if (!parseValue(p, end, depth)) return false;
        skipWhitespace(p, end);
        if (p >= end) return false;
        if (*p == ',') {
            ++p;
            skipWhitespace(p, end);
            continue; // a '}' here is a trailing comma -> next key check fails
        }
        if (*p == '}') {
            ++p;
            return true;
        }
        return false;
    }
}

inline bool parseValue(const unsigned char *&p, const unsigned char *end, std::size_t depth) {
    skipWhitespace(p, end);
    if (p >= end) return false;
    switch (*p) {
    case '{':
    case '[':
        if (depth + 1 > kMaxNesting) return false;
        return *p == '{' ? parseObject(p, end, depth + 1) : parseArray(p, end, depth + 1);
    case '"':
        return parseString(p, end);
    case 't':
        return parseLiteral(p, end, "true");
    case 'f':
        return parseLiteral(p, end, "false");
    case 'n':
        return parseLiteral(p, end, "null");
    default:
        return parseNumber(p, end);
    }
}

} // namespace json_strict_detail

// True only when [json, json+length) is exactly one complete JSON value.
inline bool isStrictJsonValue(const char *json, std::size_t length,
                              std::size_t maxBytes = static_cast<std::size_t>(-1)) {
    if (json == nullptr || length == 0 || length > maxBytes) return false;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(json);
    const unsigned char *const end = p + length;
    if (!json_strict_detail::parseValue(p, end, 0)) return false;
    json_strict_detail::skipWhitespace(p, end);
    return p == end;
}

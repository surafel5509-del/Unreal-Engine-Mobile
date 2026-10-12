// Nova Engine — strict JSON reader implementation (M2).

#include "assets/Json.h"

#include "core/Log.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace nova::assets {

using core::i64;
using core::u8;
using core::u32;
using core::usize;

// --------------------------------------------------------------------- names

const char* jsonTypeName(JsonType type) noexcept {
    switch (type) {
        case JsonType::kNull: return "null";
        case JsonType::kBool: return "bool";
        case JsonType::kNumber: return "number";
        case JsonType::kString: return "string";
        case JsonType::kArray: return "array";
        case JsonType::kObject: return "object";
    }
    return "unknown";
}

const char* jsonErrorMessage(JsonErrorCode code) noexcept {
    switch (code) {
        case JsonErrorCode::kOk: return "ok";
        case JsonErrorCode::kEmptyInput: return "input is empty";
        case JsonErrorCode::kTrailingContent: return "unexpected content after the JSON value";
        case JsonErrorCode::kUnexpectedCharacter: return "unexpected character";
        case JsonErrorCode::kInvalidLiteral: return "invalid literal (expected true/false/null)";
        case JsonErrorCode::kInvalidNumber: return "invalid number";
        case JsonErrorCode::kUnterminatedString: return "unterminated string";
        case JsonErrorCode::kInvalidEscape: return "invalid escape sequence";
        case JsonErrorCode::kInvalidUtf16Surrogate: return "invalid UTF-16 surrogate pair";
        case JsonErrorCode::kMissingColon: return "missing ':' after object member name";
        case JsonErrorCode::kMissingComma: return "missing ',' or closing bracket";
        case JsonErrorCode::kDepthLimit: return "nesting depth limit exceeded";
    }
    return "unknown error";
}

JsonLocation jsonLocate(std::string_view text, usize offset) noexcept {
    JsonLocation location{};
    const usize limit = offset < text.size() ? offset : text.size();
    for (usize index = 0; index < limit; ++index) {
        if (text[index] == '\n') {
            ++location.line;
            location.column = 1;
        } else {
            ++location.column;
        }
    }
    return location;
}

// --------------------------------------------------------------------- value

JsonType JsonValue::type() const noexcept {
    switch (m_data.index()) {
        case 1: return JsonType::kBool;
        case 2: return JsonType::kNumber;
        case 3: return JsonType::kString;
        case 4: return JsonType::kArray;
        case 5: return JsonType::kObject;
        default: return JsonType::kNull;
    }
}

bool JsonValue::asBool(bool fallback) const noexcept {
    const bool* value = std::get_if<bool>(&m_data);
    return value != nullptr ? *value : fallback;
}

double JsonValue::asNumber(double fallback) const noexcept {
    const double* value = std::get_if<double>(&m_data);
    return value != nullptr ? *value : fallback;
}

i64 JsonValue::asInteger(i64 fallback) const noexcept {
    const double* value = std::get_if<double>(&m_data);
    if (value == nullptr) {
        return fallback;
    }
    const double truncated = std::trunc(*value);
    // 2^63 is not representable as an i64 boundary check; use the exact limits
    // of the mantissa-safe range and reject anything beyond it.
    if (!(truncated >= -9223372036854775808.0) || truncated >= 9223372036854775808.0) {
        return fallback;
    }
    return static_cast<i64>(truncated);
}

float JsonValue::asFloat(float fallback) const noexcept {
    const double* value = std::get_if<double>(&m_data);
    return value != nullptr ? static_cast<float>(*value) : fallback;
}

const std::string& JsonValue::asString() const noexcept {
    static const std::string kEmpty;
    const std::string* value = std::get_if<std::string>(&m_data);
    return value != nullptr ? *value : kEmpty;
}

usize JsonValue::size() const noexcept {
    if (const JsonArray* array = std::get_if<JsonArray>(&m_data)) {
        return array->size();
    }
    if (const JsonObject* object = std::get_if<JsonObject>(&m_data)) {
        return object->size();
    }
    return 0;
}

const JsonValue& JsonValue::operator[](usize index) const noexcept {
    const JsonArray* array = std::get_if<JsonArray>(&m_data);
    if (array != nullptr && index < array->size()) {
        return (*array)[index];
    }
    return jsonNull();
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    const JsonObject* object = std::get_if<JsonObject>(&m_data);
    if (object == nullptr) {
        return nullptr;
    }
    for (const JsonObjectMember& member : *object) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

const JsonValue& JsonValue::at(std::string_view key) const noexcept {
    const JsonValue* value = find(key);
    return value != nullptr ? *value : jsonNull();
}

const JsonArray& JsonValue::asArray() const noexcept {
    static const JsonArray kEmpty;
    const JsonArray* array = std::get_if<JsonArray>(&m_data);
    return array != nullptr ? *array : kEmpty;
}

const JsonObject& JsonValue::asObject() const noexcept {
    static const JsonObject kEmpty;
    const JsonObject* object = std::get_if<JsonObject>(&m_data);
    return object != nullptr ? *object : kEmpty;
}

float JsonValue::floatAt(std::string_view key, float fallback) const noexcept {
    return at(key).asFloat(fallback);
}

i64 JsonValue::integerAt(std::string_view key, i64 fallback) const noexcept {
    return at(key).asInteger(fallback);
}

bool JsonValue::boolAt(std::string_view key, bool fallback) const noexcept {
    return at(key).asBool(fallback);
}

const std::string& JsonValue::stringAt(std::string_view key) const noexcept {
    return at(key).asString();
}

const JsonValue& jsonNull() noexcept {
    static const JsonValue kNull{};
    return kNull;
}

// -------------------------------------------------------------------- parser

namespace {

/// Append a Unicode code point as UTF-8.
void appendUtf8(std::string& out, u32 codePoint) {
    if (codePoint < 0x80u) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
}

/// Parse exactly four hexadecimal digits at `pos`; advances `pos` on success.
bool readHex4(std::string_view text, usize& pos, u32& out) {
    if (pos + 4 > text.size()) {
        return false;
    }
    u32 value = 0;
    for (usize index = 0; index < 4; ++index) {
        const char character = text[pos + index];
        u32 digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<u32>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<u32>(character - 'a') + 10u;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<u32>(character - 'A') + 10u;
        } else {
            return false;
        }
        value = (value << 4) | digit;
    }
    pos += 4;
    out = value;
    return true;
}

/// Match the JSON number grammar and report its length:
/// `[ "-" ] int [ "." digits ] [ ("e"|"E") ["-"|"+"] digits ]`, where `int` is
/// `0` or `[1-9] digits` (no leading zeros, no `+`, no `inf`/`nan`).
///
/// std::from_chars follows the C literal grammar, which is more permissive than
/// JSON ("1." and "01" would scan), so the text is matched here first and only
/// the exact range is then converted.
bool scanJsonNumber(std::string_view text, usize start, usize& outLength) {
    usize index = start;
    if (index < text.size() && text[index] == '-') {
        ++index;
    }

    const usize integerStart = index;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        ++index;
    }
    const usize integerDigits = index - integerStart;
    if (integerDigits == 0) {
        return false;
    }
    if (integerDigits > 1 && text[integerStart] == '0') {
        return false;  // 01, 007 — not JSON
    }

    if (index < text.size() && text[index] == '.') {
        ++index;
        const usize fractionStart = index;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            ++index;
        }
        if (index == fractionStart) {
            return false;  // "1." — a fraction needs at least one digit
        }
    }

    if (index < text.size() && (text[index] == 'e' || text[index] == 'E')) {
        ++index;
        if (index < text.size() && (text[index] == '+' || text[index] == '-')) {
            ++index;
        }
        const usize exponentStart = index;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
            ++index;
        }
        if (index == exponentStart) {
            return false;  // "1e" / "1e+"
        }
    }

    outLength = index - start;
    return true;
}

/// Recursive-descent parser. Errors record the offending byte offset; no
/// exceptions are thrown anywhere (the engine runtime is -fno-exceptions).
class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {
        // Skip a leading UTF-8 BOM, which some exporters emit.
        if (m_text.size() >= 3 && static_cast<u8>(m_text[0]) == 0xEF &&
            static_cast<u8>(m_text[1]) == 0xBB && static_cast<u8>(m_text[2]) == 0xBF) {
            m_pos = 3;
        }
    }

    bool parse(JsonValue& outRoot) {
        skipWhitespace();
        if (m_pos >= m_text.size()) {
            return fail(JsonErrorCode::kEmptyInput);
        }
        if (!parseValue(outRoot, 0)) {
            return false;
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            return fail(JsonErrorCode::kTrailingContent);
        }
        return true;
    }

    const JsonError& error() const { return m_error; }

private:
    bool fail(JsonErrorCode code) {
        if (m_error.code == JsonErrorCode::kOk) {
            m_error.code = code;
            m_error.offset = m_pos < m_text.size() ? m_pos : m_text.size();
        }
        return false;
    }

    bool atEnd() const { return m_pos >= m_text.size(); }
    char peek() const { return atEnd() ? '\0' : m_text[m_pos]; }

    void skipWhitespace() {
        while (!atEnd()) {
            const char character = m_text[m_pos];
            if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    bool parseValue(JsonValue& out, usize depth) {
        if (depth > kMaxJsonDepth) {
            return fail(JsonErrorCode::kDepthLimit);
        }
        if (atEnd()) {
            return fail(JsonErrorCode::kUnexpectedCharacter);
        }
        switch (peek()) {
            case '{': return parseObject(out, depth + 1);
            case '[': return parseArray(out, depth + 1);
            case '"': {
                std::string text;
                if (!parseString(text)) {
                    return false;
                }
                out = JsonValue(std::move(text));
                return true;
            }
            case 't':
                if (m_text.compare(m_pos, 4, "true") == 0) {
                    m_pos += 4;
                    out = JsonValue(true);
                    return true;
                }
                return fail(JsonErrorCode::kInvalidLiteral);
            case 'f':
                if (m_text.compare(m_pos, 5, "false") == 0) {
                    m_pos += 5;
                    out = JsonValue(false);
                    return true;
                }
                return fail(JsonErrorCode::kInvalidLiteral);
            case 'n':
                if (m_text.compare(m_pos, 4, "null") == 0) {
                    m_pos += 4;
                    out = JsonValue();
                    return true;
                }
                return fail(JsonErrorCode::kInvalidLiteral);
            default: break;
        }
        const char character = peek();
        if (character == '-' || (character >= '0' && character <= '9')) {
            return parseNumber(out);
        }
        return fail(JsonErrorCode::kUnexpectedCharacter);
    }

    bool parseNumber(JsonValue& out) {
        usize length = 0;
        if (!scanJsonNumber(m_text, m_pos, length)) {
            return fail(JsonErrorCode::kInvalidNumber);
        }

        const char* const begin = m_text.data() + m_pos;
        const char* const end = begin + length;
        double value = 0.0;
        // Locale independent, and restricted to the exact matched range so a
        // permissive prefix cannot be accepted.
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            // errc::result_out_of_range means the magnitude exceeds double
            // (e.g. 1e400); rejected rather than silently turned into inf.
            return fail(JsonErrorCode::kInvalidNumber);
        }
        m_pos += length;
        out = JsonValue(value);
        return true;
    }

    bool parseString(std::string& out) {
        // m_text[m_pos] == '"'
        ++m_pos;
        out.clear();
        while (true) {
            if (atEnd()) {
                return fail(JsonErrorCode::kUnterminatedString);
            }
            const char character = m_text[m_pos];
            if (character == '"') {
                ++m_pos;
                return true;
            }
            if (character == '\\') {
                ++m_pos;
                if (atEnd()) {
                    return fail(JsonErrorCode::kUnterminatedString);
                }
                const char escape = m_text[m_pos];
                ++m_pos;
                switch (escape) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        u32 codeUnit = 0;
                        if (!readHex4(m_text, m_pos, codeUnit)) {
                            return fail(JsonErrorCode::kInvalidEscape);
                        }
                        if (codeUnit >= 0xD800u && codeUnit <= 0xDBFFu) {
                            // High surrogate: must be followed by \uDC00-\uDFFF.
                            if (m_pos + 1 >= m_text.size() || m_text[m_pos] != '\\' ||
                                m_text[m_pos + 1] != 'u') {
                                return fail(JsonErrorCode::kInvalidUtf16Surrogate);
                            }
                            m_pos += 2;
                            u32 low = 0;
                            if (!readHex4(m_text, m_pos, low)) {
                                return fail(JsonErrorCode::kInvalidEscape);
                            }
                            if (low < 0xDC00u || low > 0xDFFFu) {
                                return fail(JsonErrorCode::kInvalidUtf16Surrogate);
                            }
                            const u32 codePoint =
                                0x10000u + ((codeUnit - 0xD800u) << 10) + (low - 0xDC00u);
                            appendUtf8(out, codePoint);
                        } else if (codeUnit >= 0xDC00u && codeUnit <= 0xDFFFu) {
                            return fail(JsonErrorCode::kInvalidUtf16Surrogate);
                        } else {
                            appendUtf8(out, codeUnit);
                        }
                        break;
                    }
                    default: return fail(JsonErrorCode::kInvalidEscape);
                }
                continue;
            }
            if (static_cast<u8>(character) < 0x20u) {
                // Control characters must be escaped in JSON strings.
                return fail(JsonErrorCode::kInvalidEscape);
            }
            out.push_back(character);
            ++m_pos;
        }
    }

    bool parseObject(JsonValue& out, usize depth) {
        // m_text[m_pos] == '{'
        ++m_pos;
        JsonObject members;
        skipWhitespace();
        if (peek() == '}') {
            ++m_pos;
            out = JsonValue(std::move(members));
            return true;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                // Either EOF or a value where a member name belongs.
                return fail(JsonErrorCode::kUnexpectedCharacter);
            }
            std::string name;
            if (!parseString(name)) {
                return false;
            }
            skipWhitespace();
            if (peek() != ':') {
                return fail(JsonErrorCode::kMissingColon);
            }
            ++m_pos;
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value, depth)) {
                return false;
            }
            members.emplace_back(std::move(name), std::move(value));
            skipWhitespace();
            const char character = peek();
            if (character == ',') {
                ++m_pos;
                continue;
            }
            if (character == '}') {
                ++m_pos;
                out = JsonValue(std::move(members));
                return true;
            }
            return fail(JsonErrorCode::kMissingComma);
        }
    }

    bool parseArray(JsonValue& out, usize depth) {
        // m_text[m_pos] == '['
        ++m_pos;
        JsonArray elements;
        skipWhitespace();
        if (peek() == ']') {
            ++m_pos;
            out = JsonValue(std::move(elements));
            return true;
        }
        while (true) {
            skipWhitespace();
            JsonValue value;
            if (!parseValue(value, depth)) {
                return false;
            }
            elements.push_back(std::move(value));
            skipWhitespace();
            const char character = peek();
            if (character == ',') {
                ++m_pos;
                continue;
            }
            if (character == ']') {
                ++m_pos;
                out = JsonValue(std::move(elements));
                return true;
            }
            return fail(JsonErrorCode::kMissingComma);
        }
    }

    std::string_view m_text;
    usize m_pos = 0;
    JsonError m_error{};
};

}  // namespace

nova::Result<JsonValue, JsonError> parseJson(std::string_view text) {
    Parser parser(text);
    JsonValue root;
    if (!parser.parse(root)) {
        const JsonError& error = parser.error();
        const JsonLocation location = jsonLocate(text, error.offset);
        NOVA_LOG_ERROR("json: %s at byte %zu (line %zu, column %zu)",
                       jsonErrorMessage(error.code), error.offset, location.line, location.column);
        return nova::Result<JsonValue, JsonError>::failure(error);
    }
    return nova::Result<JsonValue, JsonError>::success(std::move(root));
}

}  // namespace nova::assets

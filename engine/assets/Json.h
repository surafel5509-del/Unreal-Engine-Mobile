#pragma once

// Nova Engine — strict JSON reader for asset loading (M2).
//
// glTF 2.0 documents are JSON, and the engine runtime is -fno-exceptions, so
// this is a small purpose-built DOM reader rather than a third-party library:
// it reports failures through Result<T, JsonError> with a byte offset, enforces
// a recursion depth limit (hostile input must not blow the stack), and keeps
// object members in document order.
//
// Scope: RFC 8259 JSON text only — no comments, no trailing commas, no NaN/Inf,
// no BOM tolerance beyond a leading UTF-8 BOM which is skipped. Strings are
// decoded to UTF-8 (\u escapes and surrogate pairs included). Numbers are stored
// as double, which represents every integer a glTF accessor index can hold.
//
// Thread-safety: JsonValue is an immutable-after-parse value type; parsing and
// reading are free of shared state, so a document may be read from any thread.
// The only shared object is the process-wide null singleton returned by
// jsonNull(), which is never mutated.

#include "core/Result.h"
#include "core/Types.h"

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace nova::assets {

/// Discriminator for JsonValue.
enum class JsonType : core::u8 {
    kNull = 0,
    kBool,
    kNumber,
    kString,
    kArray,
    kObject,
};

/// Human-readable name of a JsonType ("null", "object", …).
[[nodiscard]] const char* jsonTypeName(JsonType type) noexcept;

class JsonValue;

/// Array elements in document order.
using JsonArray = std::vector<JsonValue>;

/// One `name: value` member; the name is UTF-8 and unescaped.
using JsonObjectMember = std::pair<std::string, JsonValue>;

/// Object members in document order. Lookup is linear: glTF objects hold a
/// handful of members, so this beats a hash map on both memory and speed.
using JsonObject = std::vector<JsonObjectMember>;

/// Failure codes for parseJson().
enum class JsonErrorCode : core::u8 {
    kOk = 0,
    kEmptyInput,
    kTrailingContent,
    kUnexpectedCharacter,
    kInvalidLiteral,
    kInvalidNumber,
    kUnterminatedString,
    kInvalidEscape,
    kInvalidUtf16Surrogate,
    kMissingColon,
    kMissingComma,
    kDepthLimit,
};

/// Human-readable description of a JsonErrorCode.
[[nodiscard]] const char* jsonErrorMessage(JsonErrorCode code) noexcept;

/// Where and why parsing failed.
struct JsonError {
    JsonErrorCode code = JsonErrorCode::kOk;
    /// Byte offset into the parsed text (0-based; text size on EOF errors).
    core::usize offset = 0;
};

/// 1-based source location of a byte offset, for log messages.
struct JsonLocation {
    core::usize line = 1;
    core::usize column = 1;
};

/// Convert a byte offset into a line/column pair.
[[nodiscard]] JsonLocation jsonLocate(std::string_view text, core::usize offset) noexcept;

/// An immutable JSON value. Copyable and movable; cheap to copy small scalars,
/// expensive for containers (as with any DOM).
class JsonValue {
public:
    JsonValue() = default;
    explicit JsonValue(bool value) : m_data(value) {}
    explicit JsonValue(double value) : m_data(value) {}
    explicit JsonValue(std::string value) : m_data(std::move(value)) {}
    explicit JsonValue(JsonArray value) : m_data(std::move(value)) {}
    explicit JsonValue(JsonObject value) : m_data(std::move(value)) {}

    [[nodiscard]] JsonType type() const noexcept;
    [[nodiscard]] bool isNull() const noexcept { return type() == JsonType::kNull; }
    [[nodiscard]] bool isBool() const noexcept { return type() == JsonType::kBool; }
    [[nodiscard]] bool isNumber() const noexcept { return type() == JsonType::kNumber; }
    [[nodiscard]] bool isString() const noexcept { return type() == JsonType::kString; }
    [[nodiscard]] bool isArray() const noexcept { return type() == JsonType::kArray; }
    [[nodiscard]] bool isObject() const noexcept { return type() == JsonType::kObject; }

    /// Value getters: a type mismatch yields `fallback` instead of an error,
    /// which matches how glTF optional properties are read.
    [[nodiscard]] bool asBool(bool fallback = false) const noexcept;
    [[nodiscard]] double asNumber(double fallback = 0.0) const noexcept;
    /// Truncates toward zero; out-of-range or non-numeric values yield
    /// `fallback`.
    [[nodiscard]] core::i64 asInteger(core::i64 fallback = 0) const noexcept;
    [[nodiscard]] float asFloat(float fallback = 0.0f) const noexcept;
    /// Empty string when this is not a string.
    [[nodiscard]] const std::string& asString() const noexcept;

    /// Element count for arrays and objects, 0 for everything else.
    [[nodiscard]] core::usize size() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    /// Array element by index, or jsonNull() when out of range / not an array.
    [[nodiscard]] const JsonValue& operator[](core::usize index) const noexcept;

    /// Object member by name, or nullptr when absent / not an object.
    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;

    /// Object member by name, or jsonNull() when absent.
    [[nodiscard]] const JsonValue& at(std::string_view key) const noexcept;

    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return find(key) != nullptr;
    }

    /// Direct container access (empty singleton when the type does not match).
    [[nodiscard]] const JsonArray& asArray() const noexcept;
    [[nodiscard]] const JsonObject& asObject() const noexcept;

    /// Typed convenience for the common glTF pattern
    /// `node["translation"][0].asFloat(0.0f)`; returns `fallback` when any hop
    /// is missing or has the wrong type.
    [[nodiscard]] float floatAt(std::string_view key, float fallback) const noexcept;
    [[nodiscard]] core::i64 integerAt(std::string_view key, core::i64 fallback) const noexcept;
    [[nodiscard]] bool boolAt(std::string_view key, bool fallback) const noexcept;
    [[nodiscard]] const std::string& stringAt(std::string_view key) const noexcept;

private:
    using Storage =
        std::variant<std::monostate, bool, double, std::string, JsonArray, JsonObject>;
    Storage m_data;
};

/// Process-wide null value returned by failed lookups. Never mutated.
[[nodiscard]] const JsonValue& jsonNull() noexcept;

/// Maximum nesting depth accepted by parseJson(). glTF documents are shallow;
/// the limit exists so malformed or hostile input cannot exhaust the stack.
inline constexpr core::usize kMaxJsonDepth = 64;

/// Parse a complete JSON text. On success the root value is returned; on
/// failure the error carries the byte offset of the first problem.
[[nodiscard]] nova::Result<JsonValue, JsonError> parseJson(std::string_view text);

}  // namespace nova::assets

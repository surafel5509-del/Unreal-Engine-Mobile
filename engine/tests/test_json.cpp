// Unit tests — the strict JSON reader used by the asset pipeline (M2).
//
// The reader is hand-written because the engine runtime is -fno-exceptions, so
// these cases cover both the accepted grammar and every rejection path.
#include <doctest.h>

#include "assets/Json.h"

#include <string>
#include <vector>

namespace {

using namespace nova;
using nova::assets::JsonErrorCode;
using nova::assets::JsonValue;
using nova::core::usize;

/// Parse and require success; returns the root value.
JsonValue parseOk(std::string_view text) {
    const auto result = assets::parseJson(text);
    REQUIRE(result.isOk());
    return result.value();
}

/// Parse and require a specific failure code.
void expectFailure(std::string_view text, JsonErrorCode expected) {
    const auto result = assets::parseJson(text);
    CAPTURE(text);
    REQUIRE(result.isError());
    CHECK(result.error().code == expected);
}

}  // namespace

TEST_CASE("Json parses scalars") {
    CHECK(parseOk("null").isNull());
    CHECK(parseOk("true").asBool(false) == true);
    CHECK(parseOk("false").asBool(true) == false);
    CHECK(parseOk("42").asNumber() == doctest::Approx(42.0));
    CHECK(parseOk("-1.5").asNumber() == doctest::Approx(-1.5));
    CHECK(parseOk("\"hello\"").asString() == "hello");
    CHECK(parseOk("\"\"").asString().empty());

    CHECK(parseOk("true").isBool());
    CHECK(parseOk("1").isNumber());
    CHECK(parseOk("\"a\"").isString());
    CHECK_FALSE(parseOk("1").isString());
}

TEST_CASE("Json accepts the number grammar and rejects the rest") {
    CHECK(parseOk("0").asNumber() == doctest::Approx(0.0));
    CHECK(parseOk("-0").asNumber() == doctest::Approx(0.0));
    CHECK(parseOk("1e3").asNumber() == doctest::Approx(1000.0));
    CHECK(parseOk("1E+3").asNumber() == doctest::Approx(1000.0));
    CHECK(parseOk("1.25e-2").asNumber() == doctest::Approx(0.0125));
    CHECK(parseOk("12345678901234").asInteger() == 12345678901234LL);

    expectFailure("01", JsonErrorCode::kInvalidNumber);
    expectFailure("+1", JsonErrorCode::kUnexpectedCharacter);
    expectFailure(".5", JsonErrorCode::kUnexpectedCharacter);
    expectFailure("1.", JsonErrorCode::kInvalidNumber);
    // Non-JSON literals: 'I'/'N' are not value starts at all, while "nan"
    // reaches the literal matcher and fails there.
    expectFailure("Infinity", JsonErrorCode::kUnexpectedCharacter);
    expectFailure("NaN", JsonErrorCode::kUnexpectedCharacter);
    expectFailure("nan", JsonErrorCode::kInvalidLiteral);
    expectFailure("nil", JsonErrorCode::kInvalidLiteral);
    expectFailure("trueish", JsonErrorCode::kTrailingContent);
    expectFailure("1e", JsonErrorCode::kInvalidNumber);
    expectFailure("1e+", JsonErrorCode::kInvalidNumber);
    expectFailure("-", JsonErrorCode::kInvalidNumber);
    // Beyond double's magnitude: rejected instead of silently becoming inf.
    expectFailure("1e400", JsonErrorCode::kInvalidNumber);
    CHECK(parseOk("1e308").asNumber() == doctest::Approx(1e308));
    expectFailure("tru", JsonErrorCode::kInvalidLiteral);
    expectFailure("nulll", JsonErrorCode::kTrailingContent);
}

TEST_CASE("Json decodes string escapes to UTF-8") {
    CHECK(parseOk(R"("a\"b\\c\/d")").asString() == "a\"b\\c/d");
    CHECK(parseOk(R"("\b\f\n\r\t")").asString() == "\b\f\n\r\t");
    CHECK(parseOk(R"("\u0041\u00e9")").asString() == "A\xc3\xa9");
    // Surrogate pair for U+1F600 (grinning face) -> 4-byte UTF-8.
    CHECK(parseOk(R"("\ud83d\ude00")").asString() == "\xF0\x9F\x98\x80");
    // A BMP code point that is not a surrogate stays 3 bytes.
    CHECK(parseOk(R"("\u4e2d")").asString() == "\xe4\xb8\xad");

    expectFailure(R"("\ud83d")", JsonErrorCode::kInvalidUtf16Surrogate);
    expectFailure(R"("\ude00")", JsonErrorCode::kInvalidUtf16Surrogate);
    expectFailure(R"("\ud83d\u0041")", JsonErrorCode::kInvalidUtf16Surrogate);
    expectFailure(R"("\u00")", JsonErrorCode::kInvalidEscape);
    expectFailure(R"("\uZZZZ")", JsonErrorCode::kInvalidEscape);
    expectFailure(R"("\q")", JsonErrorCode::kInvalidEscape);
    expectFailure("\"unterminated", JsonErrorCode::kUnterminatedString);
    expectFailure("\"trail\\", JsonErrorCode::kUnterminatedString);
    // A raw control character inside a string must be escaped.
    expectFailure(std::string("\"a\rb\""), JsonErrorCode::kInvalidEscape);
}

TEST_CASE("Json parses arrays and preserves order") {
    const JsonValue empty = parseOk("[]");
    CHECK(empty.isArray());
    CHECK(empty.empty());
    CHECK(empty.size() == 0);

    const JsonValue array = parseOk("[1, \"two\", false, null, [3], {\"k\": 4}]");
    REQUIRE(array.size() == 6);
    CHECK(array[0].asNumber() == doctest::Approx(1.0));
    CHECK(array[1].asString() == "two");
    CHECK(array[2].asBool() == false);
    CHECK(array[3].isNull());
    CHECK(array[4][0].asNumber() == doctest::Approx(3.0));
    CHECK(array[5].at("k").asNumber() == doctest::Approx(4.0));

    // Out of range yields the shared null value rather than undefined access.
    CHECK(array[99].isNull());
    CHECK(array[99].size() == 0);

    expectFailure("[1,]", JsonErrorCode::kUnexpectedCharacter);
    expectFailure("[1 2]", JsonErrorCode::kMissingComma);
    expectFailure("[1,", JsonErrorCode::kUnexpectedCharacter);
    expectFailure("[", JsonErrorCode::kUnexpectedCharacter);
}

TEST_CASE("Json parses objects in document order") {
    const JsonValue empty = parseOk("{}");
    CHECK(empty.isObject());
    CHECK(empty.size() == 0);

    const JsonValue object = parseOk(R"({"z": 1, "a": 2, "m": {"deep": true}})");
    REQUIRE(object.size() == 3);
    const auto& members = object.asObject();
    CHECK(members[0].first == "z");
    CHECK(members[1].first == "a");
    CHECK(members[2].first == "m");
    CHECK(object.at("m").at("deep").asBool() == true);

    CHECK(object.find("a") != nullptr);
    CHECK(object.find("missing") == nullptr);
    CHECK(object.contains("z"));
    CHECK_FALSE(object.contains("nope"));
    CHECK(object.at("nope").isNull());

    // Duplicate names are kept in order; find() reports the first one.
    const JsonValue duplicates = parseOk(R"({"k": 1, "k": 2})");
    CHECK(duplicates.size() == 2);
    CHECK(duplicates.at("k").asNumber() == doctest::Approx(1.0));

    expectFailure(R"({"a" 1})", JsonErrorCode::kMissingColon);
    expectFailure(R"({"a": 1,})", JsonErrorCode::kUnexpectedCharacter);
    expectFailure(R"({"a": 1 "b": 2})", JsonErrorCode::kMissingComma);
    expectFailure(R"({a: 1})", JsonErrorCode::kUnexpectedCharacter);
    expectFailure(R"({"a")", JsonErrorCode::kMissingColon);
}

TEST_CASE("Json tolerates whitespace and a leading BOM") {
    const JsonValue value = parseOk(" \t\r\n { \"a\" : [ 1 , 2 ] } \n ");
    CHECK(value.at("a").size() == 2);

    const std::string withBom = "\xEF\xBB\xBF{\"a\":1}";
    const JsonValue bomValue = parseOk(withBom);
    CHECK(bomValue.at("a").asNumber() == doctest::Approx(1.0));
}

TEST_CASE("Json rejects empty input and trailing content") {
    expectFailure("", JsonErrorCode::kEmptyInput);
    expectFailure("   ", JsonErrorCode::kEmptyInput);
    expectFailure("1 2", JsonErrorCode::kTrailingContent);
    expectFailure("{} extra", JsonErrorCode::kTrailingContent);
}

TEST_CASE("Json enforces the nesting depth limit") {
    std::string deep;
    for (usize index = 0; index < 60; ++index) {
        deep += '[';
    }
    for (usize index = 0; index < 60; ++index) {
        deep += ']';
    }
    CHECK(assets::parseJson(deep).isOk());

    std::string tooDeep;
    for (usize index = 0; index < assets::kMaxJsonDepth + 8; ++index) {
        tooDeep += '[';
    }
    for (usize index = 0; index < assets::kMaxJsonDepth + 8; ++index) {
        tooDeep += ']';
    }
    expectFailure(tooDeep, JsonErrorCode::kDepthLimit);
}

TEST_CASE("Json getters fall back instead of failing") {
    const JsonValue root = parseOk(R"({"n": 3.7, "s": "text", "b": true})");

    CHECK(root.floatAt("n", 0.0f) == doctest::Approx(3.7f));
    CHECK(root.integerAt("n", 0) == 3);           // truncates toward zero
    CHECK(root.boolAt("b", false) == true);
    CHECK(root.stringAt("s") == "text");

    CHECK(root.floatAt("missing", 1.5f) == doctest::Approx(1.5f));
    CHECK(root.integerAt("missing", -7) == -7);
    CHECK(root.boolAt("missing", true) == true);
    CHECK(root.stringAt("missing").empty());

    // Wrong type -> fallback, never a crash.
    CHECK(root.floatAt("s", 2.0f) == doctest::Approx(2.0f));
    CHECK(root.integerAt("b", 9) == 9);
    CHECK(root.at("s").asBool(true) == true);
    CHECK(root.at("n").asString().empty());

    // Scalars report size 0 and no members.
    CHECK(parseOk("5").size() == 0);
    CHECK(parseOk("5").find("x") == nullptr);
    CHECK(parseOk("\"s\"").asArray().empty());
    CHECK(parseOk("[]").asObject().empty());
}

TEST_CASE("Json integer conversion clamps out-of-range values") {
    CHECK(parseOk("-2.9").asInteger(0) == -2);
    CHECK(parseOk("1e30").asInteger(42) == 42);
    CHECK(parseOk("-1e30").asInteger(42) == 42);
    CHECK(parseOk("\"5\"").asInteger(7) == 7);
}

TEST_CASE("Json errors carry an offset and a source location") {
    const auto result = assets::parseJson("{\n  \"a\": 1,\n  \"b\": @\n}");
    REQUIRE(result.isError());
    CHECK(result.error().code == JsonErrorCode::kUnexpectedCharacter);
    const assets::JsonLocation location = assets::jsonLocate(
        "{\n  \"a\": 1,\n  \"b\": @\n}", result.error().offset);
    CHECK(location.line == 3);
    CHECK(location.column == 8);
    CHECK(std::string(assets::jsonErrorMessage(result.error().code)).size() > 0);
}

TEST_CASE("Json names every type") {
    CHECK(std::string(assets::jsonTypeName(assets::JsonType::kNull)) == "null");
    CHECK(std::string(assets::jsonTypeName(assets::JsonType::kBool)) == "bool");
    CHECK(std::string(assets::jsonTypeName(assets::JsonType::kNumber)) == "number");
    CHECK(std::string(assets::jsonTypeName(assets::JsonType::kString)) == "string");
    CHECK(std::string(assets::jsonTypeName(assets::JsonType::kArray)) == "array");
    CHECK(std::string(assets::jsonTypeName(assets::JsonType::kObject)) == "object");
}

TEST_CASE("Json reads a glTF-shaped document") {
    // Trimmed from the glTF 2.0 sample model format (BoxTextured style).
    const JsonValue root = parseOk(R"({
      "asset": {"version": "2.0", "generator": "Nova tests"},
      "scene": 0,
      "scenes": [{"nodes": [0]}],
      "nodes": [{"mesh": 0, "translation": [1.0, 2.0, 3.0]}, {"children": [2]}],
      "meshes": [{
        "primitives": [{
          "attributes": {"POSITION": 1, "NORMAL": 2, "TEXCOORD_0": 3},
          "indices": 0,
          "material": 0,
          "mode": 4
        }]
      }],
      "materials": [{
        "name": "box",
        "doubleSided": false,
        "pbrMetallicRoughness": {
          "baseColorFactor": [0.8, 0.2, 0.1, 1.0],
          "metallicFactor": 0.0,
          "roughnessFactor": 0.75
        }
      }],
      "accessors": [
        {"bufferView": 0, "componentType": 5123, "count": 36, "type": "SCALAR"},
        {"bufferView": 1, "componentType": 5126, "count": 24, "type": "VEC3",
         "min": [-0.5, -0.5, -0.5], "max": [0.5, 0.5, 0.5]}
      ],
      "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 72, "target": 34963}],
      "buffers": [{"byteLength": 648, "uri": "BoxTextured0.bin"}]
    })");

    CHECK(root.at("asset").at("version").asString() == "2.0");
    CHECK(root.at("scene").asInteger(-1) == 0);
    REQUIRE(root.at("nodes").size() == 2);
    CHECK(root.at("nodes")[0].at("translation")[1].asFloat(0.0f) == doctest::Approx(2.0f));
    CHECK(root.at("nodes")[1].at("children")[0].asInteger(-1) == 2);

    const JsonValue& primitive = root.at("meshes")[0].at("primitives")[0];
    CHECK(primitive.at("attributes").at("POSITION").asInteger(-1) == 1);
    CHECK(primitive.at("attributes").at("TEXCOORD_0").asInteger(-1) == 3);
    CHECK(primitive.integerAt("indices", -1) == 0);
    CHECK(primitive.integerAt("mode", -1) == 4);

    const JsonValue& material = root.at("materials")[0];
    CHECK(material.stringAt("name") == "box");
    CHECK(material.boolAt("doubleSided", true) == false);
    const JsonValue& pbr = material.at("pbrMetallicRoughness");
    CHECK(pbr.at("baseColorFactor")[0].asFloat(0.0f) == doctest::Approx(0.8f));
    CHECK(pbr.floatAt("roughnessFactor", 1.0f) == doctest::Approx(0.75f));

    const JsonValue& accessor = root.at("accessors")[1];
    CHECK(accessor.integerAt("componentType", 0) == 5126);
    CHECK(accessor.integerAt("count", 0) == 24);
    CHECK(accessor.stringAt("type") == "VEC3");
    CHECK(accessor.at("min")[0].asFloat(9.0f) == doctest::Approx(-0.5f));
    CHECK(root.at("buffers")[0].integerAt("byteLength", 0) == 648);
    CHECK(root.at("buffers")[0].stringAt("uri") == "BoxTextured0.bin");
}

TEST_CASE("Json handles a large document without recursion trouble") {
    std::string text = "[";
    constexpr usize kElements = 5000;
    for (usize index = 0; index < kElements; ++index) {
        if (index > 0) {
            text += ',';
        }
        text += "{\"i\":" + std::to_string(index) + ",\"v\":" + std::to_string(index * 0.5) + "}";
    }
    text += ']';

    const JsonValue root = parseOk(text);
    REQUIRE(root.size() == kElements);
    CHECK(root[0].at("i").asInteger(-1) == 0);
    CHECK(root[kElements - 1].at("i").asInteger(-1) == static_cast<long long>(kElements - 1));
    CHECK(root[1234].at("v").asNumber() == doctest::Approx(1234 * 0.5));
}

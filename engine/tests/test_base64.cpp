// Unit tests — base64 and data-URI decoding for the asset pipeline (M2).
#include <doctest.h>

#include "assets/Base64.h"
#include "core/Types.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace nova;
using nova::core::usize;

core::ByteBuffer bytesOf(std::string_view text) {
    core::ByteBuffer buffer;
    buffer.reserve(text.size());
    for (const char character : text) {
        buffer.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return buffer;
}

std::string toString(const core::ByteBuffer& buffer) {
    std::string out;
    out.reserve(buffer.size());
    for (const std::byte value : buffer) {
        out.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return out;
}

std::string encode(const core::ByteBuffer& buffer) {
    std::string out;
    assets::base64Encode(std::span<const std::byte>(buffer.data(), buffer.size()), out);
    return out;
}

}  // namespace

TEST_CASE("Base64 matches the RFC 4648 test vectors") {
    CHECK(encode(bytesOf("")) == "");
    CHECK(encode(bytesOf("f")) == "Zg==");
    CHECK(encode(bytesOf("fo")) == "Zm8=");
    CHECK(encode(bytesOf("foo")) == "Zm9v");
    CHECK(encode(bytesOf("foob")) == "Zm9vYg==");
    CHECK(encode(bytesOf("fooba")) == "Zm9vYmE=");
    CHECK(encode(bytesOf("foobar")) == "Zm9vYmFy");
}

TEST_CASE("Base64 decodes the RFC 4648 test vectors") {
    core::ByteBuffer out;
    REQUIRE(assets::base64Decode("", out));
    CHECK(out.empty());

    struct Vector {
        const char* encoded;
        const char* decoded;
    };
    const std::vector<Vector> vectors = {
        {"Zg==", "f"},   {"Zm8=", "fo"},     {"Zm9v", "foo"}, {"Zm9vYg==", "foob"},
        {"Zm9vYmE=", "fooba"}, {"Zm9vYmFy", "foobar"},
    };
    for (const Vector& vector : vectors) {
        REQUIRE(assets::base64Decode(vector.encoded, out));
        CHECK(toString(out) == vector.decoded);
    }
}

TEST_CASE("Base64 round-trips every byte value and length") {
    for (usize length = 0; length <= 12; ++length) {
        core::ByteBuffer original;
        for (usize index = 0; index < length; ++index) {
            original.push_back(static_cast<std::byte>(static_cast<unsigned char>(index * 37 + 11)));
        }
        const std::string encoded = encode(original);
        core::ByteBuffer decoded;
        REQUIRE(assets::base64Decode(encoded, decoded));
        CHECK(decoded.size() == original.size());
        // The empty check must come first: memcmp() with two null pointers and a
        // zero length is undefined behaviour even though it looks harmless, and
        // hardened libc implementations (the one libc++ builds against here)
        // abort on it.
        const bool identical =
            original.empty() ||
            std::memcmp(decoded.data(), original.data(), original.size()) == 0;
        CHECK(identical);
    }

    // All 256 byte values in one payload.
    core::ByteBuffer all;
    for (usize value = 0; value < 256; ++value) {
        all.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    core::ByteBuffer decoded;
    REQUIRE(assets::base64Decode(encode(all), decoded));
    REQUIRE(decoded.size() == 256);
    for (usize value = 0; value < 256; ++value) {
        CHECK(std::to_integer<unsigned>(decoded[value]) == value);
    }
}

TEST_CASE("Base64 skips whitespace the way MIME wraps it") {
    const std::string wrapped = "Zm9v\nYmFy\r\n  Zm9v\tYmFy\f\v";
    core::ByteBuffer out;
    REQUIRE(assets::base64Decode(wrapped, out));
    CHECK(toString(out) == "foobarfoobar");
}

TEST_CASE("Base64 rejects malformed input and clears the output") {
    core::ByteBuffer out;

    const bool badCharacter = assets::base64Decode("Zm9v*", out);
    CHECK_FALSE(badCharacter);
    CHECK(out.empty());

    const bool wrongLength = assets::base64Decode("Zm9", out);
    CHECK_FALSE(wrongLength);

    const bool paddingInside = assets::base64Decode("Zm==9v", out);
    CHECK_FALSE(paddingInside);

    const bool tooMuchPadding = assets::base64Decode("Zg====", out);
    CHECK_FALSE(tooMuchPadding);

    const bool paddingOnly = assets::base64Decode("==", out);
    CHECK_FALSE(paddingOnly);

    // Trailing bits must be zero: 'A' 'B' leaves 4 significant bits = 0b0001.
    const bool nonzeroTrailingBits = assets::base64Decode("AB==", out);
    CHECK_FALSE(nonzeroTrailingBits);
    CHECK(out.empty());

    // A previously filled buffer is cleared before a failed decode.
    REQUIRE(assets::base64Decode("Zm9v", out));
    CHECK(out.size() == 3);
    CHECK_FALSE(assets::base64Decode("Zm9", out));
    CHECK(out.empty());
}

TEST_CASE("Data URIs are split into media type, encoding and payload") {
    assets::DataUri uri{};

    REQUIRE(assets::parseDataUri("data:application/octet-stream;base64,AAEC", uri));
    CHECK(uri.mediaType == "application/octet-stream");
    CHECK(uri.base64);
    CHECK(uri.payload == "AAEC");

    REQUIRE(assets::parseDataUri("data:text/plain;charset=utf-8,hello", uri));
    CHECK(uri.mediaType == "text/plain");
    CHECK_FALSE(uri.base64);
    CHECK(uri.payload == "hello");

    REQUIRE(assets::parseDataUri("data:,raw", uri));
    CHECK(uri.mediaType.empty());
    CHECK_FALSE(uri.base64);
    CHECK(uri.payload == "raw");

    REQUIRE(assets::parseDataUri("data:image/png;base64,", uri));
    CHECK(uri.mediaType == "image/png");
    CHECK(uri.base64);
    CHECK(uri.payload.empty());

    CHECK_FALSE(assets::parseDataUri("http://example.com/x.bin", uri));
    CHECK_FALSE(assets::parseDataUri("data:no-comma", uri));
    CHECK_FALSE(assets::parseDataUri("", uri));
}

TEST_CASE("dataUriDecode handles the glTF embedded-buffer form") {
    core::ByteBuffer out;
    std::string_view mediaType;

    REQUIRE(assets::dataUriDecode("data:application/octet-stream;base64,AAECAw==", out,
                                  &mediaType));
    CHECK(mediaType == "application/octet-stream");
    REQUIRE(out.size() == 4);
    CHECK(std::to_integer<unsigned>(out[0]) == 0);
    CHECK(std::to_integer<unsigned>(out[3]) == 3);

    // Percent-encoded payloads are rejected rather than silently mis-decoded.
    CHECK_FALSE(assets::dataUriDecode("data:text/plain,hello%20world", out));
    CHECK(out.empty());

    CHECK_FALSE(assets::dataUriDecode("data:application/octet-stream;base64,###", out));
    CHECK_FALSE(assets::dataUriDecode("not-a-uri", out));
}

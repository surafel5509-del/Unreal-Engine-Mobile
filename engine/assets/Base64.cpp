// Nova Engine — base64 and data-URI decoding implementation (M2).

#include "assets/Base64.h"

#include "core/Log.h"

#include <array>
#include <cstddef>
#include <cstring>

namespace nova::assets {

using core::u8;
using core::u32;
using core::usize;

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Reverse lookup: 255 marks a character that is not base64 (whitespace is
/// handled separately so it can be skipped rather than rejected).
constexpr std::array<u8, 256> makeDecodeTable() {
    std::array<u8, 256> table{};
    for (usize index = 0; index < table.size(); ++index) {
        table[index] = 255;
    }
    for (usize index = 0; index < 64; ++index) {
        table[static_cast<usize>(static_cast<u8>(kAlphabet[index]))] = static_cast<u8>(index);
    }
    return table;
}

constexpr std::array<u8, 256> kDecodeTable = makeDecodeTable();

bool isSkippable(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

}  // namespace

bool base64Decode(std::string_view input, core::ByteBuffer& out) {
    out.clear();

    // Collect the significant characters first so padding can be validated:
    // at most two '=' and only at the very end.
    usize significant = 0;
    usize padding = 0;
    for (usize index = 0; index < input.size(); ++index) {
        const char character = input[index];
        if (isSkippable(character)) {
            continue;
        }
        if (character == '=') {
            ++padding;
            continue;
        }
        if (padding > 0) {
            NOVA_LOG_ERROR("base64: data after padding at byte %zu", index);
            return false;
        }
        if (kDecodeTable[static_cast<usize>(static_cast<u8>(character))] == 255) {
            NOVA_LOG_ERROR("base64: invalid character 0x%02x at byte %zu",
                           static_cast<unsigned>(static_cast<u8>(character)), index);
            return false;
        }
        ++significant;
    }

    if (padding > 2) {
        NOVA_LOG_ERROR("base64: %zu padding characters (maximum is 2)", padding);
        return false;
    }
    if (significant == 0) {
        // An empty payload decodes to empty bytes; that is legal in glTF for a
        // zero-length buffer view, but padding without data is not.
        if (padding > 0) {
            NOVA_LOG_ERROR("base64: padding without data");
            return false;
        }
        return true;
    }
    if ((significant + padding) % 4 != 0) {
        NOVA_LOG_ERROR("base64: length %zu is not a multiple of 4", significant + padding);
        return false;
    }

    out.reserve(((significant + padding) / 4) * 3);

    u32 accumulator = 0;
    usize bits = 0;
    for (usize index = 0; index < input.size(); ++index) {
        const char character = input[index];
        if (isSkippable(character)) {
            continue;
        }
        if (character == '=') {
            break;  // everything after this is padding, already validated
        }
        accumulator = (accumulator << 6) | kDecodeTable[static_cast<usize>(static_cast<u8>(character))];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFFu));
        }
    }

    // Trailing bits must be zero: "AB==" style inputs are accepted, but a
    // non-zero remainder means the text was not produced by an encoder.
    if (bits > 0 && (accumulator & ((1u << bits) - 1u)) != 0) {
        NOVA_LOG_ERROR("base64: %zu trailing bits are not zero", bits);
        out.clear();
        return false;
    }
    return true;
}

void base64Encode(std::span<const std::byte> input, std::string& out) {
    const usize byteCount = input.size();
    out.reserve(out.size() + ((byteCount + 2) / 3) * 4);

    usize index = 0;
    while (index + 3 <= byteCount) {
        const u32 triple = (static_cast<u32>(std::to_integer<u8>(input[index])) << 16) |
                           (static_cast<u32>(std::to_integer<u8>(input[index + 1])) << 8) |
                           static_cast<u32>(std::to_integer<u8>(input[index + 2]));
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3Fu]);
        out.push_back(kAlphabet[triple & 0x3Fu]);
        index += 3;
    }

    const usize remaining = byteCount - index;
    if (remaining == 1) {
        const u32 value = static_cast<u32>(std::to_integer<u8>(input[index])) << 16;
        out.push_back(kAlphabet[(value >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(value >> 12) & 0x3Fu]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const u32 value = (static_cast<u32>(std::to_integer<u8>(input[index])) << 16) |
                          (static_cast<u32>(std::to_integer<u8>(input[index + 1])) << 8);
        out.push_back(kAlphabet[(value >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(value >> 12) & 0x3Fu]);
        out.push_back(kAlphabet[(value >> 6) & 0x3Fu]);
        out.push_back('=');
    }
}

bool parseDataUri(std::string_view uri, DataUri& out) {
    out = DataUri{};
    constexpr std::string_view kPrefix = "data:";
    if (uri.size() < kPrefix.size() || uri.compare(0, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    const usize comma = uri.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }

    std::string_view header = uri.substr(kPrefix.size(), comma - kPrefix.size());
    out.payload = uri.substr(comma + 1);

    const usize semicolon = header.find(';');
    if (semicolon == std::string_view::npos) {
        out.mediaType = header;
        return true;
    }
    out.mediaType = header.substr(0, semicolon);
    // Only one parameter is defined by RFC 2397 for our use: ";base64".
    const std::string_view parameter = header.substr(semicolon + 1);
    out.base64 = parameter == "base64";
    return true;
}

bool dataUriDecode(std::string_view uri, core::ByteBuffer& out, std::string_view* outMediaType) {
    out.clear();
    DataUri dataUri{};
    if (!parseDataUri(uri, dataUri)) {
        NOVA_LOG_ERROR("assets: not a data URI (%zu bytes)", uri.size());
        return false;
    }
    if (!dataUri.base64) {
        NOVA_LOG_ERROR("assets: unsupported data URI encoding (only ';base64' is handled)");
        return false;
    }
    if (!base64Decode(dataUri.payload, out)) {
        return false;
    }
    if (outMediaType != nullptr) {
        *outMediaType = dataUri.mediaType;
    }
    return true;
}

}  // namespace nova::assets

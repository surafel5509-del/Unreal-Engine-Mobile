#pragma once

// Nova Engine — base64 and data-URI decoding (M2).
//
// glTF embeds binary payloads either in the .glb container or as
// `data:application/octet-stream;base64,...` URIs inside the JSON document.
// Both paths need a decoder that never throws and never allocates beyond the
// output buffer.
//
// Thread-safety: pure functions over caller-owned buffers; safe from any
// thread.

#include "core/Types.h"

#include <span>
#include <string>
#include <string_view>

namespace nova::assets {

/// Decoded `data:` URI (RFC 2397), pointing into the caller's text.
struct DataUri {
    /// Media type between `data:` and `,`/`;` — empty when absent.
    std::string_view mediaType;
    /// True when the URI carried the `;base64` parameter.
    bool base64 = false;
    /// Encoded payload (still base64 when `base64` is true).
    std::string_view payload;
};

/// Decode base64 (RFC 4648 standard alphabet). Accepts optional `=` padding and
/// skips ASCII whitespace; rejects any other character, a bad padding length or
/// a trailing partial group. `out` is cleared first and left empty on failure.
///
/// Returns false and logs on malformed input.
bool base64Decode(std::string_view input, core::ByteBuffer& out);

/// Encode bytes as base64 with padding, appending to `out`. Used by the tools
/// and the tests; the runtime only decodes.
void base64Encode(std::span<const std::byte> input, std::string& out);

/// Split a `data:` URI. Returns false when `uri` does not start with `data:` or
/// has no `,` separator. The returned views borrow from `uri`.
bool parseDataUri(std::string_view uri, DataUri& out);

/// Parse a data URI and decode it. Only `;base64` payloads are supported —
/// percent-encoded text payloads are rejected (glTF recommends base64, and the
/// binary container covers every other case).
///
/// On success `out` holds the decoded bytes and `outMediaType` (when non-null)
/// receives the media type view, which borrows from `uri`.
bool dataUriDecode(std::string_view uri, core::ByteBuffer& out,
                   std::string_view* outMediaType = nullptr);

}  // namespace nova::assets

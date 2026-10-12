# `assets` — Nova Engine module

**Namespace:** `nova::assets`  ·  **Milestone:** M2 (in progress)  ·  **CMake flag:** n/a (always built)

Runtime asset loading: glTF 2.0 models, materials and textures, and (from M3)
cooked containers and streaming. This module owns the *decoding* side — turning
bytes on disk or in the APK into typed CPU data — and hands GPU-shaped results to
`rhi` through the renderer's draw-list contract.

## What is here now

| File | Contents |
| --- | --- |
| `Json.h` / `.cpp` | Strict RFC 8259 JSON reader: `JsonValue` DOM (null/bool/number/string/array/object), `parseJson()` returning `Result<JsonValue, JsonError>`, offset + line/column error reporting, `jsonLocate()`, `jsonErrorMessage()` |
| `Base64.h` / `.cpp` | `base64Decode()` / `base64Encode()`, `DataUri` + `parseDataUri()` and `dataUriDecode()` for glTF's `data:...;base64,` embedded buffers |

### JSON reader

Hand-written rather than vendored, because the engine runtime is
`-fno-exceptions` and every third-party JSON library either throws or drags in a
large configuration surface. Design points worth knowing before using it:

* **Failures are values.** `parseJson` returns `Result<JsonValue, JsonError>`;
  `JsonError{code, offset}` says what and where, `jsonLocate()` turns the offset
  into a 1-based line/column, and the reader logs one `NOVA_LOG_ERROR` per
  failure.
* **Strict where JSON is strict, tolerant where glTF is optional.** No comments,
  no trailing commas, no leading zeros (`01`), no `1.`, no `+1`, no `inf`/`nan`;
  a leading UTF-8 BOM is skipped; numbers beyond `double`'s magnitude are
  rejected instead of silently becoming `inf`. Getters (`asFloat`, `integerAt`,
  `boolAt`, `stringAt`, …) take a fallback, so a missing optional property is a
  default value rather than an error.
* **Bounded recursion.** `kMaxJsonDepth = 64` — hostile input cannot exhaust the
  stack, which matters because a parser that cannot throw must not overflow.
* **Document order is preserved** for both array elements and object members
  (`JsonObject` is a vector of name/value pairs, so linear lookup; glTF objects
  hold a handful of members). Duplicate names are kept and `find()` reports the
  first.
* **Strings are decoded to UTF-8**, including `\uXXXX` and surrogate pairs; a
  lone surrogate or an unescaped control character is an error.

```cpp
const auto result = assets::parseJson(text);
if (!result) {
    const assets::JsonError& error = result.error();
    NOVA_LOG_ERROR("gltf: %s", assets::jsonErrorMessage(error.code));
    return;
}
const assets::JsonValue& root = result.value();
const float roughness = root.at("materials")[0]
                              .at("pbrMetallicRoughness")
                              .floatAt("roughnessFactor", 1.0f);
```

### Base64 and data URIs

`base64Decode` skips ASCII whitespace (MIME-wrapped payloads), validates padding
position and count, and rejects non-zero trailing bits — a decoder that accepts
corrupt input would silently produce a truncated buffer view. `out` is always
cleared first, so a failure cannot leave stale bytes behind. `base64Encode`
exists for `tools/` and the tests; the runtime only decodes.

`dataUriDecode` handles the glTF embedded-buffer form
(`data:application/octet-stream;base64,...`) and rejects percent-encoded text
payloads rather than mis-decoding them — the `.glb` binary container covers every
case where base64 is not enough.

### Thread-safety

Both are pure functions over caller-owned buffers with no shared mutable state,
so parsing and decoding may run on any thread (M3 streaming will use a worker).
`JsonValue` is a value type: read it from as many threads as you like, but do not
mutate a document another thread is reading.

## Tests

`engine/tests/test_json.cpp` (14 cases: scalars, the number grammar and its
rejections, string escapes and UTF-8/surrogate handling, arrays, objects and
duplicate keys, whitespace/BOM, empty input, trailing content, the depth limit,
fallback getters, integer clamping, error offsets and locations, a glTF-shaped
document, and a 5 000-element document) and `engine/tests/test_base64.cpp`
(7 cases: RFC 4648 vectors both ways, all-lengths and all-byte-values round
trips, MIME whitespace, malformed input, data-URI splitting and decoding).

## Next for M2

* `GltfDocument.h` / `GltfLoader.cpp`: the glTF 2.0 document model (scenes,
  nodes, meshes, accessors, buffer views, materials, textures) plus `.gltf` and
  `.glb` container loading through `platform::VirtualFileSystem`.
* Mesh/material upload into `rhi` buffers, and a `MeshRef`-compatible asset
  handle so the draw list can render loaded models instead of the builtin
  triangle.
* Image decode (PNG/JPEG) lands with the M3 texture pipeline (KTX2/ASTC);
  until then image bytes are carried encoded.

## Rules for new code here

See [`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md):

* depend only on layers below this one (`core` → `platform` → `assets`);
* no exceptions — return `nova::Result<T, E>` or a failure value;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

# `assets` — Nova Engine module

**Namespace:** `nova::assets`  ·  **Milestone:** M2/M3  ·  **CMake flag:** n/a (always built once landed)

Runtime asset loading: glTF 2.0 models, materials, textures, and (from M3) cooked containers and streaming.

## Planned contents (M2/M3)

* glTF 2.0 loader (binary + JSON) → RHI buffers/textures
* Material/texture registry with reference counting
* M3: cooked-asset container, versioning, streaming manager, VFS cache mount

## Status

Placeholder directory: no sources are compiled into `nova_engine` yet, so the
host build and the Android build are unaffected by it. The module lands with
M2/M3 together with its doctest suite in `engine/tests/`.

Rules that apply the moment code arrives (see
[`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md)):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>`;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

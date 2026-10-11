# `scripting` — Nova Engine module

**Namespace:** `nova::scripting`  ·  **Milestone:** M8  ·  **CMake flag:** `NOVA_ENABLE_LUA`

Lua gameplay scripting through Sol2. The JNI translation unit stays the only exception boundary in the process.

## Planned contents (M8)

* Lua VM lifecycle bound to `Engine::init`/`shutdown`
* Bindings for ECS, input, console, and asset APIs
* Script-driven components and hot reload in debug builds
* Sandboxing/timeouts for untrusted scripts

## Status

Placeholder directory: no sources are compiled into `nova_engine` yet, so the
host build and the Android build are unaffected by it. The module lands with
M8 together with its doctest suite in `engine/tests/`.

Rules that apply the moment code arrives (see
[`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md)):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>`;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

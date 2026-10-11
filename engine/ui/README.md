# `ui` — Nova Engine module

**Namespace:** `nova::ui`  ·  **Milestone:** M9  ·  **CMake flag:** `NOVA_ENABLE_UI`

RmlUi-based in-game UI, rendered as an RHI pass — including the on-screen console overlay that M1 exposes only programmatically.

## Planned contents (M9)

* RmlUi render/backend bridge over `nova::rhi`
* Layout, input routing, focus
* Debug HUD (FPS, draw calls, memory) fed by `RenderStats`/`DeviceStats`
* Console overlay wired to `core::Console`

## Status

Placeholder directory: no sources are compiled into `nova_engine` yet, so the
host build and the Android build are unaffected by it. The module lands with
M9 together with its doctest suite in `engine/tests/`.

Rules that apply the moment code arrives (see
[`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md)):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>`;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

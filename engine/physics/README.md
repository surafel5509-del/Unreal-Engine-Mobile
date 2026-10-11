# `physics` — Nova Engine module

**Namespace:** `nova::physics`  ·  **Milestone:** M5  ·  **CMake flag:** `NOVA_ENABLE_PHYSICS`

Jolt-based physics behind a Nova-facing API: rigid bodies, collision shapes, raycasts, character controller.

## Planned contents (M5)

* World stepping with a fixed timestep driven from `Engine::frame`
* Shape/component mapping onto the ECS
* Raycasts, sweeps, overlap queries
* Character controller
* Debug draw submitted through the RHI

## Status

Placeholder directory: no sources are compiled into `nova_engine` yet, so the
host build and the Android build are unaffected by it. The module lands with
M5 together with its doctest suite in `engine/tests/`.

Rules that apply the moment code arrives (see
[`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md)):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>`;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

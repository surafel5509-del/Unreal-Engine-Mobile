# `scene` — Nova Engine module

**Namespace:** `nova::scene`  ·  **Milestone:** M2  ·  **CMake flag:** n/a (always built once landed)

Scene representation: ECS storage, transform hierarchy, cameras, culling. Replaces the hard-coded two-pass loop in `engine/renderer` with a data-driven pass list.

## Planned contents (M2)

* Entity/component storage with dense arrays and stable handles
* Transform hierarchy (parent/child, dirty propagation)
* Camera components (perspective/orthographic) producing view-projection matrices
* Frustum culling and draw-list generation
* Pass list consumed by `renderer::Renderer` (frame graph in M2)

## Status

Placeholder directory: no sources are compiled into `nova_engine` yet, so the
host build and the Android build are unaffected by it. The module lands with
M2 together with its doctest suite in `engine/tests/`.

Rules that apply the moment code arrives (see
[`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md)):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>`;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

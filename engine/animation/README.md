# `animation` — Nova Engine module

**Namespace:** `nova::animation`  ·  **Milestone:** M6  ·  **CMake flag:** `NOVA_ENABLE_ANIMATION`

ozz-based skeletal animation: sampling, blending, state machines, IK.

## Planned contents (M6)

* Skeleton/clip runtime representation
* Sampling jobs and blend trees
* Animation state machine
* IK solvers
* Skinning matrix output into the UBO/SSBO path

## Status

Placeholder directory: no sources are compiled into `nova_engine` yet, so the
host build and the Android build are unaffected by it. The module lands with
M6 together with its doctest suite in `engine/tests/`.

Rules that apply the moment code arrives (see
[`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md)):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>`;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

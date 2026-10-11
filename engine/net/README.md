# `net` — Nova Engine module

**Namespace:** `nova::net`  ·  **Milestone:** M8  ·  **CMake flag:** `NOVA_ENABLE_NET`

ENet transport and session layer for multiplayer.

## Planned contents (M8)

* ENet host/client wrappers with Nova error types
* Session lifecycle, connection events into the ECS
* Snapshot serialization and interpolation
* Basic client-side prediction hooks

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

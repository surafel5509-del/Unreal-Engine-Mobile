# `editor_core` — Nova Engine module

**Namespace:** `nova::editor_core`  ·  **Milestone:** M9  ·  **CMake flag:** `n/a (host-only)`

Host-only editor/tooling core: project model, asset database, undo stack. Never compiled into `libnova.so`.

## Planned contents (M9)

* Project/scene document model
* Asset database and import pipeline front-end
* Command/undo stack
* Host-side UI integration points

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

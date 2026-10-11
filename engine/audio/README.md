# `audio` — Nova Engine module

**Namespace:** `nova::audio`  ·  **Milestone:** M7  ·  **CMake flag:** `NOVA_ENABLE_AUDIO`

Oboe output, mixing, and spatialization. This is the one module allowed to own a second native thread (the audio callback).

## Planned contents (M7)

* Oboe stream + callback thread with lock-free ring feeding
* Voice management, mixer, gain/ducking on pause
* Spatialization (distance attenuation, panning)
* Asset-backed clips and streaming music

## Status

Placeholder directory: no sources are compiled into `nova_engine` yet, so the
host build and the Android build are unaffected by it. The module lands with
M7 together with its doctest suite in `engine/tests/`.

Rules that apply the moment code arrives (see
[`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md)):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>`;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

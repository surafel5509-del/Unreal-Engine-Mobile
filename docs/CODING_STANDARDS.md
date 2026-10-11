# Nova Engine — Coding Standards

These are the rules the M0/M1 codebase follows. They are binding for every
later milestone: a change that violates them is a bug, not a style preference.

Contents: [Language & toolchain](#1-language--toolchain) ·
[Naming](#2-naming) · [Files & modules](#3-files--modules) ·
[Error handling](#4-error-handling) · [Ownership & memory](#5-ownership--memory) ·
[Threading](#6-threading) · [Documentation](#7-documentation) ·
[Vulkan & NDK rules](#8-vulkan--ndk-rules) · [Performance](#9-performance) ·
[Testing](#10-testing) · [Kotlin rules](#11-kotlin-rules) ·
[Checklist](#12-pre-commit-checklist)

---

## 1. Language & toolchain

* **C++20**, `CMAKE_CXX_EXTENSIONS OFF` — standard C++ only, no GNU extensions.
* Compilers: NDK r26 Clang (Android), GCC 11+ / Clang 14+ (host). Code must
  build warning-free with `-Wall -Wextra -Wshadow` on both.
* Engine library: `-fno-exceptions`, `-fvisibility=hidden`, `-fPIC`.
* No RTTI-dependent code (`dynamic_cast`) in the engine runtime.
* Standard library is allowed and used (`std::vector`, `std::string`,
  `std::span`, `std::mutex`, `<chrono>`); the *owning pointer* aliases in
  `core/Types.h` are mandatory for heap ownership.

## 2. Naming

| Entity | Convention | Example |
| --- | --- | --- |
| Namespace | `nova::<module>[::<submodule>]` | `nova::rhi::vkutil`, `nova::core::log` |
| Type / class / struct / enum | `PascalCase` | `InputQueue`, `VulkanPipeline`, `FrameInfo` |
| Enum value | `PascalCase` | `TouchAction::PointerDown`, `Format::BGRA8_SRGB` |
| Function / method / variable | `camelCase` | `beginFrame()`, `colorFormat` |
| Member variable | `m_` prefix + camelCase | `m_device`, `m_engineTime` |
| Constant (`constexpr`) | `k` prefix + PascalCase | `kMaxFramesInFlight`, `kCacheLineSize` |
| Template parameter | `PascalCase` | `template <typename T, typename E>` |
| Macro | `NOVA_UPPER_SNAKE` | `NOVA_ASSERT`, `NOVA_LOG_ERROR` |
| File | `PascalCase.h` / `.cpp` for types, module-scoped | `VulkanDevice.cpp`, `Result.h` |
| CMake option | `NOVA_UPPER_SNAKE` | `NOVA_ENABLE_PHYSICS` |
| Kotlin class / fun / val | Android conventions (`PascalCase` / `camelCase`) | `NovaRuntime`, `onSurfaceLost` |

Abbreviations in type names stay capitalised (`VulkanDevice`, not
`Vulkandevice`); in `camelCase` identifiers an initial abbreviation is lower
(`uboSetLayout`, `vkResultName`).

## 3. Files & modules

* `#pragma once` in every header — never include guards.
* Header/implementation pairs: `Foo.h` + `Foo.cpp`. Header-only is allowed for
  templates, `inline` utilities, and enum-translation tables
  (`VulkanCommon.h`, `Math.h`, `Types.h`, `Result.h`, `Assert.h`).
* Include order in a `.cpp`: own header, then engine headers (quoted, relative
  to `engine/`), then third-party (angled), then standard library. Example:

  ```cpp
  #include "renderer/Renderer.h"

  #include "core/Assert.h"
  #include "core/Log.h"

  #include <glm/gtc/matrix_transform.hpp>

  #include <array>
  #include <cstring>
  ```

* Includes are relative to the `engine/` root (`#include "core/Types.h"`), which
  is the include directory every target gets.
* One module per directory; a module depends only on modules below it in the
  layer map (`core` → `platform`/`rhi` → `renderer` → `Engine`). Never include
  upward, never include across siblings except through their public headers.
* Only `engine/rhi/vulkan/` may include Vulkan headers; only
  `engine/platform/VirtualFileSystem.*` and `app/src/main/cpp/jni_bridge.cpp`
  may include Android NDK headers.
* Anonymous namespaces for translation-unit-local helpers; `inline` for
  header-local helpers. No `using namespace` in headers (scoped `using` of a
  single name inside a namespace body is used deliberately in `Types.h`).

## 4. Error handling

* **No exceptions in the engine runtime.** Fallible functions return
  `nova::Result<T, E>` (`core/Result.h`) or `bool` + an out parameter, and log
  the reason with `NOVA_LOG_ERROR`.
* `NOVA_ASSERT(cond)` — debug-only invariant; a failure means the *caller* is
  wrong. `NOVA_ENSURE(cond, "fmt", args…)` — always compiled; logs and lets the
  function return a failure value. Message is optional (`__VA_OPT__`).
* Every public entry point validates its arguments and returns a distinct error
  code. `EngineError` codes cross JNI unchanged as ints, and
  `engineErrorMessage()` maps them back to text.
* Cleanup on failure is mandatory and shares one path: `Engine::init` calls
  `shutdownLocked()` on any partial failure, so a half-initialized engine is
  indistinguishable from a never-initialized one.
* Resource creation that can fail (`createBuffer`, `createPipeline`, …) returns
  a null `Unique<T>`; callers check `isValid()`/null before use.
* Destructors never throw and never fail: teardown is `waitIdle()` →
  release children → release parents, in that order.

## 5. Ownership & memory

* `nova::Unique<T>` (= `std::unique_ptr`) for single ownership;
  `nova::Shared<T>` for the rare shared case; `nova::makeUnique<T>(…)` to
  construct. **No raw `new` / `delete` / `malloc` / `free`** outside
  `core/Memory.cpp` and the VMA-facing backend code.
* Non-owning references are `T&` (never null), `T*` (may be null, documented),
  or `std::span<const T>` for buffers.
* Every owning class is non-copyable unless copying is meaningful: derive from
  `core::NonCopyable` or `= delete` the copy operations (both patterns exist;
  `= delete` is preferred for new code because it is explicit at the type).
* RAII everywhere: a resource that needs a manual release also needs a comment
  explaining why the owner cannot be a smart pointer.
* CPU scratch memory uses `LinearArena` / `StackAllocator` / `PoolAllocator`;
  GPU memory is exclusively VMA's business (`rhi/vulkan/VulkanResources.cpp`).
* Alignment requests must be powers of two; allocator bases are 256-byte
  aligned so any request up to that is satisfiable.

## 6. Threading

* Every class with mutable state documents its thread-safety in the class
  comment, using one of these sentences:
  * *"Thread-safety: not thread-safe; use from the render thread only."*
  * *"Thread-safety: all public methods are safe to call from any thread."*
  * *"Thread-safety: `pushX` from the UI thread, `drain` from the render thread."*
* One mutex (`Engine::m_mutex`) serializes frame rendering against control
  operations. Subsystems do **not** add their own locks unless they own a
  second thread (audio callback in M7 will).
* Never block the UI thread on GPU work: pacing happens on the render thread.
* Never release a surface/window while a frame may be recording — the Kotlin
  side waits for `inFrame == false` first (`NovaRuntime`).
* Atomics are `std::atomic` with explicit memory orders where they matter;
  `@Volatile` in Kotlin.

## 7. Documentation

* **Doxygen comments on every public API** — class, struct, enum, function,
  constant. `///` style, one-line summary first, then details:

  ```cpp
  /// Record + submit nothing itself: fills `frame`'s command buffer.
  /// Returns false when pipelines could not be created for this format.
  bool render(rhi::FrameInfo& frame, float timeSeconds);
  ```

* Document: units (`dtSeconds`, `width` in pixels), ownership transfer
  ("takes ownership of `nativeWindow`"), thread expectations, failure modes,
  and lifetime constraints ("must run before the device is destroyed").
* Every `.h`/`.cpp` file starts with a banner comment naming the module and its
  responsibility; non-obvious algorithms get a block comment explaining *why*,
  not *what*.
* No `// TODO` without an owner and a milestone. M0–M3 code ships complete:
  placeholders are expressed as CMake options that print an honest status
  message, or as module directories with a README stating the milestone.

## 8. Vulkan & NDK rules

* **Vulkan 1.3 core only.** Never use an extension that was promoted into 1.3
  (`VK_KHR_dynamic_rendering`, `VK_KHR_synchronization2`,
  `VK_KHR_buffer_device_address`, `VK_EXT_debug_utils` → use 1.3 core
  equivalents where they exist). Never use a deprecated API
  (`vkCmdPipelineBarrier` → `vkCmdPipelineBarrier2`, `vkQueueSubmit` →
  `vkQueueSubmit2`).
* No `VkRenderPass` / `VkFramebuffer`: dynamic rendering with
  `VkPipelineRenderingCreateInfo` (note the field names are
  `depthAttachmentFormat` / `stencilAttachmentFormat`, **not**
  `depthStencilFormat`).
* All `VkResult` values go through `vkutil::checkVk(result, "what")` so failures
  are logged with a symbolic name.
* All `Vk*CreateInfo` structs are value-initialized (`{}`) and get their
  `sType` set explicitly; `pNext` chains are built with local structs, never
  temporaries.
* `vkDeviceWaitIdle()` before destroying anything that the GPU may still
  reference; resource destruction order is the reverse of creation order.
* VMA usage: `VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE` for GPU-local,
  `AUTO_PREFER_HOST` (+ persistent mapping) for CPU-written buffers. There is no
  `VMA_ALLOCATION_CREATE_PREFER_DEVICE_LOCAL_BIT` in VMA 3.x — do not add it.
* NDK: `ANativeWindow_fromSurface` must be balanced by `ANativeWindow_release`;
  `AAssetManager_fromJava` results are **not** owned and must not be released;
  `AAsset` must be closed. JNI string access via `GetStringUTFChars` must be
  paired with `ReleaseStringUTFChars` on every path, including error paths.
* JNI functions are `extern "C"` with the exact `Java_com_nova_engine_NativeBridge_*`
  spelling and no overloads (overloads would require the mangled signature
  suffix). Keep the bridge thin: translate, call, translate back.

## 9. Performance

* ARM64 is the primary target: no allocations in the frame loop, no virtual
  calls in per-element loops, contiguous data, `kCacheLineSize = 64` awareness.
* Prefer `reserve()` + `emplace_back` over repeated reallocation; reuse buffers
  across frames (`drain()` appends into caller-owned vectors).
* Bounded structures with explicit overflow policy (input queue drops oldest,
  log ring overwrites oldest) — never unbounded growth driven by external input.
* Clamp time deltas; never trust a frame time from the platform unclamped.
* Measure before optimizing: profiler integration (`NOVA_ENABLE_PROFILER`,
  Tracy) exists as a flag; hand-written NEON intrinsics require a profile.

## 10. Testing

* **Every non-trivial module gets a doctest suite** in `engine/tests/`,
  registered in `engine/tests/CMakeLists.txt` and thus in CTest.
* Test names are sentences: `TEST_CASE("Log level filtering drops records below the threshold")`.
* Use `REQUIRE` for preconditions whose failure makes the rest of the test
* meaningless, `CHECK` for independent assertions.
* GPU-facing code is tested through the **mock RHI** (`test_renderer.cpp`):
  implement `IDevice`/`ICommandBuffer`/`IBuffer`/…, record calls as a transcript
  string, assert the exact sequence. Do not skip a test because it "needs a
  device" — mock the device.
* Tests must not depend on execution order or on global state left by another
  test: re-`initialize()` singletons (log, console) and compare by content, not
  by absolute counters that saturate (the log ring caps at 512 records).
* Tests must be deterministic: no sleeping as a synchronization primitive; use
  the queue's own counters (see the 4-thread input stress test).
* New code must build warning-free in **both** configurations:

  ```bash
  cmake --build build/host -j && ctest --test-dir build/host
  cmake --build build/host-check -j && ctest --test-dir build/host-check
  ```

## 11. Kotlin rules

* Kotlin 1.9, `jvmTarget = 17`, no AndroidX UI dependencies in the app module.
* One class per file, file named after the class; package `com.nova.engine[.sub]`.
* Native calls only through `NativeBridge` (an `object`, `external fun`
  declarations with KDoc describing parameters, return codes, and thread).
* Kotlin's `Any` has **no** `wait`/`notify`: use
  `java.util.concurrent.locks.ReentrantLock` + `Condition` (`withLock {}`,
  `await()`, `signalAll()`) for the frame pump.
* Property initialisation order matters: a property referenced from an `init {}`
  block must be declared *above* it (`NovaSurfaceView.surfaceCallback`).
* `BuildConfig` fields are generated per build type — declare them in
  `buildTypes {}`, not `defaultConfig {}`, when debug and release differ
  (`NOVA_VALIDATION` is `true` in debug, always `false` in release).
* Nullable callbacks are invoked with `?.invoke(…)`; no `!!` in production code.
* Lifecycle: every `start()` has a `stop()`; every native init has exactly one
  shutdown, ordered after the render thread has been joined.

## 12. Pre-commit checklist

- [ ] Builds warning-free: host, host-check, and (if an SDK is available) `./gradlew assembleDebug`.
- [ ] `ctest --test-dir build/host` and `ctest --test-dir build/host-check` pass.
- [ ] New public APIs have Doxygen comments, units, and thread-safety notes.
- [ ] No exceptions, no raw `new`/`delete`, no `Vk*` types outside `rhi/vulkan/`.
- [ ] No deprecated Vulkan/NDK API; no extension that is core in Vulkan 1.3.
- [ ] JNI additions match on both sides (name, parameter types, return type) and
      are declared in `NativeBridge.kt` + implemented in `jni_bridge.cpp`.
- [ ] Shaders changed → `tools/compile_shaders.sh` re-run and the `.spv` files
      committed; `FrameUBO` layout still matches between GLSL and C++
      (`static_assert(sizeof(FrameUBO) == 128)`).
- [ ] New module → directory README + CMake option + placeholder status message.
- [ ] No `TODO` without an owner/milestone; no commented-out code.

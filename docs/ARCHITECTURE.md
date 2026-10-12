# Nova Engine — Architecture

This document describes the design of the engine as it exists at the end of
**Milestone M1** (Vulkan 1.3 renderer, gradient + rotating triangle demo), and
the seams where later milestones attach.

Contents:

1. [Design goals](#1-design-goals)
2. [Layer map](#2-layer-map)
3. [Threading model](#3-threading-model)
4. [Startup and shutdown sequence](#4-startup-and-shutdown-sequence)
5. [Frame timeline](#5-frame-timeline)
6. [RHI contract](#6-rhi-contract)
7. [Vulkan backend](#7-vulkan-backend)
8. [Renderer (M1)](#8-renderer-m1)
9. [Platform layer: input and VFS](#9-platform-layer-input-and-vfs)
10. [Core conventions: errors, memory, logging, console](#10-core-conventions)
11. [Build system](#11-build-system)
12. [Testing strategy](#12-testing-strategy)
13. [Performance rules for ARM64](#13-performance-rules-for-arm64)
14. [Extension points for later milestones](#14-extension-points-for-later-milestones)

---

## 1. Design goals

| Goal | Consequence in the code |
| --- | --- |
| UE5-class feature ceiling, mobile-first budget | Modular static library; every subsystem behind a CMake option; no desktop-only dependencies |
| Deterministic failure | No exceptions in the engine runtime (`-fno-exceptions`); `Result<T, Error>` on every fallible path; partial-init state is rolled back |
| No hidden allocations in the frame loop | Per-frame UBO ring, descriptor pool sized up-front, no `new`/`delete` after init |
| Portable core, swappable backend | `nova::rhi::IDevice` is the only GPU-facing interface; renderer never sees a `Vk*` type |
| Verifiable without a device | Host unit tests with a mock RHI; host compile-check of JNI/Vulkan/AAsset sources |
| Thin JNI | 10 entry points, primitives and framework objects only; all engine state native |

Naming, ownership, and documentation rules are in
[CODING_STANDARDS.md](CODING_STANDARDS.md).

---

## 2. Layer map

```
┌────────────────────────────────────────────────────────────────────┐
│ Kotlin app module (app/src/main/java/com/nova/engine)              │
│   MainActivity · NovaSurfaceView · InputManager · NovaAssetManager │
│   runtime/NovaRuntime (render-thread frame pump)                   │
└──────────────────────────┬─────────────────────────────────────────┘
                           │ JNI — app/src/main/cpp/jni_bridge.cpp
                           │ (libnova.so, the only SHARED target)
┌──────────────────────────▼─────────────────────────────────────────┐
│ nova::Engine                       engine/Engine.h/.cpp            │
│   facade: init · shutdown · frame · setSurface · input · console   │
│   owns: device, renderer, World + hierarchy, VFS, input, clock     │
└───────┬───────────────┬──────────────────────┬─────────────────────┘
        │               │                      │
┌───────▼──────┐ ┌──────▼───────────┐ ┌────────▼──────────┐
│ renderer     │ │ platform         │ │ core              │
│ Renderer     │ │ InputQueue       │ │ Types · Result    │
│ DrawList     │ │ VirtualFileSystem│ │ Assert · Log      │
└───┬───────┬──┘ └──────────────────┘ │ Console · Memory  │
    │       │                         │ Math · Time       │
    │   ┌───▼──────────────────────┐  └───────────────────┘
    │   │ scene                    │              ▲
    │   │ Entity · World · View    │──────────────┘ (depends on core only)
    │   │ Transform · SceneHierarchy│
    │   │ Camera · Frustum · Culling│
    │   └──────────────────────────┘
    │   ┌──────────────────────────┐
    │   │ assets (M2)              │──────────────┘ (depends on core only)
    │   │ Json · Base64 · DataUri  │
    │   │ glTF loader (next)       │
    │   └──────────────────────────┘
┌───▼──────────────────────┐
│ rhi                      │
│ IDevice · ICommandBuffer │
│ RhiTypes · RhiResources  │
│ DeviceFactory            │
│  ├─ vulkan/ (M1, enabled)│
│  └─ gles/   (post-M1)    │
└──────────────────────────┘
```

Dependency rule: arrows point downward only. `core` depends on nothing inside
the engine (GLM and the standard library are its only externals); `rhi` depends
on `core`; `scene` depends on `core`; `assets` depends on `core`; `renderer`
depends on `rhi` + `scene` + `core` (the draw list reads the World, never the
reverse); `platform` depends on `core`; `Engine` composes all of them. Nothing in
`core`/`platform`/`scene`/`assets`/`renderer` includes a Vulkan header — only
`rhi/vulkan/` does, which is what makes the mock-RHI tests possible.

`engine/scene` holds the M2 ECS (entity handles, sparse-set component storage,
`World`, `View`), the transform hierarchy (`Transform`/`Parent`/
`WorldTransform` components evaluated by `SceneHierarchy`), and camera/culling
(`Camera`, `Frustum`, `Bounds`, `cullForCamera`, `sortFrontToBack`). `Engine`
owns the `World` and runs the scene stage each frame; `renderer/DrawList.h`
defines the one component the renderer needs (`MeshRef`) plus the
`DrawCommand`/`SceneSnapshot` contract the two layers exchange. The
remaining module directories (`engine/assets`, `engine/physics`, …) are
placeholders for later milestones; each has a README stating what lands there
and which CMake flag enables it.

---

## 3. Threading model

Three threads touch the engine. Everything is serialized by one mutex inside
`nova::Engine` (`m_mutex`), so the native side needs no per-subsystem locking.

| Thread | Enters through | Work |
| --- | --- | --- |
| **UI thread** | `nativeInit`, `nativeSetSurface`, `nativeResize`, `nativeOnTouch`, `nativeOnKey`, `nativeOnPause`, `nativeOnResume`, `nativeExecConsole`, `nativeShutdown` | Lifecycle, input enqueue, console execution |
| **Render thread** (`nova-render`, created by `NovaRuntime`) | `nativeFrame(dt)` | `beginFrame` → record → `endFrame` (submit + present). Blocks here on FIFO pacing |
| **GPU / driver** | — | Async; synchronized through per-frame fences and semaphores |

`NovaRuntime` is the concurrency contract on the Kotlin side:

* `inFrame` is true exactly while the render thread is inside `nativeFrame`.
  It is claimed under `lock` (`ReentrantLock`) before the call and released
  after, with `idle.signalAll()`.
* `pause()` and `onSurfaceLost()` set their flag and then **await `!inFrame`**.
  Only after that does `onSurfaceLost()` call `nativeSetSurface(null)`, which
  performs `vkDeviceWaitIdle()` and releases the `ANativeWindow`. The window is
  therefore never freed while a frame is using it.
* The render thread parks whenever `paused || !surfaceReady`, so no frames are
  recorded without a swapchain.
* `stop()` clears `running`, signals, and joins (4 s bound). `MainActivity`
  calls `nativeShutdown()` **after** `runtime.stop()` returns, so no thread is
  inside native code when the engine is destroyed.
* When `nativeFrame` returns `FRAME_SKIPPED` (swapchain missing or being
  recreated), the pump sleeps 4 ms instead of spinning.

Delta time is measured on the Kotlin side with `System.nanoTime()` and clamped
to `MAX_FRAME_SECONDS = 0.25` so a stall (app resume, GC pause) cannot produce
a huge simulation step.

Engine-internal rule: **one frame is in flight per thread, up to two frames are
in flight on the GPU** (`kMaxFramesInFlight = 2`), each with its own command
pool, fence, and UBO slot.

---

## 4. Startup and shutdown sequence

```
MainActivity.onCreate
 ├─ NovaAssetManager.prepare(context)      → app-private writable dir (logs)
 ├─ NativeBridge.nativeInit(assets, dataPath, w, h, validation)
 │   └─ Engine::init(EngineConfig)          [holds m_mutex]
 │       ├─ Log: initialize, add stderr/logcat/file sinks
 │       ├─ Clock: start
 │       ├─ VFS: mountApk(AAssetManager) + mountData(dataPath)
 │       ├─ Console: register help/stats/log/vsync/time
 │       ├─ DeviceFactory::createDevice(desc) → Vulkan 1.3 instance/device/VMA
 │       ├─ Renderer::init(device, 4 SPIR-V blobs read via VFS)
 │       └─ on any failure → shutdownLocked() (full rollback) + Result<error>
 │   return 0 on success; non-zero EngineError otherwise → Activity.finish()
 ├─ NovaSurfaceView created; holder.addCallback(...)
 └─ runtime.start()                          → render thread parks (no surface yet)

SurfaceHolder.surfaceCreated
 └─ runtime.onSurfaceReady(surface)
     ├─ nativeSetSurface(surface)  → ANativeWindow_fromSurface + Engine::setSurface
     └─ surfaceReady = true; render thread wakes and starts presenting
```

Shutdown is the exact reverse, and is idempotent at every level:

```
onPause → runtime.pause() (waits for the frame) → nativeOnPause()
surfaceDestroyed → runtime.onSurfaceLost() → nativeSetSurface(null)
onDestroy → runtime.stop() (join) → nativeShutdown() → Engine::shutdown()
            ├─ device->waitIdle()
            ├─ Renderer::shutdown()      (all GPU resources released first)
            ├─ device.reset()            (swapchain → VMA → device → instance)
            └─ Log/Console/VFS teardown
```

`Engine::init` failure and `Engine::shutdown` share one code path
(`shutdownLocked()`), so a partially initialized engine tears down exactly like
a fully initialized one. `Renderer::shutdown()` is safe to call twice, and the
destructor calls `shutdown()` as a backstop.

Error codes cross JNI as plain ints (`nova::EngineError`):

| Code | Name | Meaning |
| --- | --- | --- |
| 0 | `Ok` | success |
| 1 | `InvalidArgument` | bad config/paths/null surface handle |
| 2 | `DeviceCreateFailed` | Vulkan 1.3 instance, device, or VMA creation failed |
| 3 | `AssetLoadFailed` | SPIR-V or asset missing/unreadable |
| 4 | `RendererInitFailed` | pipeline/buffer/shader-module creation failed |
| 5 | `AlreadyInitialized` | `init()` called twice |

---

## 5. Frame timeline

```
Engine::frame(dtSeconds)                      [render thread, holds m_mutex]
 ├─ if (!m_initialized || m_paused) → kFrameSkipped
 ├─ clock.tick(); m_engineTime += dt
 ├─ InputQueue::drain(touch, key)             → per-frame input snapshot
 ├─ device->beginFrame(FrameInfo& out)
 │    ├─ vkWaitForFences(frameFence[i])       ← CPU/GPU pacing point
 │    ├─ reset command pool + command buffer
 │    ├─ apply pending surface size → recreate swapchain if dirty
 │    ├─ vkAcquireNextImageKHR (3 retries on OUT_OF_DATE)
 │    └─ wait on imagesInFlight[image] if that image is still queued
 ├─ Engine::updateScene(width, height, SceneSnapshot&)   ← scene stage
 │    ├─ animate demo Transform (rotation = quatY(t·0.9) · quatZ(t·0.55))
 │    └─ renderer::runSceneStage(input, visible, commands, snapshot, &stats)
 │         ├─ camera.aspectRatio = width / height
 │         ├─ SceneHierarchy::update(world)   → WorldTransform for every entity
 │         ├─ cullForCamera(world, camera, visible, cullStats)  ← frustum test
 │         ├─ sortFrontToBack(world, cameraPosition, visible)
 │         ├─ buildDrawCommands(world, visible, commands)
 │         └─ snapshot = {viewProjection, time, aspect, span(commands)}
 │       (false → endFrame() + kFrameSkipped, nothing recorded)
 ├─ Renderer::render(frame, snapshot)
 │    ├─ ensurePipelines(frame.colorFormat)   ← lazy; rebuild on format change
 │    ├─ write UBO slice 0 (camera block) + slice i+1 per draw command
 │    ├─ commands->begin()                    ← dynamic rendering begins
 │    ├─ pass 1 "nova.background": bind UBO slice 0, draw 3 (full-screen tri)
 │    ├─ pass 2..N: one pass per DrawCommand — bind pipeline + VBO + UBO slice
 │    └─ commands->end()
 ├─ device->endFrame()
 │    ├─ vkQueueSubmit2 (synchronization2: one submit info, stage masks)
 │    └─ vkQueuePresentKHR
 │         ├─ OUT_OF_DATE / SUBOPTIMAL → mark swapchain dirty (recreate next frame)
 │         └─ DEVICE_LOST → m_deviceLost, log once, keep returning kFrameSkipped
 └─ ringIndex = (ringIndex + 1) % kFrameSlots  → returns kFrameRendered
```

Notes:

* `beginFrame` returning `false` makes `Engine::frame` return `kFrameSkipped`
  **without** recording or submitting; the renderer state is untouched, so the
  next frame resumes normally.
* The UBO write for frame *N* goes into slot `N % kFrameSlots` (2 slots, one per
  frame in flight), so an in-flight GPU read is never overwritten. The mock-RHI
  test asserts this alternation.
* Present mode is `Fifo` by default (guaranteed available, vsync-paced) and can
  be switched to `Mailbox` at runtime with the console command `vsync off`; the
  change applies with the next swapchain recreation.

---

## 6. RHI contract

`engine/rhi/IDevice.h` is the entire GPU-facing surface the renderer may use.

```cpp
namespace nova::rhi {

struct FrameInfo {
    ICommandBuffer* commands;     // recording target for this frame
    ITexture*       backbuffer;   // current swapchain image
    u32             width, height;
    Format          colorFormat;  // drives lazy pipeline creation
    u64             frameIndex;   // monotonically increasing
    u32             ringIndex;    // 0..kMaxFramesInFlight-1
};

class IDevice {
    // resources
    Unique<IBuffer>       createBuffer(const BufferDesc&);
    Unique<IShaderModule> createShaderModule(const ShaderModuleDesc&);
    Unique<IPipeline>     createPipeline(const PipelineDesc&);
    // surface / swapchain
    void          setSurface(void* nativeWindow, u32 width, u32 height);
    void          setSurfaceSize(u32 width, u32 height);   // deferred
    void          setPresentMode(PresentMode);             // deferred
    PresentMode   presentMode() const;
    // frame loop
    bool          beginFrame(FrameInfo& out);
    void          endFrame();
    void          waitIdle();
    bool          hasSurface() const;
    const DeviceStats& stats() const;
};

}  // namespace nova::rhi
```

`ICommandBuffer` records with explicit, backend-neutral transitions:

```cpp
begin();                                        // starts dynamic rendering
bindPipeline(IPipeline&);
bindVertexBuffer(const IBuffer*);
bindUniformBuffer(u32 slot, const IBuffer*, usize offset, usize range);
draw(u32 vertexCount, u32 firstVertex);
end();                                          // ends rendering + transitions
```

Design decisions worth knowing:

* **Slot semantics.** `bindUniformBuffer(0, …)` binds the frame UBO to
  `set = 0, binding = 0`, matching the `FrameUBO` block in every M1 shader.
  M1 exposes exactly one uniform slot; the mock tests assert the `slot == 0`
  precondition instead of silently ignoring other values.
* **All resources are `Unique<T>`** (`nova::Unique` = `std::unique_ptr` with the
  engine deleter conventions). The renderer cannot leak a GPU handle: destruction
  order is enforced by member declaration order and `waitIdle()` before teardown.
* **Formats are engine enums** (`nova::rhi::Format`), translated by
  `vkutil::toVkFormat` / `fromVkFormat`. The renderer never names a `VkFormat`.
* **No render passes.** With Vulkan 1.3 core dynamic rendering there is no
  `VkRenderPass`/`VkFramebuffer` anywhere in the backend; pipelines are created
  with `VkPipelineRenderingCreateInfo` listing attachment formats.
* **Deferred surface changes.** `setSurfaceSize` and `setPresentMode` record
  intent; `beginFrame` applies it, so no swapchain recreation happens while a
  frame is recording.

The GLES 3.2 backend (`engine/rhi/gles/`, `NOVA_ENABLE_RHI_GLES`) will implement
the same interface; `DeviceFactory::createDevice` selects the backend by
description and availability.

---

## 7. Vulkan backend

`engine/rhi/vulkan/` — the only place Vulkan headers appear.

| File | Responsibility |
| --- | --- |
| `VulkanCommon.h` | `VK_USE_PLATFORM_ANDROID_KHR`, `vkResultName`, `checkVk`, all enum translations |
| `VulkanResources.h/.cpp` | `VulkanBuffer` (VMA), `VulkanShaderModule`, `VulkanPipeline`, `VulkanTexture` |
| `VulkanDevice.h/.cpp` | Instance, device, VMA allocator, queues, swapchain, frame ring, descriptor pool, command recording, present |
| `../DeviceFactory.cpp` | Creates the device when `NOVA_VULKAN_COMPILED` is set |

Instance/device setup:

* Requests **Vulkan 1.3** (`apiVersion = VK_API_VERSION_1_3`); creation fails
  with a logged, explicit error if the loader reports less.
* Extensions: `VK_KHR_surface`, `VK_KHR_android_surface` (Android), plus the
  swapchain device extension. No deprecated `VK_KHR_*` promoted-to-core
  extensions are enabled (`dynamic_rendering`, `synchronization2`,
  `buffer_device_address` are core in 1.3 and used as core features).
* Validation layers are requested only when `EngineConfig::enableValidation`
  (i.e. `BuildConfig.NOVA_VALIDATION`) is true **and** the layer is present;
  absence logs a warning and continues.
* One graphics+present queue family (asserted to be the same family on Android);
  `VkPhysicalDeviceFeatures2` chain with `synchronization2` and
  `dynamicRendering` enabled.
* Memory: **VMA 3.1.0** with `VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE` for GPU-local
  buffers and `AUTO_PREFER_HOST` + `MAPPED` for the UBO ring and the vertex
  buffer (persistently mapped, written with `memcpy`, no per-frame map/unmap).

Swapchain:

* Created lazily on the first `setSurface`, not during `init`, so headless
  startup (and the unit tests) never needs a window.
* Format preference: `B8G8R8A8_SRGB` → `R8G8B8A8_SRGB` → `B8G8R8A8_UNORM` →
  `R8G8B8A8_UNORM`, paired with the matching color space; the chosen format is
  reported through `FrameInfo::colorFormat`.
* Present mode: `Mailbox` if available else `Fifo` (default request `Fifo`).
* Image count: clamped to `minImageCount + 1` within `[2, 3]`.
* Extent: clamped to `minImageExtent..maxImageExtent`, and the *clamped* extent
  is what the renderer sees (avoids the classic off-by-one OUT_OF_DATE loop).
* `OUT_OF_DATE`/`SUBOPTIMAL` from acquire or present marks the swapchain dirty;
  recreation happens at the top of the next `beginFrame`, after
  `vkDeviceWaitIdle()`.
* `setSurface(nullptr)` performs `waitIdle()` then releases swapchain, surface
  and `ANativeWindow` immediately.

Frame ring (`kMaxFramesInFlight = 2`): per-frame command pool, command buffer,
fence, and `imagesInFlight` semaphore tracking; acquire → record → `vkQueueSubmit2`
→ present, with the fence waited at the *start* of the frame that reuses the slot.

Descriptor management: one `VkDescriptorSetLayout` (`set = 0`, single
**non-dynamic** UBO binding, `VERTEX|FRAGMENT` stage flags) and a pool sized
`kDescriptorSetsPerFrame = 64` per frame. `bindUniformBuffer(slot, buffer,
offset, range)` allocates a fresh set from that pool, writes the buffer/offset
with `vkUpdateDescriptorSets` and binds it — so per-object UBO slices need no
shader change and no dynamic-offset support. The pool resets with the frame
ring; a frame may therefore bind at most 64 uniform slices, which is why the
renderer caps its draw list at `kMaxDrawCommandsPerFrame = 32` (33 binds with
the background pass).

---

## 8. Renderer (M2)

`engine/renderer/Renderer.h/.cpp` records a frame from a `SceneSnapshot`;
`engine/renderer/DrawList.h/.cpp` is the scene→renderer contract.

**Uniform block** (128 bytes, `std140`, `set = 0, binding = 0`, identical in
GLSL and C++, `static_assert(sizeof(FrameUniforms) == 128)`):

```cpp
struct FrameUniforms {         // GLSL: layout(std140, binding = 0) uniform FrameUBO
    math::Mat4 transform;      //  0..63   clip = transform * position
    math::Vec4 colorA;         // 64..79   gradient top
    math::Vec4 colorB;         // 80..95   gradient bottom
    math::Vec4 accent;         // 96..111  triangle tint / highlight
    f32 time;                  // 112      seconds since engine start
    f32 aspect;                // 116      width / height
    f32 pad0, pad1;            // 120..127 explicit std140 padding
};
```

**Per-object slicing.** Each frame slot owns one persistently mapped buffer of
`(kMaxDrawCommandsPerFrame + 1) · kUniformSliceBytes` = `33 · 256` = 8 448
bytes. Slice 0 holds the camera block (`transform = viewProjection`); slice
`i + 1` holds draw command *i* (`transform = viewProjection · world`, palette
and `time`/`aspect` copied from slice 0). 256 bytes satisfies every Vulkan
`minUniformBufferOffsetAlignment` (16/64/256) and lets each object bind its own
descriptor set at a plain offset — no dynamic offsets, no shader or SPIR-V
change from the M1 layout. Overflow beyond 32 commands is dropped and counted in
`RenderStats::droppedCommands` (logged once per frame).

**Draw list** (`renderer/DrawList.h`): `MeshRef{BuiltinMesh mesh, Vec4 tint}` is
the component that marks an entity drawable; `buildDrawCommands(world, visible,
out)` appends one `DrawCommand{mesh, world, tint}` per entity that owns both a
`MeshRef` and a `WorldTransform`, preserving the caller's order (so the scene
stage controls draw order via `sortFrontToBack`). `SceneSnapshot` bundles the
view-projection, `timeSeconds`, `aspect` and a `std::span` of the commands; the
span is non-owning and `Engine` reuses one vector across frames, keeping the
frame loop allocation-free.

**Scene stage** (`renderer::runSceneStage`): the per-frame pass — aspect into
the `Camera`, `SceneHierarchy::update`, `cullForCamera`, `sortFrontToBack`,
`buildDrawCommands`, snapshot fill — as one free function instead of code inside
`Engine::frame`. That is deliberate: `Engine::init()` needs a real Vulkan device,
so a scene stage buried in `Engine` could only be compile-checked on the host,
whereas `runSceneStage` is covered by 5 dedicated doctest cases (contents,
hierarchy re-evaluation, vector reuse, and both failure paths). It fails closed:
empty draw list, identity view-projection, `false`, and `Engine` skips the frame.

**Pass 1 — `nova.background`:** a full-screen triangle generated in the vertex
shader from `gl_VertexIndex` (3 vertices, no buffer bound), UVs computed from
the oversized triangle, fragment shader mixes `colorA → colorB` vertically with
a slow time-based hue shift and a subtle vignette. Reads slice 0.

**Pass 2..N — one pass per draw command:** 3 interleaved vertices
(`position vec3` @ location 0, `color vec3` @ location 1, stride 24) in a
host-visible buffer; Y-up coordinates, top vertex violet `#7C4DFF`, bottom-left
cyan `#00E5FF`, bottom-right pink `#FF4D8D`. The object's world matrix comes
from the ECS hierarchy and its tumble from `Engine::updateScene`
(`rotation = quatRotationY(t·0.9) · quatRotationZ(t·0.55)`), which reproduces
the M1 image exactly — `test_drawlist.cpp` pins both the camera matrix and the
tumble against `Renderer::viewProjection(aspect)`, the reference
`perspectiveVk(60°) · lookAt((0,0,3), origin, +Y)`.

Pipeline state: `frontFace = CLOCKWISE`, `cullMode = BACK`, no depth attachment
(opaque draws are ordered front-to-back by the scene stage, so no depth test is
needed), dynamic rendering formats from `FrameInfo::colorFormat`. Pipelines are
created lazily on first use and **rebuilt only when the color format changes** —
the mock test asserts the pipeline count goes 2 → 4 across a format change while
live resource tracking stays consistent.

`RenderStats` (draw calls, pipeline count, frame slot, dropped commands) is
exposed for the `stats` console command, which also prints World/hierarchy and
culling counters.

---

## 9. Platform layer: input and VFS

### InputQueue (`engine/platform/InputQueue.h/.cpp`)

* Fixed-capacity ring (default 256 events) of touch and key events, written by
  the UI thread, drained once per frame by the render thread.
* **Drop-oldest** overflow policy: a burst that exceeds capacity discards the
  oldest event and increments a counter (exposed via `pendingCount()` /
  `droppedCount()`), so input never blocks the UI thread and never grows
  unbounded.
* Tracks the last position of up to `kMaxTrackedPointers = 10` contacts
  (`lastPosition(pointerId, x, y)`), which gesture code needs without
  replaying the whole queue.
* Thread-safety: single internal mutex; `pushTouch`/`pushKey` from the UI
  thread, `drain`/`clear` from the render thread. Verified by a 4-thread
  producer stress test (2000 pushes, 0 dropped).
* Android `MotionEvent` action codes are passed through as ints and mapped to
  `nova::platform::TouchAction` (`Down/Move/Up/Cancel/PointerDown/PointerUp`).

### VirtualFileSystem (`engine/platform/VirtualFileSystem.h/.cpp`)

Two mounts, searched in order:

| Mount | Backing | Use |
| --- | --- | --- |
| `apk` | `AAssetManager` (Android) | Read-only shipped content: SPIR-V, models, textures |
| `data` | host filesystem under the app-private dir | Writable: logs, asset cache, saved state |

API: `mountApk`, `mountData`, `readAsset`, `readText`, `exists`, plus
`hasApkMount`/`hasDataMount`. Paths are normalized (`./`, `../`, duplicate and
leading slashes collapsed) and `../` escapes are rejected, so a hostile path
string cannot read outside the mounts. On host builds (`NOVA_HOST_ANDROID`) the
same code compiles against the `AAssetManager` shims in `tools/host_check/`,
which lets `test_vfs.cpp` exercise the real normalization and data-mount logic.

M3 (asset pipeline) extends this with a cooked-asset cache and streaming
without changing the call sites.

---

## 10. Core conventions

### Errors — `Result<T, Error>`

`engine/core/Result.h` provides a `Result<T, E>` with `isOk()`, `value()`,
`error()`, and `unwrapOr(fallback)`. Every fallible engine API returns one;
**no engine function throws**. The engine library is compiled with
`-fno-exceptions`; the JNI translation unit is the single exception boundary
(it may throw `IllegalStateException` for programmer errors such as calling
`nativeFrame` before `nativeInit`, and it converts JNI string failures into
error codes).

### Assertions — `NOVA_ASSERT` / `NOVA_ENSURE`

`engine/core/Assert.h`. `NOVA_ASSERT` is a debug-only invariant check;
`NOVA_ENSURE` is always compiled, logs with printf-style formatting
(`__VA_OPT__` makes the message argument optional), and returns false-ish
control flow at the call site. Both are used at API boundaries to make contract
violations loud in logcat rather than silent corruption.

### Memory — RAII, no raw `new`/`delete`

`engine/core/Memory.h/.cpp`:

| Type | Purpose |
| --- | --- |
| `nova::Unique<T>` / `Shared<T>` / `makeUnique<T>` | The only owning smart pointers used in engine code (`Types.h`) |
| `LinearArena` | Bump allocator for per-frame scratch; `reset()` reclaims everything |
| `StackAllocator` | Mark/release LIFO allocator for nested scratch scopes |
| `PoolAllocator` | Fixed-size element pool with an intrusive LIFO freelist |

Allocator blocks are over-aligned to `kBlockBaseAlignment = 256` bytes so
`allocate(size, 128)` can honour cache-line and SIMD alignment requests from a
malloc'd base. GPU memory is VMA's job; these allocators are CPU-side only.

### Logging — `nova::core::log`

Five levels (Trace/Debug/Info/Warn/Error), printf-style macros
(`NOVA_LOG_INFO`, …), pluggable sinks (stderr, Android logcat under
`__ANDROID__`, file), and a **512-entry ring buffer** of recent records that
survives crashes and is queryable through `forEachRetained` (used by the
`stats`/`log` console commands and by tests). `setLevel` filters at the source.
Thread-safe: one mutex around sink dispatch and ring writes.

### Console — `nova::core::Console`

Tokenizer (handles quoted arguments with escapes), command registry with help
strings, `exec(line) → std::string`, sorted `help` output, and replace-on-reregister
semantics. `Engine` registers `stats`, `log`, `vsync on|off`, `time`; the console
itself provides `help`. Executing before `init()` returns
`"engine not initialized"` rather than crashing. The JNI entry point
`nativeExecConsole` returns the output string to Kotlin.

### Time and math

`engine/core/Time.h` — monotonic clock, `tick()`, `deltaSeconds()`,
`elapsedSeconds()`, frame counters. `engine/core/Math.h` wraps GLM with engine
aliases (`Vec2/3/4`, `Mat4`, `Quat`) and provides `perspectiveVk` (Vulkan clip
space: Y flip + `[0,1]` depth) and `lookAt`, so no renderer code has to remember
the Vulkan/GL projection difference.

---

## 11. Build system

Two independent build paths share one `CMakeLists.txt` tree:

**Android** (AGP `externalNativeBuild` → root CMake):

```
libnova.so (SHARED) = app/src/main/cpp/jni_bridge.cpp
                    + nova_engine (STATIC)
                    + log, android, vulkan (NDK libs)
                    + Threads::Threads
```

`nova_engine` is compiled with `-fno-exceptions -fvisibility=hidden -Wall
-Wextra -Wshadow`, C++20, `-fPIC`; GLM is a `PUBLIC` dependency, Vulkan-Headers,
VMA and doctest are `PRIVATE`/`SYSTEM`. Only `jni_bridge.cpp` keeps exceptions
enabled, and only exported JNI symbols are visible.

**Host** (developer machine / CI):

```
nova_engine (STATIC, no Vulkan/JNI)   → nova_tests (doctest) → CTest
NOVA_HOST_CHECK=ON                    → nova_engine (+ Vulkan, shims)
                                        + nova_jni_check (OBJECT, jni_bridge.cpp)
```

`NOVA_HOST_CHECK` defines `NOVA_HOST_ANDROID=1` and adds
`tools/host_check/include` (shim `android/log.h`, `asset_manager.h`,
`asset_manager_jni.h`, `native_window.h`) plus a JDK `include` dir for `jni.h`
(the per-OS `jni_md.h` subdirectory is handled automatically). Sources guard
Android-only branches with `#if defined(__ANDROID__) || defined(NOVA_HOST_ANDROID)`,
and logcat output stays `__ANDROID__`-only.

Feature flags are listed in the root [README](../README.md#44-cmake-feature-flags).
`NOVA_ENABLE_LTO` turns on IPO for release builds when the toolchain supports it.

Gradle side: `settings.gradle.kts` (plugin management + `:app`),
root `build.gradle.kts` (AGP 8.5.2, Kotlin 1.9.24), `app/build.gradle.kts`
(namespace, SDK levels, `ndkVersion`, ABI filters, per-build-type
`NOVA_VALIDATION`, the `:app:compileShaders` task, packaging), and
`gradle.properties` for the knobs.

---

## 12. Testing strategy

| Level | What | Where |
| --- | --- | --- |
| Unit (host) | core containers/allocators, math, log ring, console, input queue (incl. multithreaded), VFS | `engine/tests/*.cpp`, doctest, CTest |
| Renderer (host) | Full `Renderer` against a **mock `IDevice`** that records every call as a string | `engine/tests/test_renderer.cpp` |
| Compile (host) | JNI bridge, Vulkan backend, AAsset/ANativeWindow paths | `-DNOVA_HOST_CHECK=ON` |
| Shaders (host) | GLSL → SPIR-V with glang at Vulkan 1.3 target | `tools/compile_shaders.sh` |
| Kotlin (build) | `kotlinc` against `android.jar` during `assembleDebug` | Gradle |
| On-device | Visual + logcat verification of the demo | manual / M9 tooling |

The mock RHI is the key trick: `MockDevice` implements `IDevice` and appends
tokens (`begin`, `pipe:<name>`, `ubo:<slot>@<offset>`, `vbo:<name>`,
`draw:<name>:<count>`, `end`) to a transcript. Tests then assert the **exact**
recorded sequence for a frame, the UBO slice offset of every draw, the written
uniform contents per slice, the UBO ring alternation across frames, pipeline
rebuild counts on format change, live-resource accounting after init/shutdown,
and that a failed `createShaderModule` rolls the renderer back to a clean state.
That gives renderer-behaviour coverage with no GPU, no device, and no Vulkan
loader.

Current totals: **133 test cases, 52 061 assertions** (M0/M1 suites plus the M2
ECS, transform-hierarchy, culling, draw-list, JSON and base64 suites), passing in
the plain host build under gcc, clang and clang+libc++, and in the host-check
build, with zero first-party compiler warnings under `-Wall -Wextra -Wshadow`.

CI runs the suite three ways on purpose. The Android NDK compiles this engine
against **libc++**, and libstdc++ disagrees with it in ways that only surface on
a device build — libc++ declares the floating-point `std::from_chars` overload
as *deleted*, which broke the JSON reader on arm64 while every host build stayed
green. The `clang-libcxx` matrix entry reproduces that standard library on the
host, so the next such break costs 40 seconds instead of an APK build.

---

## 13. Performance rules for ARM64

Rules the M0/M1 code already follows, and that later milestones must keep:

1. **No allocations in the frame loop.** UBO ring, descriptor sets, command
   buffers and input vectors are pre-sized at init; `drain()` reuses caller
   vectors.
2. **No virtual calls in hot inner loops.** Virtual dispatch is fine at the RHI
   boundary (a handful of calls per frame); per-vertex/per-entity loops iterate
   contiguous data with free functions.
3. **Cache-line awareness.** `kCacheLineSize = 64`; allocator blocks are
   256-byte aligned; hot structs avoid false sharing by keeping per-thread state
   separate (input queue has one mutex, taken once per frame, not per event).
4. **Contiguous data.** Interleaved vertex data in one host-visible buffer;
   `std::vector`/`std::span` over linked structures.
5. **NEON via the compiler.** Math is GLM (auto-vectorized); no hand-written
   intrinsics until profiling says so (M4).
6. **Deferred, not immediate.** Surface size/present-mode changes are applied at
   a safe point (`beginFrame`), never mid-record.
7. **Bounded work.** Input queue drops oldest instead of growing; log ring is
   fixed at 512; frame dt is clamped.

---

## 14. Extension points for later milestones

| Milestone | Attaches here |
| --- | --- |
| M2 ECS + scene | `engine/scene/` (`nova::scene`) — **ECS core landed**: `Entity`, `SparseSet`/`ComponentStorage<T>`, `World`, `View<Ts...>`. **Transform hierarchy landed**: `Transform`/`Parent`/`WorldTransform` components plus `SceneHierarchy` (CSR child lists, level-order evaluation, cycle/orphan reporting). **Cameras + culling landed**: `Camera` (perspective/orthographic, Vulkan clip space), `Frustum` (Gribb-Hartmann extraction for `[0,1]` depth), `Bounds`, `cullForCamera`, `sortFrontToBack`. **Scene-driven draw list landed**: `renderer/DrawList.h` (`MeshRef`, `DrawCommand`, `SceneSnapshot`, `buildDrawCommands`) — `Engine` owns the `World` and `Renderer::render` iterates the draw list instead of two hard-coded passes. Remaining: the frame graph, glTF/PBR assets and the GLES backend |
| M2 frame graph | Between `Engine::frame` and `Renderer::render`: a pass graph that allocates transient attachments through `IDevice` |
| M2 glTF/PBR | `engine/assets/` loaders → `rhi` buffers/textures; new pipelines registered by name. **Foundations landed**: strict JSON reader (`assets/Json.h`) and base64/data-URI decoding (`assets/Base64.h`), both `-fno-exceptions`-safe and fully host-tested; the glTF document model + `.glb` container read on top of them |
| M2 GLES backend | `engine/rhi/gles/` implementing `IDevice`; `DeviceFactory` picks it when Vulkan 1.3 is unavailable |
| M3 asset pipeline | `VirtualFileSystem` gains a cooked-cache mount; `NOVA_ENABLE_*` streaming thread |
| M5 physics | `engine/physics/` behind `NOVA_ENABLE_PHYSICS`, stepped from `Engine::frame` with a fixed timestep |
| M6 animation | `engine/animation/` (ozz), evaluated before rendering, writing into ECS transforms |
| M7 audio | `engine/audio/` (Oboe) on its own callback thread — the one place a second native thread is allowed |
| M8 Lua | `engine/scripting/` (Sol2); the JNI TU stays the only exception boundary |
| M9 UI/editor | `engine/ui/` (RmlUi) renders as an RHI pass; `engine/editor_core/` is host-only |

Every addition must keep the dependency rule (no upward includes), return
`Result` from fallible calls, ship doctest coverage, and document thread-safety
on the class — see [CODING_STANDARDS.md](CODING_STANDARDS.md).

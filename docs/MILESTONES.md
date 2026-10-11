# Nova Engine — Milestones

Milestone definitions, acceptance criteria, and current status. Work proceeds
one milestone at a time: a milestone is done when its acceptance criteria are
met **and** its code is covered by the host test/compile-check suite.

| # | Name | Focus | Status |
| --- | --- | --- | --- |
| M0 | Project skeleton | Gradle + NDK + CMake, JNI bridge, module layout, host build, test harness | ✅ complete |
| M1 | Vulkan renderer | Vulkan 1.3 RHI, swapchain, dynamic rendering, M1 demo scene | ✅ complete |
| M2 | Scene & ECS | ECS, scene graph, glTF 2.0, PBR, frame graph, GLES fallback | 🔄 in progress (ECS, transform hierarchy, cameras + culling landed) |
| M3 | Asset pipeline | Cooking, streaming, ASTC/KTX2 textures, asset cache | ⬜ planned |
| M4 | Rendering features | Shadows, IBL, HDR, post-processing, MSAA/TAA | ⬜ planned |
| M5 | Physics | Jolt integration, collision shapes, raycasts, character controller | ⬜ planned |
| M6 | Animation | ozz skeleton sampling, blending, state machines, IK | ⬜ planned |
| M7 | Audio | Oboe output, mixing, spatialization, asset-backed clips | ⬜ planned |
| M8 | Scripting & net | Lua (Sol2) gameplay layer, ENet multiplayer | ⬜ planned |
| M9 | UI & tooling | RmlUi in-game UI, editor core, on-device profiler overlay | ⬜ planned |

---

## M0 — Project skeleton (complete)

**Goal:** a checkout that builds an installable APK on a clean machine, with a
host-testable engine core and a documented layout.

Acceptance criteria and where each is satisfied:

| Criterion | Evidence |
| --- | --- |
| Gradle project with Kotlin DSL, AGP 8.x, wrapper committed | `settings.gradle.kts`, `build.gradle.kts` (AGP 8.5.2, Kotlin 1.9.24), `gradle/wrapper/` (Gradle 8.7), `gradlew`/`gradlew.bat` |
| NDK + CMake wired through `externalNativeBuild` | `app/build.gradle.kts` (`ndkVersion = 26.3.11579264`, `path = ../CMakeLists.txt`, `version = 3.22.1`) |
| Mandatory directory layout | `app/`, `engine/{core,platform,rhi/{vulkan,gles},renderer,scene,assets,physics,audio,animation,scripting,ui,net,editor_core,tests}`, `third_party/`, `shaders/`, `tools/`, `samples/`, `docs/` |
| Kotlin layer: activity, surface view, native bridge | `MainActivity.kt`, `NovaSurfaceView.kt`, `NativeBridge.kt`, `InputManager.kt`, `AssetManager.kt`, `runtime/NovaRuntime.kt` |
| JNI bridge with a thin, documented surface | `app/src/main/cpp/jni_bridge.cpp` — 10 entry points, signature-checked against `NativeBridge.kt` |
| `AndroidManifest.xml` with Vulkan requirement and rotation handling | `uses-feature android.hardware.vulkan.version = 0x00030000` (required), `configChanges`, `singleTask`, no runtime permissions |
| Engine core: types, errors, asserts, log, console, memory, math, time | `engine/core/*` |
| Platform layer: input queue, virtual file system | `engine/platform/*` |
| Modular CMake with feature options | Root `CMakeLists.txt` (`NOVA_ENABLE_*`, `NOVA_BUILD_TESTS`, `NOVA_HOST_CHECK`, `NOVA_ENABLE_LTO`, `NOVA_ENABLE_SANITIZERS`) |
| Unit-test harness for non-trivial modules | `engine/tests/` + CTest; 41 cases / 213 assertions at M0 close (61 / 30 629 including the M2 ECS suite) |
| Host compile-check of Android-only sources | `-DNOVA_HOST_CHECK=ON` with `tools/host_check/` shims |
| Third-party dependencies reproducible | Submodules (`vulkan-headers`, `glm`) + vendored single-header libs (`vk_mem_alloc` 3.1.0, `doctest` 2.4.11) |
| README with Android Studio + CLI instructions | Root [README.md](../README.md) |

---

## M1 — Vulkan renderer (complete)

**Goal:** a real-time Vulkan 1.3 renderer on an Android device showing an
animated gradient background with a rotating triangle at display refresh rate.

Acceptance criteria and where each is satisfied:

| Criterion | Evidence |
| --- | --- |
| Vulkan 1.3 instance + physical/logical device, no deprecated APIs | `rhi/vulkan/VulkanDevice.cpp` — `VK_API_VERSION_1_3`, core dynamic rendering, `vkQueueSubmit2` (synchronization2), no promoted-extension usage |
| VMA-backed buffers, host-visible UBO + vertex buffer, device-local ready | `rhi/vulkan/VulkanResources.cpp` (`VMA_MEMORY_USAGE_AUTO_PREFER_*`) |
| Swapchain created lazily from an `ANativeWindow`, correct format/extent/present mode, OUT_OF_DATE recovery | `VulkanDevice.cpp` (`setSurface`, `beginFrame`, format preference list, extent clamping, dirty-flag recreation) |
| RHI abstraction with a device factory and no `Vk*` leakage upward | `rhi/IDevice.h`, `rhi/RhiResources.h`, `rhi/RhiTypes.h`, `rhi/DeviceFactory.cpp` |
| Command recording with dynamic rendering and explicit transitions | `ICommandBuffer::begin/end`, `bindPipeline`, `bindVertexBuffer`, `bindUniformBuffer`, `draw` |
| Double-buffered frame ring with fences/semaphores, no CPU-side stalls beyond pacing | `kMaxFramesInFlight = 2`, per-frame command pool/fence, `imagesInFlight` tracking |
| Per-frame UBO ring with correct std140 layout shared by GLSL and C++ | `renderer/Renderer.h` (`FrameUBO`, 128 bytes, `static_assert`), `shaders/*.vert|frag` (`set = 0, binding = 0`) |
| Background gradient pass (full-screen triangle, no vertex buffer) | `shaders/background.vert|frag`, `nova.background` pipeline |
| Rotating triangle pass (interleaved pos+color, Y-up, Vulkan clip space) | `shaders/triangle.vert|frag`, `nova.triangle` pipeline, `math::perspectiveVk` |
| Pipelines rebuilt only when the attachment format changes | `Renderer::ensurePipelines` + mock-RHI test (2 → 4 pipelines on format change) |
| 60 FPS pacing (and correct behaviour at 120 Hz) | FIFO present mode by default; `vsync off` switches to Mailbox |
| Rotation/resize handled without process restart | Manifest `configChanges` → `nativeResize` → `setSurfaceSize` → deferred swapchain recreation |
| Pause/resume/background safe: surface released only after the frame ends | `NovaRuntime.pause()/onSurfaceLost()` await `!inFrame`; `nativeSetSurface(null)` does `waitIdle()` first |
| Input reaches the engine | `InputManager` → `nativeOnTouch`/`nativeOnKey` → `platform::InputQueue` → drained in `Engine::frame` |
| Console with runtime commands | `core/Console` + `Engine::registerConsoleCommands` (`help`, `stats`, `log`, `vsync`, `time`), reachable via `nativeExecConsole` |
| Shaders compiled to SPIR-V and shipped in the APK; regeneration automated | `shaders/*.spv` in `app/src/main/assets/shaders/`, `tools/compile_shaders.sh`, `:app:compileShaders` |
| Demo runnable out of the box from a plain checkout | Checked-in `.spv` + Gradle/CMake defaults; `./gradlew installDebug` then launch |
| Tests for the non-trivial parts | `engine/tests/test_renderer.cpp` (mock RHI: rollback, exact draw transcript, UBO alternation, format-change rebuild, frame skip, view-projection), plus core/platform suites |

**Known M1 limitations (by design, scheduled later):** no depth buffer or depth
test (M2/M4), no texture sampling (M2), no scene graph — the two passes are
hard-coded in `Renderer` (M2), single uniform slot (M2), Vulkan-only — the GLES
3.2 fallback backend directory exists but is not compiled (`NOVA_ENABLE_RHI_GLES`,
post-M1), and the console has no on-screen overlay (M9).

---

## M2 — Scene, ECS, glTF, PBR (in progress)

**Goal:** render a real 3D scene: an ECS-driven scene graph, glTF 2.0 models
with PBR materials, a frame graph for pass ordering and transient attachments,
and the GLES 3.2 fallback backend.

### Landed so far

✅ **ECS core** — `engine/scene/`:

| Piece | File | Notes |
| --- | --- | --- |
| Entity handles | `Entity.h` | slot index + generation; recycled slots bump the generation so stale handles fail `isAlive()`; `std::hash` specialization |
| Sparse-set storage | `ComponentStorage.h` | O(1) insert/find/erase (swap-remove), dense value array parallel to the index array, type-erased `IComponentStorage` purge path, RTTI-free `componentTypeId<T>()` |
| Registry | `World.h` / `World.cpp` | create/destroy with slot recycling, typed `add`/`remove`/`get`/`has`/`count`, storage vector indexed by type id, `Stats` diagnostics; no exceptions (mutations on dead handles log + fail) |
| Iteration | `View.h` | `View<Ts...>` intersects over the smallest matching storage; iterators yield `std::tuple<Entity, Ts&...>` for structured bindings and in-place mutation; `each()`, `collectEntities()`, `size()` |

Covered by `engine/tests/test_ecs.cpp` (20 cases: handle semantics, sparse-set
behaviour, non-trivial component moves, purge-on-destroy, view
intersection/mutation/empty-storage paths, 10 000-entity churn). Host and
host-check builds stay warning-free.

✅ **Transform hierarchy** — `engine/scene/`:

| Piece | File | Notes |
| --- | --- | --- |
| Transform components | `Transform.h` | `Transform` (T·R·S, trivially copyable), `Parent` (null handle == root), `WorldTransform` (cached world matrix) |
| Hierarchy evaluation | `SceneHierarchy.h` / `.cpp` | `setParent`/`detach` with dead-handle, self-parent and cycle rejection; `update()` derives CSR child lists from `Parent` components and evaluates level-order (breadth first) so parents are written before children, with no recursion depth tied to hierarchy depth |
| Robustness | `SceneHierarchy.cpp` | Destroyed parents demote children to orphan roots (`UpdateStats::orphans`); cycles written directly into `Parent` are reported (`cyclic`) and fall back to local matrices instead of hanging |

Child lists are derived per update rather than stored, so the hierarchy cannot
drift out of sync with the registry when entities are destroyed; the CSR buffers
are reused across frames (allocation-free in steady state).

Covered by `engine/tests/test_transform.cpp` (16 cases: matrix composition,
parenting/detach, cycle rejection, translation/rotation/scale inheritance,
three-level accumulation, idempotent re-update, Transform-less entities,
orphans, direct cycles, self-parenting, a 1 000-deep chain and an 8 192-entity
wide hierarchy).

✅ **Cameras + frustum culling** — `engine/scene/`:

| Piece | File | Notes |
| --- | --- | --- |
| Camera | `Camera.h` | Intrinsic parameters only (perspective FOV / orthographic half-height, aspect, near/far, priority); placement comes from the entity's `WorldTransform`, so cameras inherit the scene graph. `projectionMatrix` / `viewMatrix` / `viewProjectionMatrix` follow the Vulkan conventions from `core/Math.h` (Y flip, `[0,1]` depth); added `math::orthographicVk` and `math::inverse` |
| Frustum | `Frustum.h` / `.cpp` | Gribb-Hartmann plane extraction adapted to Vulkan's `[0,1]` clip depth (near = `row2`, not the classic `[-1,1]` form `row3 + row2`), normalized inward normals, point/sphere/AABB tests conservative toward keeping geometry |
| Culling | `Culling.h` / `.cpp` | `Bounds` component, `worldBounds()` (world AABB expanded by the absolute basis, so rotation and scale are handled), `cullFrustum()`, `cullForCamera()` (excludes the camera from its own list), `sortFrontToBack()` (allocation-free `std::sort`), `CullStats` diagnostics |

Entities without `Bounds` are kept and reported as `unbounded` rather than
dropped — an unknown extent is not a reason to discard a light, a camera or a
logical node.

Covered by `engine/tests/test_culling.cpp` (14 cases: projection conventions and
clip-space corners, view-matrix inversion, plane extraction with true
world-space near distances, sphere/AABB conservativeness, world AABB derivation
under translation and rotation, camera exclusion, non-camera failure paths,
orthographic volumes, parented cameras driven through `SceneHierarchy`,
front-to-back ordering, and a 4 000-object scene whose expectations are derived
from the depth slab rather than hard-coded counts).

### Remaining acceptance criteria

* `engine/scene/`: the pass list that lets `Renderer` iterate a culled world
  instead of drawing two hard-coded passes.
* `engine/assets/`: glTF 2.0 (binary + JSON) loader → meshes/materials/textures
  through the RHI; KTX2/ASTC decode path stubbed for M3.
* PBR pipeline: metallic-roughness BRDF, IBL-ready (full IBL in M4), depth
  attachment (`D32_SFloat`) with depth test enabled, MSAA optional.
* Frame graph: passes declare reads/writes; transient attachment allocation,
  automatic barrier/transition insertion; replaces the hard-coded two-pass loop.
* `engine/rhi/gles/`: `IDevice` implementation on OpenGL ES 3.2 with a
  capability-based fallback in `DeviceFactory` (Vulkan 1.3 → GLES 3.2 → error).
* `Engine` owns the `World` and drives it from `frame()`; the `stats` console
  command reports `World::Stats`.
* Sample: `samples/gltf_viewer` — load a bundled model, orbit camera, touch drag.
* Tests: transform hierarchy + culling math, glTF parsing (host, fixture files),
  frame-graph ordering, mock-RHI transcript for the new passes.

---

## M3–M9 (outline)

* **M3 Asset pipeline:** offline cooker (`tools/`), cooked-asset container with
  versioning, streaming manager, ASTC/KTX2 compression, VFS cache mount.
* **M4 Rendering features:** CSM shadows, IBL (irradiance + prefiltered env),
  HDR + tonemapping, bloom/DOF post chain, TAA/MSAA resolve, GPU-driven
  instancing.
* **M5 Physics:** Jolt behind `NOVA_ENABLE_PHYSICS`, broadphase, shapes,
  raycasts/sweeps, character controller, fixed-timestep stepping from
  `Engine::frame`, debug draw through the RHI.
* **M6 Animation:** ozz skeletons/clips, sampling jobs, blend trees, IK,
  skinning matrices into the UBO/SSBO path.
* **M7 Audio:** Oboe callback thread (the one extra native thread), mixer,
  voices, spatialization, asset-backed clips, ducking on pause.
* **M8 Scripting & networking:** Lua (Sol2) gameplay bindings, hot reload in
  debug; ENet transport, client/server session, snapshot interpolation.
* **M9 UI & tooling:** RmlUi in-game UI (incl. the on-screen console overlay),
  `engine/editor_core` host-only tooling, on-device profiler HUD, CI matrix
  (host tests + host-check + assembleDebug).

Each milestone lands with: module directory README updated, CMake option wired,
doctest coverage for the non-trivial parts, host-check still green, and the
README status table updated.

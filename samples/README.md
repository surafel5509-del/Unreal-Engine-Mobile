# `samples` — runnable Nova Engine samples

Samples are complete, runnable programs — not test harnesses. Each one is a
separate build target (an Android module, or a host executable once the editor
core lands) that exercises a specific slice of the engine and doubles as the
reference for how that slice is meant to be used.

## M1 sample: `hello_triangle` (the `app` module)

The Milestone 1 sample **is** the `app` module, so a plain checkout has a
runnable demo with nothing extra to configure:

```bash
./gradlew installDebug
adb shell am start -n com.nova.engine/.MainActivity
```

What it demonstrates:

| Feature | Where to look |
| --- | --- |
| Kotlin ↔ C++ lifecycle over JNI | `app/src/main/java/com/nova/engine/MainActivity.kt`, `app/src/main/cpp/jni_bridge.cpp` |
| Surface creation / resize / destruction without tearing down the engine | `NovaSurfaceView.kt`, `runtime/NovaRuntime.kt`, `VulkanDevice::setSurface` |
| Dedicated render thread with FIFO pacing | `runtime/NovaRuntime.kt` (`renderLoop`) |
| Vulkan 1.3 dynamic rendering, two passes, per-frame UBO ring | `engine/renderer/Renderer.cpp`, `engine/rhi/vulkan/` |
| SPIR-V loaded from APK assets through the VFS | `Engine::loadShader`, `engine/platform/VirtualFileSystem.cpp` |
| Touch/key input into the engine event queue | `InputManager.kt`, `engine/platform/InputQueue.cpp` |
| Runtime console | `core::Console`, `Engine::registerConsoleCommands`, `NativeBridge.nativeExecConsole` |

Console commands available in the sample: `help`, `stats`, `log <message>`,
`vsync on|off`, `time`. Call them from Kotlin:

```kotlin
val out = NativeBridge.nativeExecConsole("stats")   // e.g. from a debug gesture
android.util.Log.i("NovaDemo", out)
```

(An on-screen console overlay arrives with the UI module in M9.)

### Modifying the sample

Small, safe experiments — each one is a single-place edit:

* **Triangle geometry / colours:** `kTriangleVertices` at the top of
  `engine/renderer/Renderer.cpp` — interleaved `position (vec3)` + `color (vec3)`,
  Y-up. Keep the array a multiple of 6 floats; the draw call uses
  `size / stride` vertices.
* **Rotation speed:** `Renderer::render` — the tumble is
  `rotationY(time * 0.9f) * rotationZ(time * 0.55f)`.
* **Gradient colours:** the `FrameUBO` fill in `Renderer::render`
  (`colorA`, `colorB`, `accent`) and the mixing in `shaders/background.frag`.
* **Camera:** `Renderer::viewProjection` — 60° vertical FOV, near 0.1, far 100,
  camera at `+Z`. Uses `math::perspectiveVk`, which already applies the Vulkan
  Y flip and `[0,1]` depth range; do not "fix" the projection by hand.
* **Shaders:** edit `shaders/*.vert|*.frag`, then regenerate SPIR-V with
  `tools/compile_shaders.sh` (or `-Pnova.compileShaders=on`) and commit the
  `.spv` files. The `FrameUBO` layout in GLSL must stay byte-identical to the
  C++ struct (`static_assert(sizeof(FrameUBO) == 128)` guards it).
* **Add a third pass:** create a pipeline in `Renderer::ensurePipelines`, bind
  it in `render()` between `commands->begin()` and `commands->end()`, and extend
  the expected transcript in `engine/tests/test_renderer.cpp` — the mock-RHI test
  asserts the exact recorded call sequence, so it will fail loudly until updated.

Every change should keep both host builds green:

```bash
cmake --build build/host -j && ctest --test-dir build/host
cmake --build build/host-check -j && ctest --test-dir build/host-check
```

## Planned samples

| Sample | Milestone | Purpose |
| --- | --- | --- |
| `gltf_viewer` | M2 | Load a bundled glTF 2.0 model with PBR materials, orbit camera, touch drag |
| `ecs_boids` | M2 | Thousands of ECS entities to exercise storage layout and culling |
| `gles_fallback` | M2 | Force the GLES 3.2 backend (`NOVA_ENABLE_RHI_GLES`) and render the same scene |
| `asset_streaming` | M3 | Cooked-asset container, texture streaming, VFS cache mount |
| `shadow_scene` | M4 | CSM shadows, IBL, HDR tonemapping, post chain |
| `physics_playground` | M5 | Rigid bodies, raycasts, character controller, debug draw |
| `animation_rig` | M6 | Skinned mesh with blend tree and IK |
| `audio_room` | M7 | Spatialized voices, mixer, ducking on pause |
| `lua_sandbox` | M8 | Gameplay logic in Lua with hot reload |
| `net_lobby` | M8 | Two-device ENet session with snapshot interpolation |
| `ui_overlay` | M9 | RmlUi HUD + on-screen console |

New samples follow the same rules as engine modules: their own directory here, a
README stating what they demonstrate and how to run them, and (where logic is
non-trivial) doctest coverage in `engine/tests/`.

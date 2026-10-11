# Nova Engine

A from-scratch, UE5-class 3D game engine for Android. Kotlin front-end,
C++20 engine core, Vulkan 1.3 renderer (OpenGL ES 3.2 fallback planned),
built as strictly separated modules so subsystems can be compiled in or out.

```
Kotlin (Activity, Surface, input, assets)
        │  JNI — 10 thin entry points
        ▼
C++20 engine: core → platform → rhi → renderer → scene → …
        │
        ▼
Vulkan 1.3 (dynamic rendering, synchronization2, VMA)
```

**Current status: Milestones M0 + M1 complete.** The `app` module builds and
runs a real Vulkan renderer: an animated full-screen gradient with a rotating,
vertex-coloured triangle on top, presented at display refresh rate through a
double-buffered FIFO/Mailbox swapchain.

| Milestone | Scope | Status |
| --- | --- | --- |
| **M0** | Project skeleton: Gradle (Kotlin DSL) + NDK + CMake, manifest, JNI bridge, module layout, host build, test harness | ✅ done |
| **M1** | Vulkan 1.3 RHI + renderer: instance/device/VMA, swapchain, dynamic rendering, pipelines, per-frame UBO ring, input queue, console, gradient + rotating triangle demo | ✅ done |
| M2 | ECS, scene graph, glTF 2.0 loading, PBR shading, frame graph | planned |
| M3 | Asset pipeline: cooking, streaming, texture compression (ASTC/KTX2) | planned |
| M4 | Rendering features: shadows, IBL, post-processing, HDR | planned |
| M5–M9 | Physics (Jolt), animation (ozz), audio (Oboe), Lua scripting, UI (RmlUi), networking (ENet), tooling/editor core | planned |

See [docs/MILESTONES.md](docs/MILESTONES.md) for the roadmap and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

---

## 1. What you see when you run it

* A smooth vertical gradient (deep indigo → near black) that shifts over time,
  drawn as a full-screen triangle.
* A rotating triangle with per-vertex colours (violet / cyan / pink), spinning
  around the Z axis, with a projection-correct aspect ratio.
* Rotation follows display refresh (60 Hz and 120 Hz panels both pace correctly).
* Touch and key events flow into the engine's platform-independent input queue
  (`nova::platform::InputQueue`), drained once per frame.
* Rotating the device resizes the swapchain without recreating the process
  (`configChanges` + `nativeResize` + lazy pipeline rebuild for the new format).
* Backgrounding pauses the render thread; the surface is only released after
  the in-flight frame has finished, so the native window is never freed under
  a running frame.

Engine console (reachable from Kotlin via `NativeBridge.nativeExecConsole`):
`help`, `stats`, `log <msg>`, `vsync on|off`, `time`.

---

## 2. Requirements

### To build and run the app

| Tool | Version | Notes |
| --- | --- | --- |
| Android Studio | Koala 2024.1.1 or newer | Bundles the SDK manager; CLI-only works too |
| JDK | 17 | Required by AGP 8.5 |
| Android Gradle Plugin | 8.5.2 | Pinned in `build.gradle.kts` |
| Gradle | 8.7 | Wrapper included — do not install separately |
| Kotlin | 1.9.24 | Pinned in `build.gradle.kts` |
| Android SDK | compile/target 34, min 31 | Android 12 is the minimum OS |
| NDK | 26.3.11579264 | Pinned via `ndkVersion` |
| CMake | 3.22.1 | Installed through the SDK manager |
| Device | arm64-v8a, **Vulkan 1.3** | Physical device recommended |

The Vulkan 1.3 requirement is real, not nominal: the renderer uses core
dynamic rendering and synchronization2 instead of the deprecated
`VK_KHR_dynamic_rendering` / render-pass path. Most devices from 2022 onward
with Adreno 6xx/7xx, Mali-G7x and newer report Vulkan 1.3. Check with:

```bash
adb shell dumpsys SurfaceFlinger | grep -i vulkan   # or install a Vulkan info app
```

### To build the host targets (tests, compile checks)

CMake ≥ 3.22, Ninja, and a C++20 compiler (GCC 11+, Clang 14+, MSVC 19.30+).
No Android SDK needed.

---

## 3. Quick start

### 3.1 Clone (submodules are required)

```bash
git clone --recursive https://github.com/surafel5509-del/Unreal-Engine-Mobile.git
cd Unreal-Engine-Mobile

# If you already cloned without --recursive:
git submodule update --init --recursive
```

`third_party/vulkan-headers` and `third_party/glm` are git submodules;
`third_party/vk_mem_alloc` and `third_party/doctest` are vendored single-header
libraries (see [third_party/README.md](third_party/README.md)).

### 3.2 Android Studio

1. **File → Open** and select the repository root (the folder containing
   `settings.gradle.kts`).
2. Let Gradle sync. On the first sync, Studio offers to install missing SDK
   components (NDK 26.3.11579264, CMake 3.22.1) — accept.
3. Plug in a device (or start an arm64 emulator), pick **app** in the run
   configuration, and press **Run ▶**.
4. Watch logs in Logcat with the filter `tag:Nova` (the engine logs under
   `NovaEngine`, `NovaRuntime`, `NovaMain`).

Studio builds the native side through `externalNativeBuild`, so editing C++
triggers an incremental CMake/Ninja rebuild on the next Run.

### 3.3 Command line

```bash
# Debug APK (validation layers enabled by default)
./gradlew assembleDebug

# Install on the connected device
./gradlew installDebug
# …or manually:
adb install -r app/build/outputs/apk/debug/app-debug.apk

# Launch
adb shell am start -n com.nova.engine/.MainActivity

# Release APK (minified + shrunk, validation disabled)
./gradlew assembleRelease

# Logs
adb logcat -s NovaEngine:V NovaRuntime:V NovaMain:V AndroidRuntime:E

# Clean everything (Gradle + native)
./gradlew clean
```

On Windows use `gradlew.bat` instead of `./gradlew`.

### 3.4 Build knobs (`gradle.properties`)

| Property | Default | Effect |
| --- | --- | --- |
| `nova.abiFilters` | `arm64-v8a` | Comma-separated ABI list |
| `nova.validation` | `true` | Vulkan validation layer in **debug** builds (release is always `false`) |
| `nova.compileShaders` | `auto` | `auto` = use glslang if found, else keep checked-in `.spv`; `on` = fail without a compiler; `off` = always use checked-in `.spv` |
| `nova.glslang` | *(unset)* | Absolute path to `glslangValidator` / `glslang` / `glslc` |

Override per-invocation:

```bash
./gradlew assembleDebug -Pnova.validation=false
./gradlew assembleDebug -Pnova.compileShaders=on -Pnova.glslang=$HOME/vulkan/bin/glslangValidator
```

---

## 4. Host builds: unit tests and Android compile checks

Everything that does not need a GPU or an Android device is verified on the
host. This is the same entry point CI uses.

### 4.1 Unit tests (41 test cases, 213 assertions)

```bash
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNOVA_BUILD_TESTS=ON
cmake --build build/host -j
ctest --test-dir build/host --output-on-failure
```

Run the binary directly for doctest filtering:

```bash
./build/host/engine/tests/nova_tests                      # everything
./build/host/engine/tests/nova_tests --list-test-cases    # inventory
./build/host/engine/tests/nova_tests -tc='*renderer*'     # by name
./build/host/engine/tests/nova_tests -s                   # show successful asserts
```

| Test file | Covers |
| --- | --- |
| `test_core.cpp` | `Result<T,E>`, log ring buffer (capacity 512, ordering, level filtering), console tokenizer/quotes/help |
| `test_memory.cpp` | `LinearArena`, `StackAllocator` marks, `PoolAllocator` LIFO reuse, 32/64/128-byte alignment |
| `test_math.cpp` | Rotation matrices (X/Y/Z at 90°), `perspectiveVk` (Y flip, 0..1 depth), `lookAt` |
| `test_input.cpp` | FIFO order, drop-oldest overflow policy, `lastPosition`, 4-thread producer stress |
| `test_console.cpp` | Tokenization, quoted arguments, execution, unknown/empty commands, sorted `help`, command replacement |
| `test_vfs.cpp` | Data mount read/normalize, missing files, temporary directories |
| `test_renderer.cpp` | Mock-RHI: init + rollback on failure, exact recorded draw sequence, UBO ring alternation, pipeline rebuild on format change, frame skipping, view-projection aspect/Y-flip |

### 4.2 Compile-checking the Android-only code on the host

`app/src/main/cpp/jni_bridge.cpp`, the Vulkan backend, and the `AAssetManager`
/ `ANativeWindow` paths normally only compile inside the NDK. With
`NOVA_HOST_CHECK=ON` they are compiled against the shim headers in
`tools/host_check/`, which catches JNI/RHI regressions without a device:

```bash
cmake -S . -B build/host-check -G Ninja \
      -DNOVA_HOST_CHECK=ON -DNOVA_BUILD_TESTS=ON \
      -DNOVA_JNI_INCLUDE="$JAVA_HOME/include"
cmake --build build/host-check -j
ctest --test-dir build/host-check --output-on-failure
```

`NOVA_JNI_INCLUDE` may be omitted when `JAVA_HOME` points at a JDK. Details in
[tools/host_check/README.md](tools/host_check/README.md).

### 4.3 Sanitizers

```bash
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DNOVA_ENABLE_SANITIZERS=ON -DNOVA_BUILD_TESTS=ON
cmake --build build/asan -j && ctest --test-dir build/asan --output-on-failure
```

### 4.4 CMake feature flags

Every subsystem is an opt-in option (see the root `CMakeLists.txt`). Enabling a
subsystem that lands in a later milestone prints an honest status message
instead of silently doing nothing.

| Option | Default | Subsystem |
| --- | --- | --- |
| `NOVA_BUILD_TESTS` | `ON` | doctest unit tests (host only) |
| `NOVA_HOST_CHECK` | `OFF` | Compile-check JNI/Android sources on host |
| `NOVA_ENABLE_RHI_VULKAN` | `ON` | Vulkan 1.3 backend |
| `NOVA_ENABLE_RHI_GLES` | `OFF` | OpenGL ES 3.2 fallback (post-M1) |
| `NOVA_ENABLE_PHYSICS` | `OFF` | Jolt physics (M5) |
| `NOVA_ENABLE_AUDIO` | `OFF` | Oboe/miniaudio (M7) |
| `NOVA_ENABLE_LUA` | `OFF` | Sol2 scripting (M8) |
| `NOVA_ENABLE_NET` | `OFF` | ENet networking (M8) |
| `NOVA_ENABLE_UI` | `OFF` | RmlUi in-game UI (M9) |
| `NOVA_ENABLE_ANIMATION` | `OFF` | ozz animation (M6) |
| `NOVA_ENABLE_PROFILER` | `OFF` | Tracy integration |
| `NOVA_ENABLE_SANITIZERS` | `OFF` | ASan/UBSan |
| `NOVA_ENABLE_LTO` | `ON` | IPO for release builds |

---

## 5. Shaders

GLSL sources live in `shaders/`; the compiled SPIR-V that ships in the APK is
checked in at `app/src/main/assets/shaders/*.spv`, so **a plain checkout builds
without any shader toolchain**.

Regenerate them after editing a shader:

```bash
# CLI (glslangValidator / glslang / glslc from PATH, or $GLSLANG_VALIDATOR)
tools/compile_shaders.sh                       # -> app/src/main/assets/shaders
tools/compile_shaders.sh /tmp/out              # custom output dir
GLSLANG_VALIDATOR=/opt/glslang ./tools/compile_shaders.sh

# Or let Gradle do it during assembleDebug (task: :app:compileShaders)
./gradlew compileShaders
```

Both paths target Vulkan 1.3. Shader files: `background.vert/.frag`
(full-screen gradient triangle driven by `gl_VertexIndex`) and
`triangle.vert/.frag` (position + colour attributes, `FrameUBO` at
`set = 0, binding = 0`).

---

## 6. Repository layout

```
Unreal-Engine-Mobile/
├── app/                        Android application module (Kotlin + JNI glue)
│   ├── build.gradle.kts        AGP config, ABI filters, shader task
│   ├── proguard-rules.pro
│   └── src/main/
│       ├── AndroidManifest.xml Vulkan 1.3 feature, configChanges, singleTask
│       ├── assets/shaders/     Compiled SPIR-V shipped in the APK
│       ├── cpp/jni_bridge.cpp  The 10 JNI entry points (thin by design)
│       ├── java/com/nova/engine/
│       │   ├── MainActivity.kt     Lifecycle, immersive mode, engine boot
│       │   ├── NovaSurfaceView.kt  SurfaceView + surface callbacks
│       │   ├── NativeBridge.kt     JNI facade (external fun declarations)
│       │   ├── InputManager.kt     MotionEvent/KeyEvent -> engine events
│       │   ├── AssetManager.kt     App-private writable data dir
│       │   └── runtime/NovaRuntime.kt  Dedicated render-thread frame pump
│       └── res/                Theme, strings, adaptive icon
├── engine/                     C++20 engine (static lib `nova_engine`)
│   ├── CMakeLists.txt
│   ├── Engine.h/.cpp           Facade: init/shutdown/frame/input/console
│   ├── core/                   Types, Result, Assert, Log, Console, Memory, Math, Time
│   ├── platform/               InputQueue, VirtualFileSystem (APK + data mounts)
│   ├── rhi/                    IDevice contract, RhiTypes, DeviceFactory
│   │   ├── vulkan/             Vulkan 1.3 backend (device, swapchain, resources)
│   │   └── gles/               OpenGL ES 3.2 fallback (post-M1)
│   ├── renderer/               M1 renderer: gradient + triangle passes
│   ├── scene/ assets/ physics/ audio/ animation/
│   ├── scripting/ ui/ net/ editor_core/     (later milestones — see each README)
│   └── tests/                  doctest suites + CMake/CTest wiring
├── shaders/                    GLSL sources (compiled by tools/compile_shaders.sh)
├── samples/                    Runnable samples (M1 demo = the `app` module)
├── third_party/                Submodules + vendored single-header libs
├── tools/                      compile_shaders.sh, host_check shims
├── docs/                       ARCHITECTURE, CODING_STANDARDS, MILESTONES
├── CMakeLists.txt              Root build: options, Android + host paths
├── settings.gradle.kts         Gradle modules (`:app`)
├── build.gradle.kts            Plugin versions (AGP 8.5.2, Kotlin 1.9.24)
├── gradle.properties           Build knobs
└── gradlew / gradlew.bat / gradle/wrapper/   Gradle 8.7 wrapper
```

---

## 7. Troubleshooting

**`Vulkan 1.3 not supported` / `EngineError::DeviceCreateFailed` (code 2)**
The device or emulator does not report Vulkan 1.3. Use a physical device from
2022 or newer, or an emulator image with Vulkan 1.3 host support. The manifest
already declares `android.hardware.vulkan.version = 0x00030000` as required, so
Play filtering hides the app on unsupported devices.

**Validation layer warnings at startup, or init fails with validation on**
Some devices do not ship `VK_LAYER_KHRONOS_validation`. The engine logs a
warning and continues without it; to silence it build with
`-Pnova.validation=false`.

**`glm/glm.hpp: No such file or directory` or `vulkan/vulkan.h: No such file…`**
Submodules are missing:

```bash
git submodule update --init --recursive
```

**`NDK not configured` / `No version of NDK matched`**
Install NDK `26.3.11579264` (SDK Manager → SDK Tools → Show Package Details),
or change `ndkVersion` in `app/build.gradle.kts` to an installed 26.x.

**Native build fails and the error is hard to find**
The CMake/Ninja logs live under `app/.cxx/`; the configure log is also echoed
by `./gradlew externalNativeBuildDebug --info`.

**Black screen, no crash**
Check `adb logcat -s NovaEngine:V`. The engine logs swapchain format, present
mode, image count, and pipeline creation; `nativeFrame` returning
`FRAME_SKIPPED` (1) repeatedly means the surface was not attached yet.

**Shaders did not recompile**
`:app:compileShaders` is skipped when no compiler is found (`nova.compileShaders=auto`)
and the checked-in `.spv` files are used — Gradle prints a warning saying so.
Set `nova.compileShaders=on` to turn that into a build failure.

---

## 8. Documentation

* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — layers, threading model, RHI
  contract, frame timeline, memory and error-handling conventions.
* [docs/CODING_STANDARDS.md](docs/CODING_STANDARDS.md) — naming, types,
  ownership, and API-documentation rules the codebase follows.
* [docs/MILESTONES.md](docs/MILESTONES.md) — milestone definitions and the
  acceptance criteria M0/M1 satisfy.
* [third_party/README.md](third_party/README.md) — dependency provenance and
  licenses.
* [tools/host_check/README.md](tools/host_check/README.md) — host shims.

---

## 9. License

Engine sources are provided as-is for this project. Third-party components keep
their own licenses: glm (MIT), Vulkan-Headers (Apache 2.0), Vulkan Memory
Allocator (MIT), doctest (MIT). See [third_party/README.md](third_party/README.md).

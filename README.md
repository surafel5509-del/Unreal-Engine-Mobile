# Nova Engine

A mobile-first C++20 game-engine foundation with a Kotlin Android scene editor.
The default APK uses **OpenGL ES 2/3**, not Vulkan, and targets **Android 5.0+**.
It is an early, limited editor—not Unreal Engine, Unity, or a feature-complete
replacement for either product.

**Version 0.3.0: Nova Studio landscape workspace.**

- Android 5–7 (API 21–25): OpenGL ES 2.
- Android 8+ (API 26+): request OpenGL ES 3; retry ES 2 if unavailable.
- ARM32, ARM64, x86 and x86_64 in one APK; no `libvulkan.so` load dependency.
- Native procedural valley: editable terrain, water plane, trees, cabins and rocks.
- Landscape navy workspace: resizable hierarchy, viewport, inspector and asset dock.
- Eight mesh types, native ray picking, axis-constrained transforms, orbit/pinch,
  tint, visibility, rotation preview, 30-entry undo/redo and terrain height tools.
- Multiple local projects/scenes, prefabs, console command scripts, Android JSON
  import/export, recovery saves, frame caps and native statistics.

[Editor guide and limits](docs/ANDROID_EDITOR.md) ·
[Download the latest tested APK](https://github.com/surafel5509-del/Unreal-Engine-Mobile/releases/download/apk-latest/nova-engine-debug.apk)

The Vulkan backend remains in the repository as an optional development path.
Historical milestones below describe that work; the compatibility APK disables it.

| Milestone | Scope | Status |
| --- | --- | --- |
| **M0** | Project skeleton: Gradle (Kotlin DSL) + NDK + CMake, manifest, JNI bridge, module layout, host build, test harness | ✅ done |
| **M1** | Vulkan 1.3 RHI + renderer: instance/device/VMA, swapchain, dynamic rendering, pipelines, per-frame UBO ring, input queue, console, gradient + rotating triangle demo | ✅ done |
| **M2** | ECS, scene graph, glTF 2.0 loading, PBR shading, frame graph | 🔄 in progress — ECS core, transform hierarchy, cameras + frustum culling and the scene-driven draw list landed |
| M3 | Asset pipeline: cooking, streaming, texture compression (ASTC/KTX2) | planned |
| M4 | Rendering features: shadows, IBL, post-processing, HDR | planned |
| M5–M9 | Physics (Jolt), animation (ozz), audio (Oboe), Lua scripting, UI (RmlUi), networking (ENet), tooling/editor core | planned |

See [docs/MILESTONES.md](docs/MILESTONES.md) for the roadmap and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design.

---

## 1. What you see when you run it

A landscape editor opens with **Emerald Valley**, a small live low-poly 3D scene,
not a background image or a photorealistic open-world game. Drag the three blue-gray
panel dividers to resize the workspace; toolbars and asset shelves scroll sideways.

Tap an object to select it, or use the hierarchy. **Move / Rotate / Scale** and
**Free / X / Y / Z** control viewport drags. Numeric inspector values apply with
**Apply**. **Orbit** drags the camera, pinch zooms, and **Frame** centers selection.
Select Terrain for center-raise/lower, smoothing and height reset. **Game** enlarges
the preview with a camera-navigation pad; it is not a physics character controller.
**Play** rotates only objects whose **Rotate during Play** checkbox is enabled.

**Assets** contains Meshes, Scenes, Prefabs and console Scripts. Tap the project
name for the project manager. **Save** writes app-private JSON; recovery is separate.
**Build → Export project JSON** opens Android's file picker for portable backups.
Import accepts project bundles or single Nova scenes—not models/textures. Export
is **not an APK compiler**. This release supports 32 objects per scene.

The optional Vulkan source backend remains compile-checked, but the shipped APK
uses GLES. Physics, shadows, textures, skeletal animation, audio, particles, UI
design and gameplay scripting remain unimplemented; see the [feature boundaries](docs/ANDROID_EDITOR.md).

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
| Android SDK | compile/target 34, min 21 | Android 5+; GLES 2 minimum, GLES 3 preferred on Android 8+ |
| NDK | 26.3.11579264 | Pinned via `ndkVersion` |
| CMake | 3.22.1 | Installed through the SDK manager |
| Device | ARM32/ARM64/x86/x86_64 Android; **OpenGL ES 2+** | Physical device recommended |

Compatibility is negotiated with the actual EGL driver, not assumed from the
Android version alone. Emulator tests cannot guarantee every OEM/GPU driver;
physical-device testing remains necessary. No iOS build is included.

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
| `nova.abiFilters` | `arm64-v8a,armeabi-v7a,x86,x86_64` | Comma-separated ABI list |
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

### 4.1 Unit tests (133 test cases, 52 061 assertions)

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
| `test_renderer.cpp` | Mock-RHI: init + rollback on failure, exact recorded draw sequence, per-command UBO slice offsets and contents, draw-list budget/unknown-mesh drops, empty draw list, UBO ring alternation, pipeline rebuild on format change, frame skipping, view-projection aspect/Y-flip |
| `test_ecs.cpp` | M2 ECS: entity handles + generation recycling, sparse-set swap-remove, non-trivial component moves, purge-on-destroy, view intersection/mutation/empty-storage paths, 10 000-entity churn |
| `test_transform.cpp` | M2 hierarchy: `Transform::localMatrix` composition, parenting/detach, cycle rejection, world-matrix accumulation (translation/rotation/scale), orphans, direct-cycle detection, 1 000-deep chain, 8 192-entity wide hierarchy |
| `test_culling.cpp` | M2 cameras + culling: perspective/orthographic projection (Vulkan Y flip, `[0,1]` depth), view-matrix inversion, frustum plane extraction and point/sphere/AABB tests, world-space AABB derivation, camera exclusion, parented cameras, front-to-back sorting, 4 000-object scene |
| `test_drawlist.cpp` | M2 scene→renderer bridge: `buildDrawCommands` order/skip rules, append semantics, full scene stage (hierarchy → cull → sort → draw list), the camera entity reproducing the M1 reference view-projection, the tumble matrix, and a 2 000-object scene |
| `test_json.cpp` | M2 asset pipeline: strict JSON reader — scalars, the number grammar (leading zeros, `1.`, `+1`, `inf`/`nan`, overflow), string escapes and UTF-16 surrogate pairs, arrays, objects and duplicate keys, whitespace/BOM, depth limit, fallback getters, error offsets and line/column, a glTF-shaped document |
| `test_base64.cpp` | M2 asset pipeline: RFC 4648 vectors both ways, all-lengths/all-byte-values round trips, MIME whitespace, malformed input (padding position/count, non-zero trailing bits), data-URI splitting and decoding |

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
| `NOVA_ENABLE_RHI_GLES` | `OFF` | OpenGL ES 2/3 (enabled by the Android app) |
| `NOVA_ENABLE_PHYSICS` | `OFF` | Jolt physics (M5) |
| `NOVA_ENABLE_AUDIO` | `OFF` | Oboe/miniaudio (M7) |
| `NOVA_ENABLE_LUA` | `OFF` | Sol2 scripting (M8) |
| `NOVA_ENABLE_NET` | `OFF` | ENet networking (M8) |
| `NOVA_ENABLE_UI` | `OFF` | RmlUi in-game UI (M9) |
| `NOVA_ENABLE_ANIMATION` | `OFF` | ozz animation (M6) |
| `NOVA_ENABLE_PROFILER` | `OFF` | Tracy integration |
| `NOVA_ENABLE_SANITIZERS` | `OFF` | ASan/UBSan |
| `NOVA_ENABLE_LTO` | `ON` | IPO for release builds |

### 4.5 Continuous integration

Two GitHub Actions workflows run on every push and pull request:

| Workflow | File | What it proves |
| --- | --- | --- |
| `host-build` | [.github/workflows/host-build.yml](.github/workflows/host-build.yml) | Unit tests on gcc, clang **and** clang+libc++ (`build/host`) — the last one reproduces the standard library the Android NDK builds against, so device-only breaks fail on the host in seconds. A separate job compile-checks the Android-only sources (JNI bridge, Vulkan backend, `AAssetManager`/`ANativeWindow`) against the shims in `tools/host_check` (`build/host-check`). Zero first-party warnings; compiler errors are re-published as check annotations. |
| `android-apk` | [.github/workflows/android-apk.yml](.github/workflows/android-apk.yml) | The real device build path: JDK 17 + AGP 8.5.2 + Gradle 8.7 wrapper + NDK `26.3.11579264` + CMake `3.22.1` produce `app-debug.apk` for ARM32/ARM64/x86/x86_64 in one APK. The APK is inspected (`libnova.so`, `assets/shaders/*.spv`, `classes.dex`) and uploaded as the **`nova-engine-debug-apk`** artifact. |

After build, lint, packaging checks and API 21/26 emulator tests pass, the branch workflow republishes the APK as a **release asset**, which downloads
as the bare `.apk` (no zip wrapper) — the easiest route from a phone browser or
`curl`:

<https://github.com/surafel5509-del/Unreal-Engine-Mobile/releases/download/apk-latest/nova-engine-debug.apk>

The run's *Artifacts* section carries the same binary as
`nova-engine-debug-apk`; note that **GitHub artifacts always download as a
`.zip`**, so unzip first (`unzip nova-engine-debug-apk.zip`) — the APK inside is
named `nova-engine-debug.apk`. Install with:

```bash
adb install -r nova-engine-debug.apk
```

Each run also verifies the package before publishing it: `apksigner verify
--verbose --print-certs` (including v1 for Android 5/6), `zipalign -c 4`, and
`aapt dump badging` (asserting `sdkVersion:21` and all four ABIs). Android lint
checks API compatibility, and a `.sha256` checksum accompanies the APK. The output is
posted to the job summary and as `apk-verify` check annotations.

**Troubleshooting — "There was a problem parsing the package."** This dialog
comes from the device's package installer, not from the engine, and in practice
has three causes, in order of likelihood:

1. The file installed was the artifact **zip** (or a zip renamed to `.apk`).
   A real APK contains `AndroidManifest.xml`: `unzip -l file.apk`.
   Use the release link above, or unzip the artifact first.
2. The download was truncated (flaky mobile connection). Compare the file size
   against the asset size shown on the release page, or re-download over `adb`.
3. The device runs **Android 4.4 or older**. Version 0.2.0 requires Android 5.0
   / API 21 or newer. Check with `adb shell getprop ro.build.version.sdk`.

**Updates:** this is a debug-signed APK, not a production release. A fresh CI
runner can create a different debug key. If you see
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`, uninstalling the old app removes its data;
back up your scene first. Stable production signing is not configured.

Version 0.2.0 removes the Vulkan startup requirement and adds all four Android
ABIs. Android 5/6 installation is checked using the APK's v1 signature as well
as API-21 emulator installation.

**Troubleshooting — "App not installed."** This is a generic installer error,
not proof of a GPU/renderer problem. Download the new APK (not the artifact ZIP).
If it still fails, record the phone model, Android version, and the exact error
from a computer with Android platform-tools and USB debugging enabled:

```bash
adb shell getprop ro.product.model
adb shell getprop ro.build.version.sdk
adb shell getprop ro.product.cpu.abilist
adb install -r nova-engine-debug.apk
```

- `INSTALL_FAILED_NO_MATCHING_ABIS`: the APK does not contain your Android OS's
  native ABI. Use version 0.2.0+ for ARM and x86, in both 32- and 64-bit variants.
- `INSTALL_FAILED_OLDER_SDK`: this build needs Android 5.0 / API 21 or newer.
- `INSTALL_FAILED_UPDATE_INCOMPATIBLE`: signing-key conflict; uninstalling the
  existing app removes its data. Check other profiles/Secure Folder as well.
- `INSTALL_FAILED_INSUFFICIENT_STORAGE`: free internal storage and retry.
- Certificate/invalid-APK errors: re-download the original signed APK without
  modifying/repacking it and compare its SHA-256 with the release checksum.

Do not disable device security checks to work around an unexplained failure.

The runner has no `glslangValidator`, so `:app:compileShaders` skips itself
(`nova.compileShaders=auto`) and the APK ships the checked-in `.spv` files —
identical to a plain local checkout.

---

## 5. Shaders

The default GLES renderer uses embedded GLSL ES 1.00 sources in
`engine/renderer/GlesShaders.h`, accepted by both ES 2 and ES 3 contexts.
It does not load or compile SPIR-V. The following is for the optional Vulkan backend.

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
│       ├── AndroidManifest.xml GLES 2 feature, configChanges, singleTask
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
│   │   └── gles/               OpenGL ES 2/3 (enabled by the Android app)
│   ├── renderer/               Renderer: gradient pass + scene-driven draw list
│   ├── scene/                  M2 ECS, transform hierarchy, cameras, frustum culling
│   ├── assets/                 M2 loaders: strict JSON reader, base64/data URIs (glTF next)
│   ├── physics/ audio/ animation/
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

**`Vulkan 1.3 not supported` / old error-2 dialog**
You are running an older APK. Version 0.2.0 uses GLES and does not require Vulkan.
If its GLES initialization fails, collect `adb logcat -s nova NovaMain` plus the
phone model/Android version. A driver failure is not fixed by changing minSdk.

**Optional Vulkan builds: validation layer warnings at startup**
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
Check `adb logcat -s nova NovaMain NovaRuntime`. The engine logs swapchain format, present
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

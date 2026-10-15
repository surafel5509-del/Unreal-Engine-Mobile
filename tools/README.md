# `tools` — build and verification utilities

Everything here runs on a developer machine or in CI. Nothing in this directory
ships inside the APK.

| Tool | Purpose |
| --- | --- |
| [`compile_shaders.sh`](compile_shaders.sh) | Compile `shaders/*.vert|*.frag` to SPIR-V for Vulkan 1.3 |
| [`verify_apk_contents.py`](verify_apk_contents.py) | Check APK ZIP integrity and ELF architecture of all four Android native libraries |
| [`host_check/`](host_check/README.md) | Shim headers + stubs that let the Android-only sources compile on a host |

---

## `compile_shaders.sh`

```bash
tools/compile_shaders.sh                 # -> app/src/main/assets/shaders/*.spv
tools/compile_shaders.sh /tmp/out        # custom output directory
GLSLANG_VALIDATOR=/opt/glslang/bin/glslang tools/compile_shaders.sh
```

Compiler detection order:

1. `$GLSLANG_VALIDATOR` (absolute path to `glslangValidator`, `glslang`, or `glslc`)
2. `glslangValidator` / `glslang` / `glslc` from `PATH`

Installing one of them:

```bash
sudo apt install glslang-tools        # Debian/Ubuntu
brew install glslang                  # macOS
# or from the Vulkan SDK: $VULKAN_SDK/bin/glslangValidator
```

The script passes `--target-env vulkan1.3` (and `-V` for glslang, `-O` for
glslc), matching what the engine requests at runtime, and fails with a clear
message when no compiler is available.

**The compiled `.spv` files are checked in** at
`app/src/main/assets/shaders/`, so a plain checkout builds and runs without any
shader toolchain. Re-run this script (or the Gradle task below) whenever a
shader source changes, and commit the regenerated binaries.

Gradle equivalent — `:app:compileShaders`, wired as a `preBuild` dependency and
controlled by `gradle.properties`:

| Property | Behaviour |
| --- | --- |
| `nova.compileShaders=auto` (default) | Use a compiler if found; otherwise keep the checked-in `.spv` and warn |
| `nova.compileShaders=on` | Fail the build when no compiler is found |
| `nova.compileShaders=off` | Always use the checked-in `.spv` |
| `nova.glslang=/abs/path` | Explicit compiler path (also honours `$GLSLANG_VALIDATOR`) |

```bash
./gradlew compileShaders
./gradlew assembleDebug -Pnova.compileShaders=on -Pnova.glslang=$VULKAN_SDK/bin/glslangValidator
```

---

## `host_check/`

Minimal stand-ins for the NDK headers that the Android-only translation units
include (`android/log.h`, `android/asset_manager.h`,
`android/asset_manager_jni.h`, `android/native_window.h`), plus `shim_impl.cpp`
with `extern "C"` no-op definitions. They exist so CI and developers can compile
—and unit-test—the JNI bridge, the Vulkan backend, and the `AAssetManager` VFS
path without an Android SDK, NDK, device, or emulator.

Enable with:

```bash
cmake -S . -B build/host-check -G Ninja \
      -DNOVA_HOST_CHECK=ON -DNOVA_BUILD_TESTS=ON \
      -DNOVA_JNI_INCLUDE="$JAVA_HOME/include"
cmake --build build/host-check -j
ctest --test-dir build/host-check --output-on-failure
```

`-DNOVA_JNI_INCLUDE` may be omitted when `JAVA_HOME` points at a JDK. The build
handles the JDK layout where `jni_md.h` lives in a per-OS subdirectory
(`include/linux/`, `include/darwin/`).

Full details, the macro strategy (`__ANDROID__ || NOVA_HOST_ANDROID`), and the
limits of what the shims can verify are in
[`host_check/README.md`](host_check/README.md).

---

## Adding a tool

* Self-contained, documented at the top of the file (usage, inputs, outputs,
  failure modes).
* Host-only: never assume an Android SDK, NDK, device, or GPU.
* Prefer a CMake/CTest entry point so CI can run it (`enable_testing()` +
  `add_test`), and mention it in this README.
* Deterministic output: no timestamps, absolute paths, or locale-dependent
  formatting in generated files that get committed.

## APK native-library checks

```bash
python3 tools/verify_apk_contents.py app/build/outputs/apk/debug/app-debug.apk
python3 -m unittest discover -s tools/tests -p 'test_apk_contents.py' -v
```

All four ABI directories must contain `libnova.so` with matching ELF class/machine.
The CMake/NDK default links the C++ runtime statically; a packaged shared C++
runtime, if present, also has its architecture checked. These checks do not
replace device installation or GPU testing.

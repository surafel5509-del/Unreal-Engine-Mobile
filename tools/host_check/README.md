# Host compile-check (NOVA_HOST_CHECK)

Lets a plain Linux/macOS CI box compile the **Android-only** engine sources
without the NDK:

* `app/src/main/cpp/jni_bridge.cpp` (JNI entry points),
* `engine/platform/VirtualFileSystem.cpp` APK branch (`AAssetManager_*`),
* `engine/rhi/vulkan/*` (`VK_USE_PLATFORM_ANDROID_KHR`, `ANativeWindow`).

## What it provides

| Piece | Purpose |
| --- | --- |
| `include/android/*.h` | Faithful declarations of the NDK API subset the engine calls. |
| `shim_impl.cpp` | Linkable no-op definitions so host links succeed. |
| `NOVA_HOST_ANDROID` | Compile define that selects the Android code paths on a host. |
| `NOVA_JNI_INCLUDE` | Path to a real JDK's `include/` (for `jni.h`). |

The shims are **not** used by Android builds: with the NDK, the real headers
and `libandroid`/`liblog` provide these symbols.

## Usage

```bash
cmake -S . -B build/host-check \
  -DNOVA_HOST_CHECK=ON \
  -DNOVA_BUILD_TESTS=ON \
  -DNOVA_JNI_INCLUDE=/path/to/jdk/include
cmake --build build/host-check -j
ctest --test-dir build/host-check --output-on-failure
```

`NOVA_JNI_INCLUDE` can be omitted when `JAVA_HOME` is set.

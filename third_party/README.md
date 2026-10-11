# `third_party` — dependencies

Everything Nova Engine builds on, and how each dependency enters the build.

| Dependency | Version / commit | Form | Used by | License |
| --- | --- | --- | --- | --- |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | `v1.4.365` (`c468508`) | **git submodule** | `engine/rhi/vulkan/` (headers only — the loader comes from the NDK / device) | Apache 2.0 |
| [glm](https://github.com/g-truc/glm) | `master` (`6f14f47`) | **git submodule** | `engine/core/Math.h` and everything above it | MIT / Happy Bunny |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | 3.1.0 | **vendored** (`vk_mem_alloc/vk_mem_alloc.h`) | `engine/rhi/vulkan/VulkanResources.cpp` | MIT |
| [doctest](https://github.com/doctest/doctest) | 2.4.11 | **vendored** (`doctest/doctest.h`) | `engine/tests/` (host builds only) | MIT |

Planned for later milestones (added as submodules when the corresponding
`NOVA_ENABLE_*` option starts building real code): Jolt Physics (M5), ozz-animation
(M6), Oboe or miniaudio (M7), Sol2 + Lua (M8), ENet (M8), RmlUi (M9), Tracy (optional
profiler), cgltf or tinygltf (M2), KTX-Software / astc-encoder (M3).

---

## Submodules vs vendored — why both

**Submodules** are used for projects that are large, actively developed, and
header-only or header-plus-loader: `Vulkan-Headers` (the whole Vulkan API
surface) and `glm` (math, tracked upstream). They are pinned to a specific
commit, so a checkout is reproducible, and `git submodule update --remote` is an
explicit, reviewable action.

**Vendored single-header libraries** are used for one-file dependencies whose
version we want frozen inside the repository: VMA and doctest. Vendoring means
a plain `git clone` (without `--recursive`) can still build the host tests, and
no network access is needed to compile the Vulkan backend.

## Getting the sources

```bash
# Fresh clone, everything at once
git clone --recursive https://github.com/surafel5509-del/Unreal-Engine-Mobile.git

# Already cloned without --recursive
git submodule update --init --recursive

# Move a submodule to a newer upstream commit (deliberate, reviewable)
git submodule update --remote third_party/glm
git add third_party/glm && git commit -m "third_party: bump glm"
```

The vendored headers are ordinary tracked files — nothing to fetch.

## How they are wired into CMake

`engine/CMakeLists.txt`:

* `glm` — `PUBLIC` include directory (its headers appear in `engine/core/Math.h`,
  which is a public engine header).
* `third_party/vulkan-headers/include` — `SYSTEM` + `PRIVATE`: only
  `rhi/vulkan/` sees Vulkan types.
* `third_party/vk_mem_alloc` — `SYSTEM` + `PRIVATE`: VMA is an implementation
  detail of the Vulkan backend (`VMA_IMPLEMENTATION` is defined in exactly one
  translation unit).
* `third_party/doctest` — `SYSTEM` + `PRIVATE`, host test builds only.

All third-party includes are `SYSTEM`, so `-Wall -Wextra -Wshadow` never
surfaces upstream warnings as Nova warnings.

## Android notes

* The **Vulkan loader** (`libvulkan.so`) and the GL/EGL libraries come from the
  NDK/device; `Vulkan-Headers` supplies only headers. The root `CMakeLists.txt`
  links the loader with `find_library(nova_vulkan_lib vulkan REQUIRED)`.
* VMA is compiled with Vulkan 1.3 (`VMA_VULKAN_VERSION 1003000`) and no
  extension shims that 1.3 promotes to core.
* Nothing here requires Java/Kotlin interop, so the dependency set is identical
  for the Android and host builds.

## Licenses

Each subdirectory keeps its upstream license file (`LICENSE.txt`,
`copying.txt`, or the submodule's own `LICENSE`). All four current dependencies
are MIT or Apache 2.0 — permissive, attribution-style licenses compatible with
shipping a closed-source game. Attribution text for a shipping APK/AAB should
include the contents of those license files.

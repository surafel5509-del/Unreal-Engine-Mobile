# `gles` — OpenGL ES 3.2 RHI backend

**Namespace:** `nova::rhi::gles`  ·  **Milestone:** post-M1 (with M2)  ·  **CMake flag:** `NOVA_ENABLE_RHI_GLES` (default `OFF`)

Fallback backend for devices whose Vulkan support is below 1.3 — the same
`nova::rhi::IDevice` contract implemented on OpenGL ES 3.2, selected by
`rhi/DeviceFactory.cpp` when Vulkan device creation fails or the platform
reports an older Vulkan version.

## Planned contents

* `GlesDevice.h/.cpp` — `IDevice` implementation: EGL context/surface from an
  `ANativeWindow`, capability detection, swap behaviour and vsync mapping
  (`eglSwapInterval`) for `PresentMode`.
* `GlesResources.h/.cpp` — `IBuffer`/`ITexture`/`IShaderModule`/`IPipeline` on
  GL objects; GLSL ES 3.20 shader variants generated from the same sources in
  `shaders/` (SPIR-V is Vulkan-only).
* `GlesCommon.h` — `GLenum` error checking helper mirroring `vkutil::checkVk`,
  plus format/topology translation tables.
* Uniform-buffer mapping: GLES 3.2 UBOs at `set = 0, binding = 0` equivalent
  (`glUniformBlockBinding`), so `renderer::Renderer` needs no changes.

## Constraints

* The renderer and every layer above `rhi` must stay backend-agnostic: no `GL*`
  type may appear outside this directory, exactly as no `Vk*` type appears
  outside `../vulkan/`.
* Dynamic rendering has no GLES analogue — the backend translates the
  begin/end-rendering calls into framebuffer binding and load/store operations.
* Feature gaps (no `synchronization2` analogue, implicit barriers) are handled
  inside the backend and reported through `DeviceStats`.
* Tests: the mock-RHI suite in `engine/tests/test_renderer.cpp` already covers
  the contract; GLES-specific translation tables get their own host tests that
  do not require a GL context.

## Status

Placeholder directory — nothing here is compiled yet. Enabling
`NOVA_ENABLE_RHI_GLES` today prints an honest status message from the root
`CMakeLists.txt` instead of silently building nothing.

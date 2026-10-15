# OpenGL ES 2/3 backend

`GlesDevice.cpp` implements the existing `IDevice`/`ICommandBuffer` contracts
using EGL and the common GLES 2 API. Enabled for the Android app with
`NOVA_ENABLE_RHI_GLES=ON`, `NOVA_ENABLE_RHI_VULKAN=OFF`.

API 21–25 uses ES 2; API 26+ requests ES 3 and retries ES 2. Programs are built
from `renderer/GlesShaders.h`. CPU-backed buffer/shader descriptions survive
context recreation. EGL bindings are released between serialized native calls.

Supported scope: default-window framebuffer, triangle lists, one interleaved
position/color VBO, per-object uniform slices, material tint, depth testing,
FIFO pacing and context/surface recreation. This is not a full implementation
of every future RHI feature (textures, storage buffers, compute, instancing,
offscreen render targets). See [Android editor guide](../../../docs/ANDROID_EDITOR.md).

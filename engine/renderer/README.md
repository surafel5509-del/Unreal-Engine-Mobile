# `renderer` — Nova Engine module

**Namespace:** `nova::renderer`  ·  **Milestone:** M1 → M2 (in progress)  ·  **CMake flag:** n/a (always built)

Frame recording: turns a scene snapshot into RHI commands. Owns the GPU
resources it needs (frame UBO ring, vertex buffer, pipelines) and nothing else —
no scene state, no swapchain, no queue submissions.

## What is here now

| File | Contents |
| --- | --- |
| `Renderer.h` / `.cpp` | `FrameUniforms` (128 B `std140` block shared with the GLSL `FrameUBO`), `RenderStats`, `InitError`, and `Renderer`: lazy pipeline creation keyed on the color format, per-frame UBO slices, and `render(FrameInfo&, const SceneSnapshot&)` |
| `DrawList.h` / `.cpp` | The scene→renderer contract: `BuiltinMesh`, the `MeshRef` component, `DrawCommand`, `SceneSnapshot`, `buildDrawCommands(world, visible, out)` and `runSceneStage(input, visible, commands, snapshot, outStats)` — the whole per-frame scene pass as one host-testable function |

### Frame UBO layout

Each frame slot owns one persistently mapped buffer of
`(kMaxDrawCommandsPerFrame + 1) · kUniformSliceBytes` = `33 · 256` = 8 448 bytes:

| Slice | Contents |
| --- | --- |
| 0 | Camera block: `transform = viewProjection`, palette (`colorA`/`colorB`/`accent`), `time`, `aspect` — read by the background pass |
| 1 … 32 | One `FrameUniforms` per draw command: `transform = viewProjection · world`, palette/time/aspect copied from slice 0 |

256 bytes satisfies every Vulkan `minUniformBufferOffsetAlignment` (16/64/256),
and because `ICommandBuffer::bindUniformBuffer(slot, buffer, offset, range)`
allocates a fresh descriptor set per bind (`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`,
non-dynamic, pool of `kDescriptorSetsPerFrame = 64`), per-object data needed
**no shader or SPIR-V change** from the M1 layout. Overflow past 32 commands is
dropped, counted in `RenderStats::droppedCommands` and logged once per frame.

### Recorded frame

```
beginRendering
  bindPipeline(nova.background);  bindUniformBuffer(0, ubo, 0);    draw(3)
  for each DrawCommand i:
    bindPipeline(mesh);  bindVertexBuffer(mesh);
    bindUniformBuffer(0, ubo, (i + 1) * 256);  draw(vertexCount)
endRendering
```

One dynamic-rendering scope per frame; opaque draws arrive front-to-back from
the scene stage, so no depth attachment is needed yet (M2 PBR adds one).

### Draw-list construction

`buildDrawCommands` is the low-level step; `runSceneStage` is what a frame loop
actually calls — it writes the viewport aspect into the camera, evaluates the
hierarchy, culls, sorts and fills both the command vector and the snapshot:

```cpp
#include "renderer/DrawList.h"

// per frame, after animating whatever moves:
renderer::SceneStageInput input{};
input.world = &world;
input.hierarchy = &hierarchy;
input.cameraEntity = cameraEntity;
input.timeSeconds = engineTime;
input.aspect = static_cast<float>(width) / static_cast<float>(height);

renderer::SceneStageOutput output{};
std::vector<scene::Entity> visible;             // reused across frames
std::vector<renderer::DrawCommand> commands;    // reused across frames
renderer::SceneSnapshot snapshot{};

if (renderer::runSceneStage(input, visible, commands, snapshot, &output)) {
    renderer.render(frameInfo, snapshot);       // output.cull feeds `stats`
}
```

Building a draw list by hand (several passes into one vector, custom ordering)
still works:

```cpp
std::vector<scene::Entity> visible;
scene::CullStats stats{};
scene::cullForCamera(world, cameraEntity, visible, stats);
scene::sortFrontToBack(world, scene::cameraPosition(cameraWorld), visible);

std::vector<renderer::DrawCommand> commands;
renderer::buildDrawCommands(world, visible, commands);

renderer::SceneSnapshot snapshot{};
snapshot.viewProjection = scene::viewProjectionMatrix(camera, cameraWorld);
snapshot.timeSeconds = engineTime;
snapshot.aspect = width / height;
snapshot.commands = commands;                  // non-owning view
renderer.render(frameInfo, snapshot);
```

Semantics worth knowing:

* **An entity is drawn when it owns a `MeshRef` with a real mesh *and* a
  `WorldTransform`.** A drawable that never went through
  `SceneHierarchy::update()` is skipped rather than drawn at the origin.
* **Order is the caller's.** `buildDrawCommands` appends in the order of
  `visible` and never clears `out`, so a frame can concatenate several passes
  into one vector.
* **`SceneSnapshot::commands` is a `std::span`.** The vector must outlive the
  render call; `Engine` reuses one vector so the frame loop stays
  allocation-free.
* **`runSceneStage` fails closed.** A missing/destroyed camera (or a null
  world/hierarchy pointer) returns `false` with an empty draw list and an
  identity view-projection; `Engine` skips the frame rather than rendering
  garbage.
* **The renderer never reads the `World` during recording.** The dependency runs
  one way (`renderer → scene → core`), which keeps `MeshRef` the only
  scene-visible renderer type.
* **Thread-safety:** `render()` runs on the render thread under `Engine`'s frame
  mutex; `init()`/`shutdown()` run during engine init/teardown and must not
  overlap a frame.

## Adding a mesh

1. Add an enumerator to `BuiltinMesh`.
2. Return its pipeline from `Renderer::pipelineFor()` and its vertex count from
   `Renderer::vertexCountFor()` (plus the buffer itself, and a second vertex
   buffer once meshes differ in layout).
3. Give entities `MeshRef{BuiltinMesh::kFoo}` and a `Transform` + `Bounds`.

glTF-loaded meshes (M2) will replace step 1–2 with a resource handle alongside
the builtin path.

## Tests

`engine/tests/test_renderer.cpp` (mock RHI: exact draw transcript with UBO slice
offsets, per-slice uniform contents, budget drops, unknown-mesh skips, empty
draw list, UBO ring alternation, pipeline rebuild on format change, init
rollback) and `engine/tests/test_drawlist.cpp` (draw-list construction, the full
scene stage, the camera entity reproducing `Renderer::viewProjection()` element
by element, a 2 000-object scene).

## Rules for new code here

See [`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md):

* depend on `rhi`, `scene` and `core` — never on `Engine` or `platform`;
* no Vulkan headers outside `rhi/vulkan/`;
* no exceptions — return `nova::Result<T, E>` or a failure value;
* no allocations in the frame path (reuse buffers, vectors and descriptor sets);
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

# `scene` — Nova Engine module

**Namespace:** `nova::scene`  ·  **Milestone:** M2 (in progress)  ·  **CMake flag:** n/a (always built)

Scene representation: entity/component storage, transform hierarchy, cameras,
culling. `nova::Engine` owns the `World` and runs the scene stage each frame;
the result reaches the GPU as a `renderer::SceneSnapshot` draw list (see
[`engine/renderer/README.md`](../renderer/README.md)). This module depends on
`core` only and never includes a renderer or RHI header — the single component
the renderer needs (`MeshRef`) is declared on the renderer side.

## What is here now (M2)

| File | Contents |
| --- | --- |
| `Entity.h` | `Entity` handle = slot index + generation; `kNullEntity`; `std::hash` specialization |
| `ComponentStorage.h` | `SparseSet` (O(1) index ↔ dense position, swap-remove), `IComponentStorage` (type-erased purge path), `ComponentStorage<T>` (dense value array parallel to the index array), `componentTypeId<T>()` (RTTI-free per-type ids) |
| `World.h` / `World.cpp` | Entity registry (create/destroy with slot recycling and generation bumping), typed component API (`add`/`remove`/`get`/`has`/`count`), storage registry indexed by type id, `Stats` diagnostics |
| `View.h` | `View<Ts...>` intersection iteration over the smallest matching storage; iterators yield `std::tuple<Entity, Ts&...>` (structured bindings, in-place mutation); `each()`, `collectEntities()`, `size()` |
| `Transform.h` | `Transform` (local T·R·S, `localMatrix()`), `Parent` (null handle == root), `WorldTransform` (cached world matrix) — all trivially copyable |
| `SceneHierarchy.h` / `.cpp` | `setParent`/`detach` (rejects dead handles, self-parenting, cycles), `isAncestorOf`, `depthOf`, `childrenOf`, and `update()` which derives CSR child lists from `Parent` components and evaluates them level-order into `WorldTransform` |
| `Camera.h` | `Camera` component (perspective/orthographic intrinsics, `priority`), `projectionMatrix()` / `viewMatrix()` / `viewProjectionMatrix()` in Vulkan clip space; placement comes from the entity's `WorldTransform`, so cameras can be parented |
| `Frustum.h` / `.cpp` | Gribb-Hartmann plane extraction for Vulkan's `[0, 1]` clip depth, normalized inward normals, `contains()` / `intersectsSphere()` / `intersectsAabb()` (conservative toward keeping geometry) |
| `Culling.h` / `.cpp` | `Bounds` component, `worldBounds()` (world AABB expanded by the absolute basis), `cullFrustum()`, `cullForCamera()` (excludes the camera itself), `sortFrontToBack()`, `CullStats` diagnostics |

```cpp
#include "scene/View.h"

nova::scene::World world;
const auto entity = world.create();
world.add<Transform>(entity, Transform{});

for (auto [e, transform] : world.view<Transform>()) {
    transform.position.y += 1.0f;
}
```

Semantics worth knowing before using it:

* **Handles, not pointers.** A destroyed entity's slot is recycled with a bumped
  generation, so a stale `Entity` fails `isAlive()` instead of aliasing a new
  entity.
* **No exceptions.** Mutating a dead/null entity logs an error and returns
  `nullptr`/`false`; queries report absence silently. Internal invariants use
  `NOVA_ASSERT`.
* **Iteration order is unspecified.** Erase is a swap-remove, so dense order
  changes; sort view results when order matters (M2 culling sorts by distance).
* **Component references are transient.** They are invalidated by any later
  erase of that component type — never cache them across structural changes.
* **Structural changes invalidate live views.** Collect entities first
  (`collectEntities`) when a system needs to create/destroy while iterating.
* **Thread-safety:** not thread-safe and deliberately lock-free; one `World` is
  owned and driven by a single simulation thread.

Hierarchy semantics worth knowing:

```cpp
nova::scene::SceneHierarchy hierarchy;
hierarchy.setParent(world, child, root);   // adds identity Transforms if missing
hierarchy.update(world);                   // writes WorldTransform for every owner

const auto& stats = hierarchy.lastStats();  // roots / written / orphans / cyclic / depth
```

* Child lists are **derived** from `Parent` components on every `update()`, so a
  destroyed parent can never leave stale hierarchy state behind — its children
  are demoted to orphan roots and counted in `UpdateStats::orphans`.
* Evaluation is **level-order** (breadth first) over entity indices: parents are
  always written before children read them, and stack usage does not grow with
  hierarchy depth (verified with a 1 000-deep chain).
* **Cycles cannot hang** the traversal: entities that form one are never reached
  from a root, are counted in `UpdateStats::cyclic`, get their local matrix as a
  best-effort world matrix, and are logged once. `setParent` rejects cycles up
  front, so this path only triggers on `Parent` components written directly.
* CSR scratch buffers (`counts`/`offsets`/`indices`/levels) are reused across
  frames; `reset()` releases them.

Culling semantics worth knowing:

```cpp
nova::scene::CullStats stats;
std::vector<Entity> visible;
if (nova::scene::cullForCamera(world, cameraEntity, visible, stats)) {
    nova::scene::sortFrontToBack(world, cameraEye, visible);   // opaque draw list
}
```

* Entities **without** `Bounds` are kept and counted in `CullStats::unbounded` —
  an unknown extent is not a reason to drop something (lights, cameras and
  logical nodes live here).
* The camera entity is excluded from its own draw list.
* Sphere/AABB tests are conservative in the *keep* direction, so culling can
  retain a little extra geometry but never drops something visible.
* `sortFrontToBack` uses `std::sort` (no temporary buffer) and is therefore not
  stable; transparent lists need an explicit key and back-to-front ordering,
  which the frame graph will own.

Tests: `engine/tests/test_ecs.cpp` (20 cases — handle semantics, sparse-set
behaviour, non-trivial component moves, purge-on-destroy, view
intersection/mutation/empty-storage paths, 10 000-entity churn),
`engine/tests/test_transform.cpp` (16 cases — matrix composition, parenting and
detach, cycle rejection, translation/rotation/scale inheritance, orphans,
direct cycles, deep chain, wide hierarchy) and
`engine/tests/test_culling.cpp` (15 cases — projection conventions, view-matrix
inversion, frustum plane extraction and point/sphere/AABB tests, world AABB
derivation, camera exclusion and failure paths, orthographic volumes, parented
cameras, front-to-back ordering, 4 000-object scene).

## Consumed by the renderer

`renderer::buildDrawCommands(world, visible, out)` turns a culled, sorted entity
list into `DrawCommand`s: an entity is drawn when it owns both a
`renderer::MeshRef` (with a real mesh) and a `WorldTransform` (written by
`SceneHierarchy::update`). `renderer::runSceneStage()` runs the whole stage for
one frame — this is what `Engine::updateScene` calls after animating:

```cpp
renderer::SceneStageInput input{};
input.world = &m_world;
input.hierarchy = &m_hierarchy;
input.cameraEntity = m_cameraEntity;
input.timeSeconds = m_engineTime;
input.aspect = width / height;                 // from the swapchain

// writes camera.aspectRatio, evaluates the hierarchy, culls, sorts and fills
// both m_drawCommands and the snapshot the renderer consumes
renderer::runSceneStage(input, m_visible, m_drawCommands, snapshot, &output);
```

It fails closed — empty draw list, identity view-projection, `false` — when the
camera entity has no `Camera`/`WorldTransform`, so a caller can skip the frame
instead of rendering garbage.

Covered end to end by `engine/tests/test_drawlist.cpp` (15 cases), including a
check that the camera entity reproduces `Renderer::viewProjection(aspect)`
element by element, so the M1 image cannot drift.

## Still planned for M2

* glTF 2.0 assets feeding meshes/materials/textures into the draw list
  (`engine/assets`), replacing the single builtin mesh
* PBR pipeline with a depth attachment (`D32_SFloat`) and depth test
* Frame graph: passes declare reads/writes; transient attachments and barriers

## Rules for new code here

See [`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>` or a failure value;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

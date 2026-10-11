# `scene` — Nova Engine module

**Namespace:** `nova::scene`  ·  **Milestone:** M2 (in progress)  ·  **CMake flag:** n/a (always built)

Scene representation: entity/component storage, transform hierarchy, cameras,
culling. Replaces the hard-coded two-pass loop in `engine/renderer` with a
data-driven pass list.

## What is here now (M2, first increment)

| File | Contents |
| --- | --- |
| `Entity.h` | `Entity` handle = slot index + generation; `kNullEntity`; `std::hash` specialization |
| `ComponentStorage.h` | `SparseSet` (O(1) index ↔ dense position, swap-remove), `IComponentStorage` (type-erased purge path), `ComponentStorage<T>` (dense value array parallel to the index array), `componentTypeId<T>()` (RTTI-free per-type ids) |
| `World.h` / `World.cpp` | Entity registry (create/destroy with slot recycling and generation bumping), typed component API (`add`/`remove`/`get`/`has`/`count`), storage registry indexed by type id, `Stats` diagnostics |
| `View.h` | `View<Ts...>` intersection iteration over the smallest matching storage; iterators yield `std::tuple<Entity, Ts&...>` (structured bindings, in-place mutation); `each()`, `collectEntities()`, `size()` |
| `Transform.h` | `Transform` (local T·R·S, `localMatrix()`), `Parent` (null handle == root), `WorldTransform` (cached world matrix) — all trivially copyable |
| `SceneHierarchy.h` / `.cpp` | `setParent`/`detach` (rejects dead handles, self-parenting, cycles), `isAncestorOf`, `depthOf`, `childrenOf`, and `update()` which derives CSR child lists from `Parent` components and evaluates them level-order into `WorldTransform` |

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

Tests: `engine/tests/test_ecs.cpp` (20 cases — handle semantics, sparse-set
behaviour, non-trivial component moves, purge-on-destroy, view
intersection/mutation/empty-storage paths, 10 000-entity churn) and
`engine/tests/test_transform.cpp` (16 cases — matrix composition, parenting and
detach, cycle rejection, translation/rotation/scale inheritance, orphans,
direct cycles, deep chain, wide hierarchy).

## Still planned for M2

* Camera components (perspective/orthographic) producing view-projection matrices
* Frustum culling and draw-list generation
* Pass list consumed by `renderer::Renderer` (frame graph)

## Rules for new code here

See [`docs/CODING_STANDARDS.md`](../../docs/CODING_STANDARDS.md):

* depend only on layers below this one (`core` → `platform`/`rhi` → `renderer` → `Engine`);
* no exceptions — return `nova::Result<T, E>` or a failure value;
* Doxygen comments plus an explicit thread-safety statement on every public class;
* unit tests in `engine/tests/` registered with CTest.

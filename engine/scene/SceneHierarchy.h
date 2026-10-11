#pragma once

// Nova Engine — transform hierarchy (M2).
//
// Maintains parent/child links and recomputes world-space matrices for every
// entity that owns a Transform.
//
// Design notes:
//   * Child lists are *derived* from Parent components on every update instead
//     of being stored persistently. Destroyed entities have their components
//     purged by World, so there is no stale-hierarchy state to invalidate —
//     the structure cannot drift out of sync with the registry.
//   * Child lists are held in CSR form (counts -> offsets -> flat index array),
//     which is allocation-free in steady state and cache friendly.
//   * Traversal is level-order (breadth first) over entity indices, so a
//     parent's WorldTransform is always written before its children read it,
//     and no recursion depth is proportional to the hierarchy depth (mobile
//     render threads have small stacks).
//   * Cycles cannot hang the traversal: entities that form a cycle are simply
//     never reached from a root, and are reported in UpdateStats::cyclic.
//     SceneHierarchy::setParent rejects cycles up front, so reaching this path
//     means Parent components were written directly.
//
// Thread-safety: not thread-safe; drive from the same simulation thread that
// owns the World.

#include "core/Types.h"
#include "scene/Entity.h"
#include "scene/World.h"

#include <vector>

namespace nova::scene {

/// Builds and evaluates the transform hierarchy of a World.
class SceneHierarchy : public core::NonCopyable {
public:
    /// Diagnostics for the most recent update().
    struct UpdateStats {
        usize roots = 0;              ///< entities evaluated with an identity parent
        usize transformsWritten = 0;  ///< WorldTransform matrices produced
        usize orphans = 0;            ///< parent handle dead, or parent has no Transform
        usize cyclic = 0;             ///< entities unreachable from any root (cycle)
        usize deepestLevel = 0;       ///< 0 for roots only, 1 for one child level, ...
    };

    SceneHierarchy() = default;
    ~SceneHierarchy() = default;

    // -- structure ---------------------------------------------------------

    /// Link `child` under `parent`; a null `parent` detaches (makes it a root).
    ///
    /// Fails (logging an error) when either handle is dead, when the two are
    /// equal, or when the link would create a cycle. Adds a default (identity)
    /// Transform to `child` — and to `parent`, so the link is usable — when
    /// missing, and always writes the Parent component.
    bool setParent(World& world, Entity child, Entity parent);

    /// Detach `child` from its parent, making it a root.
    bool detach(World& world, Entity child) { return setParent(world, child, kNullEntity); }

    /// True when `candidate` is `entity` itself or one of its ancestors.
    /// Terminates safely on pre-existing cycles.
    [[nodiscard]] bool isAncestorOf(World& world, Entity entity, Entity candidate) const;

    /// Number of parent links above `entity` (roots are 0); 0 when dead.
    [[nodiscard]] usize depthOf(World& world, Entity entity) const;

    /// Children recorded by the last update(), appended to `out` in CSR order.
    /// Empty when the entity has no children or was never part of an update.
    void childrenOf(World& world, Entity entity, std::vector<Entity>& out) const;

    // -- evaluation --------------------------------------------------------

    /// Recompute WorldTransform for every entity owning a Transform.
    /// WorldTransform components are created on demand. Returns the statistics
    /// for this pass (also readable through lastStats()).
    UpdateStats update(World& world);

    [[nodiscard]] const UpdateStats& lastStats() const noexcept { return m_stats; }

    /// Drop all cached scratch memory (call when the world is torn down).
    void reset();

private:
    /// Rebuild CSR child lists + the root level from Parent components.
    void buildChildLists(World& world);

    /// Best-effort matrices for entities trapped in a parent cycle.
    void resolveCyclic(World& world);

    // CSR child lists, indexed by entity index.
    std::vector<u32> m_childCounts;
    std::vector<u32> m_childOffsets;
    std::vector<u32> m_childIndices;
    std::vector<u32> m_fillCursor;

    // Level-order traversal scratch (reused across frames).
    std::vector<u32> m_currentLevel;
    std::vector<u32> m_nextLevel;
    std::vector<core::u8> m_visited;

    UpdateStats m_stats{};
};

}  // namespace nova::scene

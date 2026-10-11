// Nova Engine — transform hierarchy implementation (M2).

#include "scene/SceneHierarchy.h"

#include "core/Assert.h"
#include "core/Log.h"
#include "core/Math.h"
#include "scene/Transform.h"
#include "scene/View.h"

#include <tuple>

namespace nova::scene {

using core::u8;
using core::u32;
using core::usize;

namespace {

/// True when `entity` has a Parent component pointing at a live entity that
/// itself owns a Transform; `outParent` receives that handle.
///
/// This single definition of "has a usable parent" is shared by the CSR build
/// and the traversal so roots and children can never disagree.
bool liveParent(const World& world, Entity entity, Entity& outParent) {
    const Parent* link = world.get<Parent>(entity);
    if (link == nullptr || link->entity.isNull()) {
        return false;
    }
    if (!world.isAlive(link->entity) || !world.has<Transform>(link->entity)) {
        return false;
    }
    outParent = link->entity;
    return true;
}

/// True when a Parent link exists but could not be honoured (dead parent, or a
/// parent without a Transform). Reported as an orphan.
bool brokenParentLink(const World& world, Entity entity) {
    const Parent* link = world.get<Parent>(entity);
    return link != nullptr && !link->entity.isNull();
}

}  // namespace

// ----------------------------------------------------------------- structure

bool SceneHierarchy::setParent(World& world, Entity child, Entity parent) {
    if (!world.isAlive(child)) {
        NOVA_LOG_ERROR("scene: setParent on a dead child entity (%u:%u)", child.index,
                       child.generation);
        return false;
    }
    if (!parent.isNull() && !world.isAlive(parent)) {
        NOVA_LOG_ERROR("scene: setParent(%u) to a dead parent (%u:%u)", child.index, parent.index,
                       parent.generation);
        return false;
    }
    if (child == parent) {
        NOVA_LOG_ERROR("scene: setParent(%u) rejected — an entity cannot parent itself",
                       child.index);
        return false;
    }
    // Linking child under parent creates a cycle when child is already an
    // ancestor of parent.
    if (!parent.isNull() && isAncestorOf(world, parent, child)) {
        NOVA_LOG_ERROR("scene: setParent(%u -> %u) rejected — would create a cycle", child.index,
                       parent.index);
        return false;
    }

    if (world.get<Transform>(child) == nullptr) {
        world.add<Transform>(child);
    }
    // A parent without a Transform would make the link unusable (the child
    // would be demoted to an orphan root on the next update), so give the
    // parent an identity transform instead of silently dropping the link.
    if (!parent.isNull() && world.get<Transform>(parent) == nullptr) {
        world.add<Transform>(parent);
    }
    world.add<Parent>(child, Parent{parent});
    return true;
}

bool SceneHierarchy::isAncestorOf(World& world, Entity entity, Entity candidate) const {
    Entity current = entity;
    // Guard against pre-existing cycles: no chain can be longer than the number
    // of entity slots before it must repeat.
    const usize limit = world.entitySlots() + 1;
    for (usize step = 0; step < limit; ++step) {
        if (current.isNull() || !world.isAlive(current)) {
            return false;
        }
        if (current == candidate) {
            return true;
        }
        const Parent* link = world.get<Parent>(current);
        if (link == nullptr) {
            return false;
        }
        current = link->entity;
    }
    NOVA_LOG_ERROR("scene: parent chain from entity %u exceeds %zu links (cycle)", entity.index,
                   limit);
    return false;
}

usize SceneHierarchy::depthOf(World& world, Entity entity) const {
    if (!world.isAlive(entity)) {
        return 0;
    }
    usize depth = 0;
    Entity current = entity;
    const usize limit = world.entitySlots() + 1;
    while (depth < limit) {
        const Parent* link = world.get<Parent>(current);
        if (link == nullptr || link->entity.isNull() || !world.isAlive(link->entity)) {
            return depth;
        }
        current = link->entity;
        ++depth;
    }
    return depth;
}

void SceneHierarchy::childrenOf(World& world, Entity entity, std::vector<Entity>& out) const {
    if (m_childOffsets.empty() || !world.isAlive(entity)) {
        return;
    }
    if (entity.index + 1 >= static_cast<u32>(m_childOffsets.size())) {
        return;
    }
    const u32 begin = m_childOffsets[entity.index];
    const u32 end = m_childOffsets[entity.index + 1];
    out.reserve(out.size() + (end - begin));
    for (u32 position = begin; position < end; ++position) {
        const Entity child = world.entityAt(m_childIndices[position]);
        if (!child.isNull()) {
            out.push_back(child);
        }
    }
}

// ---------------------------------------------------------------- evaluation

SceneHierarchy::UpdateStats SceneHierarchy::update(World& world) {
    m_stats = UpdateStats{};

    const usize slots = world.entitySlots();
    if (slots == 0) {
        return m_stats;
    }

    // 1. Every Transform owner gets a WorldTransform slot to write into.
    //    Adding a *different* component type cannot invalidate the Transform
    //    view being iterated (separate storage).
    for (auto entry : world.view<Transform>()) {
        const Entity entity = std::get<Entity>(entry);
        if (world.get<WorldTransform>(entity) == nullptr) {
            world.add<WorldTransform>(entity);
        }
    }

    // 2. Derive CSR child lists and the root level.
    buildChildLists(world);

    // 3. Level-order traversal: a parent is always written before its children
    //    read it, and stack depth is independent of hierarchy depth.
    m_visited.assign(slots, 0);
    usize level = 0;
    while (!m_currentLevel.empty()) {
        m_nextLevel.clear();
        for (u32 index : m_currentLevel) {
            const Entity entity = world.entityAt(index);
            if (entity.isNull()) {
                continue;
            }
            Transform* local = world.get<Transform>(entity);
            WorldTransform* cached = world.get<WorldTransform>(entity);
            if (local == nullptr || cached == nullptr) {
                continue;
            }

            math::Mat4 parentWorld = math::identity();
            Entity parentHandle = kNullEntity;
            if (liveParent(world, entity, parentHandle)) {
                const WorldTransform* parentWorldTransform = world.get<WorldTransform>(parentHandle);
                if (parentWorldTransform != nullptr) {
                    parentWorld = parentWorldTransform->matrix;
                }
            }

            cached->matrix = parentWorld * local->localMatrix();
            m_visited[index] = 1;
            ++m_stats.transformsWritten;

            const u32 begin = m_childOffsets[index];
            const u32 end = m_childOffsets[index + 1];
            for (u32 position = begin; position < end; ++position) {
                m_nextLevel.push_back(m_childIndices[position]);
            }
        }
        if (!m_nextLevel.empty()) {
            m_stats.deepestLevel = level + 1;
        }
        ++level;
        m_currentLevel.swap(m_nextLevel);
    }

    // 4. Anything owning a Transform that was never reached is part of a cycle.
    resolveCyclic(world);
    return m_stats;
}

void SceneHierarchy::buildChildLists(World& world) {
    const usize slots = world.entitySlots();

    // Counts pass.
    if (m_childCounts.size() < slots) {
        m_childCounts.resize(slots, 0);
    }
    for (u32& count : m_childCounts) {
        count = 0;
    }
    m_currentLevel.clear();

    for (auto entry : world.view<Transform>()) {
        const Entity entity = std::get<Entity>(entry);
        Entity parentHandle = kNullEntity;
        if (liveParent(world, entity, parentHandle)) {
            ++m_childCounts[parentHandle.index];
        } else {
            if (brokenParentLink(world, entity)) {
                ++m_stats.orphans;
            }
            m_currentLevel.push_back(entity.index);
        }
    }
    m_stats.roots = m_currentLevel.size();

    // Prefix sums -> offsets (size slots + 1).
    m_childOffsets.resize(slots + 1);
    m_childOffsets[0] = 0;
    for (usize index = 0; index < slots; ++index) {
        m_childOffsets[index + 1] = m_childOffsets[index] + m_childCounts[index];
    }

    // Fill pass.
    const usize totalChildren = m_childOffsets[slots];
    m_childIndices.resize(totalChildren);
    if (m_fillCursor.size() < slots) {
        m_fillCursor.resize(slots, 0);
    }
    for (usize index = 0; index < slots; ++index) {
        m_fillCursor[index] = m_childOffsets[index];
    }
    for (auto entry : world.view<Transform>()) {
        const Entity entity = std::get<Entity>(entry);
        Entity parentHandle = kNullEntity;
        if (liveParent(world, entity, parentHandle)) {
            m_childIndices[m_fillCursor[parentHandle.index]++] = entity.index;
        }
    }
}

void SceneHierarchy::resolveCyclic(World& world) {
    usize cyclic = 0;
    for (auto entry : world.view<Transform>()) {
        const Entity entity = std::get<Entity>(entry);
        if (entity.index < static_cast<u32>(m_visited.size()) && m_visited[entity.index] != 0) {
            continue;
        }
        ++cyclic;
        // Best effort: fall back to the local matrix so a cyclic entity renders
        // somewhere sane instead of keeping a stale world matrix.
        const Transform& local = std::get<1>(entry);
        WorldTransform* cached = world.get<WorldTransform>(entity);
        if (cached != nullptr) {
            cached->matrix = local.localMatrix();
        }
    }
    m_stats.cyclic = cyclic;
    if (cyclic > 0) {
        NOVA_LOG_ERROR("scene: %zu entities form a parent cycle; wrote local matrices instead",
                       cyclic);
    }
}

void SceneHierarchy::reset() {
    m_childCounts.clear();
    m_childCounts.shrink_to_fit();
    m_childOffsets.clear();
    m_childOffsets.shrink_to_fit();
    m_childIndices.clear();
    m_childIndices.shrink_to_fit();
    m_fillCursor.clear();
    m_fillCursor.shrink_to_fit();
    m_currentLevel.clear();
    m_currentLevel.shrink_to_fit();
    m_nextLevel.clear();
    m_nextLevel.shrink_to_fit();
    m_visited.clear();
    m_visited.shrink_to_fit();
    m_stats = UpdateStats{};
}

}  // namespace nova::scene

#pragma once

// Nova Engine — entity/component registry (M2 ECS).
//
// World owns every entity and every component storage. Entity lifecycle is
// index + generation based (scene/Entity.h); components live in sparse-set
// storages (scene/ComponentStorage.h) indexed by a per-type id, so lookup is a
// vector index rather than a hash.
//
// Iteration goes through scene/View.h: `#include "scene/View.h"` and call
// `world.view<Transform, Mesh>()`. The member function is declared here and
// defined in View.h to keep the header dependency one-directional.
//
// Failure handling: no exceptions. Mutating a dead/null entity logs an error
// and returns a failure value (nullptr / false); queries simply report
// absence. Internal invariants use NOVA_ASSERT.
//
// Thread-safety: not thread-safe. One World is owned and driven by a single
// thread (the simulation/render thread). Reads from another thread require
// external synchronization; there is no internal locking by design so the
// per-frame hot path stays allocation- and lock-free.

#include "core/Assert.h"
#include "core/Log.h"
#include "core/Types.h"
#include "scene/ComponentStorage.h"
#include "scene/Entity.h"

#include <utility>
#include <vector>

namespace nova::scene {

/// Forward declaration; the definition and World::view() live in scene/View.h.
template <typename... Ts>
class View;

/// Owning registry of entities and their components.
class World : public core::NonCopyable {
public:
    /// Diagnostics snapshot, surfaced through the `stats` console command.
    struct Stats {
        usize entitySlots = 0;     ///< allocated slots (alive + recycled)
        usize aliveEntities = 0;   ///< entities currently alive
        usize componentTypes = 0;  ///< distinct component types in use
        usize components = 0;      ///< total live components
        usize memoryBytes = 0;     ///< approximate dense-array capacity in bytes
    };

    World() = default;
    ~World();

    // -- entities ----------------------------------------------------------

    /// Create an entity. Indices are recycled from destroyed entities with a
    /// bumped generation, so handles stay unique over the process lifetime
    /// (2^32 reuses of one slot would be required to collide).
    [[nodiscard]] Entity create();

    /// Create `count` entities at once, appending to `out`.
    void create(std::vector<Entity>& out, usize count);

    /// Destroy an entity and purge its components from every storage.
    /// Returns false (and logs) for null/stale handles.
    bool destroy(Entity entity);

    /// True when the handle refers to a currently alive entity of this world.
    [[nodiscard]] bool isAlive(Entity entity) const noexcept;

    /// Rebuild the live handle for an entity index; kNullEntity when the index
    /// is out of range or not alive. Views use this to hand out real handles.
    [[nodiscard]] Entity entityAt(u32 index) const noexcept;

    [[nodiscard]] usize aliveCount() const noexcept { return m_aliveCount; }

    /// Entity slots allocated so far (alive + recycled).
    [[nodiscard]] usize entitySlots() const noexcept { return m_records.size(); }

    // -- components --------------------------------------------------------

    /// Add a component, constructing it from `args`. Replaces the component if
    /// the entity already has one. Returns nullptr for a dead/null entity.
    ///
    /// The returned reference is invalidated by any later erase of that
    /// component type (sparse-set swap-remove), so do not cache it across
    /// structural changes.
    template <typename T, typename... Args>
    T* add(Entity entity, Args&&... args);

    /// Remove a component. Returns false when the entity is dead or had none.
    template <typename T>
    bool remove(Entity entity);

    /// Component pointer, or nullptr when absent or the entity is dead.
    template <typename T>
    [[nodiscard]] T* get(Entity entity);

    template <typename T>
    [[nodiscard]] const T* get(Entity entity) const;

    /// Presence test. Cheap enough for per-frame probing.
    template <typename T>
    [[nodiscard]] bool has(Entity entity) const;

    /// Number of entities owning `T`.
    template <typename T>
    [[nodiscard]] usize count() const;

    /// Total bytes held by every component storage (approximate).
    [[nodiscard]] usize componentMemoryBytes() const;

    // -- views -------------------------------------------------------------

    /// Iterate the entities that own *all* of `Ts`.
    /// Defined in scene/View.h — include it to use views.
    template <typename... Ts>
    [[nodiscard]] View<Ts...> view();

    // -- storage access (used by View and diagnostics) ---------------------

    /// Storage for `T`, or nullptr when no entity ever had one. Creating a
    /// storage is deliberate (add<T>), so an unused type costs nothing.
    template <typename T>
    [[nodiscard]] ComponentStorage<T>* storage();

    template <typename T>
    [[nodiscard]] const ComponentStorage<T>* storage() const;

    /// Presence by raw entity index; skips handle validation. Views use this
    /// because every index inside a storage is alive by construction.
    template <typename T>
    [[nodiscard]] bool hasIndex(u32 index) const;

    /// Aggregate diagnostics.
    [[nodiscard]] Stats stats() const;

private:
    struct EntityRecord {
        u32 generation = 0;
        bool alive = false;
    };

    /// Storage for `T`, created on first use.
    template <typename T>
    ComponentStorage<T>* storageOrCreate();

    /// Shared failure path for mutations on invalid handles.
    bool rejectInvalidEntity(const char* operation, Entity entity) const;

    std::vector<EntityRecord> m_records;
    std::vector<u32> m_freeIndices;

    /// Indexed by componentTypeId<T>(); unused slots stay null.
    std::vector<nova::Unique<IComponentStorage>> m_storages;

    usize m_aliveCount = 0;
};

// ---------------------------------------------------------------------------
// Template definitions
// ---------------------------------------------------------------------------

template <typename T, typename... Args>
T* World::add(Entity entity, Args&&... args) {
    if (!isAlive(entity)) {
        rejectInvalidEntity("add", entity);
        return nullptr;
    }
    ComponentStorage<T>* target = storageOrCreate<T>();
    return &target->emplace(entity.index, std::forward<Args>(args)...);
}

template <typename T>
bool World::remove(Entity entity) {
    if (!isAlive(entity)) {
        rejectInvalidEntity("remove", entity);
        return false;
    }
    ComponentStorage<T>* target = storage<T>();
    if (target == nullptr || !target->containsEntity(entity.index)) {
        return false;
    }
    target->eraseEntity(entity.index);
    return true;
}

template <typename T>
T* World::get(Entity entity) {
    if (!isAlive(entity)) {
        return nullptr;
    }
    ComponentStorage<T>* target = storage<T>();
    return target != nullptr ? target->find(entity.index) : nullptr;
}

template <typename T>
const T* World::get(Entity entity) const {
    if (!isAlive(entity)) {
        return nullptr;
    }
    const ComponentStorage<T>* target = storage<T>();
    return target != nullptr ? target->find(entity.index) : nullptr;
}

template <typename T>
bool World::has(Entity entity) const {
    return isAlive(entity) && hasIndex<T>(entity.index);
}

template <typename T>
usize World::count() const {
    const ComponentStorage<T>* target = storage<T>();
    return target != nullptr ? target->size() : 0;
}

template <typename T>
ComponentStorage<T>* World::storage() {
    const u32 typeId = componentTypeId<T>();
    if (typeId >= static_cast<u32>(m_storages.size())) {
        return nullptr;
    }
    // The id -> type mapping is 1:1 by construction (componentTypeId), so the
    // downcast is exact without RTTI.
    return static_cast<ComponentStorage<T>*>(m_storages[typeId].get());
}

template <typename T>
const ComponentStorage<T>* World::storage() const {
    const u32 typeId = componentTypeId<T>();
    if (typeId >= static_cast<u32>(m_storages.size())) {
        return nullptr;
    }
    return static_cast<const ComponentStorage<T>*>(m_storages[typeId].get());
}

template <typename T>
bool World::hasIndex(u32 index) const {
    const ComponentStorage<T>* target = storage<T>();
    return target != nullptr && target->containsEntity(index);
}

template <typename T>
ComponentStorage<T>* World::storageOrCreate() {
    const u32 typeId = componentTypeId<T>();
    if (typeId >= static_cast<u32>(m_storages.size())) {
        m_storages.resize(static_cast<usize>(typeId) + 1);
    }
    nova::Unique<IComponentStorage>& slot = m_storages[typeId];
    if (slot == nullptr) {
        slot = nova::makeUnique<ComponentStorage<T>>();
    }
    return static_cast<ComponentStorage<T>*>(slot.get());
}

}  // namespace nova::scene

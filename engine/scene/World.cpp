// Nova Engine — entity/component registry implementation (M2 ECS).
//
// Non-template members only; the typed paths live in scene/World.h.

#include "scene/World.h"

#include "core/Log.h"

namespace nova::scene {

World::~World() = default;

// ------------------------------------------------------------------ entities

Entity World::create() {
    u32 index = 0;
    if (!m_freeIndices.empty()) {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    } else {
        index = static_cast<u32>(m_records.size());
        m_records.push_back(EntityRecord{});
    }

    EntityRecord& record = m_records[index];
    NOVA_ASSERT(!record.alive, "World::create: slot %u reused while alive", index);
    record.alive = true;
    ++m_aliveCount;
    return Entity{index, record.generation};
}

void World::create(std::vector<Entity>& out, usize count) {
    out.reserve(out.size() + count);
    for (usize i = 0; i < count; ++i) {
        out.push_back(create());
    }
}

bool World::destroy(Entity entity) {
    if (!isAlive(entity)) {
        return rejectInvalidEntity("destroy", entity);
    }

    // Purge from every storage first so no component can outlive its entity.
    for (nova::Unique<IComponentStorage>& entry : m_storages) {
        if (entry != nullptr) {
            entry->eraseEntity(entity.index);
        }
    }

    EntityRecord& record = m_records[entity.index];
    record.alive = false;
    ++record.generation;  // invalidates every outstanding handle to this slot
    m_freeIndices.push_back(entity.index);
    --m_aliveCount;
    return true;
}

bool World::isAlive(Entity entity) const noexcept {
    if (entity.isNull() || entity.index >= static_cast<u32>(m_records.size())) {
        return false;
    }
    const EntityRecord& record = m_records[entity.index];
    return record.alive && record.generation == entity.generation;
}

Entity World::entityAt(u32 index) const noexcept {
    if (index >= static_cast<u32>(m_records.size())) {
        return kNullEntity;
    }
    const EntityRecord& record = m_records[index];
    if (!record.alive) {
        return kNullEntity;
    }
    return Entity{index, record.generation};
}

bool World::rejectInvalidEntity(const char* operation, Entity entity) const {
    if (entity.isNull()) {
        NOVA_LOG_ERROR("scene: World::%s on a null entity handle", operation);
    } else {
        NOVA_LOG_ERROR("scene: World::%s on dead entity %u:%u (stale handle)", operation,
                       entity.index, entity.generation);
    }
    return false;
}

// -------------------------------------------------------------- diagnostics

usize World::componentMemoryBytes() const {
    usize total = 0;
    for (const nova::Unique<IComponentStorage>& entry : m_storages) {
        if (entry != nullptr) {
            total += entry->memoryBytes();
        }
    }
    return total;
}

World::Stats World::stats() const {
    Stats out{};
    out.entitySlots = m_records.size();
    out.aliveEntities = m_aliveCount;
    for (const nova::Unique<IComponentStorage>& entry : m_storages) {
        if (entry == nullptr) {
            continue;
        }
        ++out.componentTypes;
        out.components += entry->size();
        out.memoryBytes += entry->memoryBytes();
    }
    return out;
}

}  // namespace nova::scene

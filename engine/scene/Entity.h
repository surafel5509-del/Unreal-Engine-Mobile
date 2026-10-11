#pragma once

// Nova Engine — entity handles (M2 ECS).
//
// An Entity is a lightweight value type: a slot index into the World's entity
// table plus a generation counter. Destroying an entity bumps the generation
// and recycles the index, so a stale handle can never alias a live entity —
// the classic use-after-destroy bug becomes a failed isAlive() instead.
//
// Thread-safety: pure value type, no internal state; safe to pass between
// threads as long as the owning World is not mutated concurrently.

#include "core/Types.h"

#include <cstddef>

namespace nova::scene {

using core::u32;
using core::usize;

/// Opaque handle to an entity inside a nova::scene::World.
///
/// Handles are only meaningful together with the World that issued them.
/// Comparing handles from different worlds is undefined in spirit (they may
/// compare equal while referring to unrelated entities).
struct Entity {
    /// Sentinel index used by default-constructed (null) handles.
    static constexpr u32 kInvalidIndex = 0xFFFFFFFFu;

    /// Slot in the world's entity table.
    u32 index = kInvalidIndex;

    /// Bumped every time `index` is recycled; stale handles fail isAlive().
    u32 generation = 0;

    /// True for handles that never referred to an entity.
    [[nodiscard]] constexpr bool isNull() const noexcept { return index == kInvalidIndex; }

    friend constexpr bool operator==(const Entity&, const Entity&) noexcept = default;
};

/// The null handle: equal to itself, never alive in any World.
inline constexpr Entity kNullEntity{};

}  // namespace nova::scene

/// std::hash specialization so Entity can key unordered containers
/// (per-entity side tables, editor selection sets, ...).
namespace std {

template <>
struct hash<nova::scene::Entity> {
    std::size_t operator()(const nova::scene::Entity& entity) const noexcept {
        const std::size_t index = static_cast<std::size_t>(entity.index);
        const std::size_t generation = static_cast<std::size_t>(entity.generation);
        // Boost-style combine; portable across 32- and 64-bit size_t.
        return index ^ (generation + 0x9E3779B9u + (index << 6) + (index >> 2));
    }
};

}  // namespace std

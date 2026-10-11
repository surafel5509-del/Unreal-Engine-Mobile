#pragma once

// Nova Engine — ECS component storage (M2).
//
// Sparse-set storage: a dense array of components kept parallel to a dense
// array of entity indices, plus a sparse index -> dense-position map.
//
//   insert / find / erase : O(1)
//   iteration             : contiguous memory, no per-element virtual dispatch
//
// Erase is a swap-remove, so component order is *not* insertion order and
// dense positions are unstable across erases. Anything that needs stable
// ordering must sort the view result (M2 culling does this by distance).
//
// Header-only: every entry point is a template over the component type.
//
// Thread-safety: not thread-safe. A World owns its storages and is driven from
// a single simulation thread.

#include "core/Assert.h"
#include "core/Types.h"
#include "scene/Entity.h"

#include <atomic>
#include <type_traits>
#include <utility>
#include <vector>

namespace nova::scene {

/// Type-erased storage interface.
///
/// Lets World purge every component of a destroyed entity without knowing the
/// component types at that point. Only the erasure/query paths are virtual;
/// typed access goes through ComponentStorage<T> directly.
class IComponentStorage {
public:
    virtual ~IComponentStorage() = default;

    IComponentStorage() = default;
    IComponentStorage(const IComponentStorage&) = delete;
    IComponentStorage& operator=(const IComponentStorage&) = delete;

    /// Remove the component owned by entity index `index`, if any.
    virtual void eraseEntity(u32 index) = 0;

    /// True when a component exists for entity index `index`.
    [[nodiscard]] virtual bool containsEntity(u32 index) const = 0;

    /// Number of live components.
    [[nodiscard]] virtual usize size() const = 0;

    /// Drop every component (entities themselves are untouched).
    virtual void clear() = 0;

    /// `sizeof(T)`; used for memory diagnostics.
    [[nodiscard]] virtual usize componentSize() const = 0;

    /// Capacity in bytes currently held by the dense arrays (approximate).
    [[nodiscard]] virtual usize memoryBytes() const = 0;
};

/// O(1) index <-> dense-position map with swap-remove semantics.
class SparseSet {
public:
    /// Sentinel stored in the sparse array meaning "no dense entry".
    static constexpr u32 kInvalid = 0xFFFFFFFFu;

    /// Initial sparse capacity; grows geometrically on demand.
    static constexpr usize kInitialCapacity = 64;

    [[nodiscard]] bool contains(u32 index) const noexcept {
        return index < static_cast<u32>(m_sparse.size()) && m_sparse[index] != kInvalid;
    }

    /// Insert `index` and return its dense position. Requires !contains(index).
    u32 insert(u32 index) {
        NOVA_ASSERT(!contains(index), "SparseSet: index %u already present", index);
        grow(static_cast<usize>(index) + 1);
        const u32 densePosition = static_cast<u32>(m_dense.size());
        m_dense.push_back(index);
        m_sparse[index] = densePosition;
        return densePosition;
    }

    /// Remove `index`, moving the last dense entry into the hole it leaves.
    void erase(u32 index) {
        if (!contains(index)) {
            return;
        }
        const u32 densePosition = m_sparse[index];
        const u32 lastIndex = m_dense.back();
        m_dense[densePosition] = lastIndex;
        m_sparse[lastIndex] = densePosition;
        m_dense.pop_back();
        m_sparse[index] = kInvalid;
    }

    /// Dense position of `index`; kInvalid when absent.
    [[nodiscard]] u32 densePosition(u32 index) const noexcept {
        return contains(index) ? m_sparse[index] : kInvalid;
    }

    /// Entity index stored at dense position `position`.
    [[nodiscard]] u32 denseIndexAt(usize position) const {
        NOVA_ASSERT(position < m_dense.size(), "SparseSet: dense position %zu out of range",
                    position);
        return m_dense[position];
    }

    /// All live entity indices, in dense (iteration) order.
    [[nodiscard]] const std::vector<u32>& dense() const noexcept { return m_dense; }

    [[nodiscard]] usize size() const noexcept { return m_dense.size(); }

    [[nodiscard]] usize memoryBytes() const noexcept {
        return m_dense.capacity() * sizeof(u32) + m_sparse.capacity() * sizeof(u32);
    }

    /// Remove every entry.
    void clear() noexcept {
        for (u32 index : m_dense) {
            m_sparse[index] = kInvalid;
        }
        m_dense.clear();
    }

    /// Reserve space for `count` entities without changing size.
    void reserve(usize count) {
        m_dense.reserve(count);
        grow(count);
    }

private:
    void grow(usize required) {
        if (required <= m_sparse.size()) {
            return;
        }
        usize capacity = m_sparse.empty() ? kInitialCapacity : m_sparse.size();
        while (capacity < required) {
            capacity *= 2;
        }
        m_sparse.resize(capacity, kInvalid);
    }

    std::vector<u32> m_dense;
    std::vector<u32> m_sparse;
};

namespace detail {

/// One program-wide counter handing out a distinct id per component type.
inline std::atomic<u32>& componentTypeCounter() noexcept {
    static std::atomic<u32> counter{0};
    return counter;
}

}  // namespace detail

/// Stable, per-type component id — identical in every translation unit.
///
/// Ids are dense and assigned on first use, so World can index a vector of
/// storages by id with no hashing and no RTTI.
template <typename T>
[[nodiscard]] u32 componentTypeId() noexcept {
    static const u32 id = detail::componentTypeCounter().fetch_add(1, std::memory_order_relaxed);
    return id;
}

/// Typed component storage: sparse set + parallel dense value array.
///
/// `T` must be move constructible and move assignable (swap-remove moves the
/// last element into a hole, and re-adding replaces in place). Trivially
/// copyable POD components — the common case — satisfy both.
template <typename T>
class ComponentStorage final : public IComponentStorage {
    static_assert(std::is_move_constructible_v<T>,
                  "Nova ECS components must be move constructible");
    static_assert(std::is_move_assignable_v<T>,
                  "Nova ECS components must be move assignable");

public:
    ComponentStorage() = default;

    /// Construct with space for `reserveCount` components.
    explicit ComponentStorage(usize reserveCount) {
        m_set.reserve(reserveCount);
        m_values.reserve(reserveCount);
    }

    /// Construct (or replace) the component for entity index `index`.
    /// Returns a reference that stays valid until the next erase of *that*
    /// component; references to other components may be invalidated by any
    /// erase because of swap-remove.
    template <typename... Args>
    T& emplace(u32 index, Args&&... args) {
        if (m_set.contains(index)) {
            T& existing = m_values[m_set.densePosition(index)];
            existing = T(std::forward<Args>(args)...);
            return existing;
        }
        const u32 densePosition = m_set.insert(index);
        m_values.emplace_back(std::forward<Args>(args)...);
        NOVA_ASSERT(m_values.size() == m_set.size(),
                    "ComponentStorage: dense arrays diverged (%zu vs %zu)", m_values.size(),
                    m_set.size());
        return m_values[densePosition];
    }

    /// Component for entity index `index`, or nullptr when absent.
    [[nodiscard]] T* find(u32 index) noexcept {
        const u32 densePosition = m_set.densePosition(index);
        return densePosition == SparseSet::kInvalid ? nullptr : &m_values[densePosition];
    }

    [[nodiscard]] const T* find(u32 index) const noexcept {
        const u32 densePosition = m_set.densePosition(index);
        return densePosition == SparseSet::kInvalid ? nullptr : &m_values[densePosition];
    }

    /// Component at a dense position (iteration); no bounds check in release.
    [[nodiscard]] T& at(usize densePosition) {
        NOVA_ASSERT(densePosition < m_values.size(), "ComponentStorage: position %zu out of range",
                    densePosition);
        return m_values[densePosition];
    }

    [[nodiscard]] const T& at(usize densePosition) const {
        NOVA_ASSERT(densePosition < m_values.size(), "ComponentStorage: position %zu out of range",
                    densePosition);
        return m_values[densePosition];
    }

    // -- IComponentStorage -------------------------------------------------

    void eraseEntity(u32 index) override {
        if (!m_set.contains(index)) {
            return;
        }
        const u32 densePosition = m_set.densePosition(index);
        // Move the tail value into the hole first, then swap-remove the index,
        // so the two dense arrays stay parallel. Skip the move when the hole is
        // already the tail (self-move-assignment is not guaranteed for all T).
        if (densePosition + 1 != static_cast<u32>(m_values.size())) {
            m_values[densePosition] = std::move(m_values.back());
        }
        m_values.pop_back();
        m_set.erase(index);
    }

    [[nodiscard]] bool containsEntity(u32 index) const override { return m_set.contains(index); }

    [[nodiscard]] usize size() const override { return m_values.size(); }

    [[nodiscard]] usize componentSize() const override { return sizeof(T); }

    [[nodiscard]] usize memoryBytes() const override {
        return m_set.memoryBytes() + m_values.capacity() * sizeof(T);
    }

    void clear() override {
        m_values.clear();
        m_set.clear();
    }

    // -- iteration helpers -------------------------------------------------

    /// Entity indices in dense order (parallel to values()).
    [[nodiscard]] const std::vector<u32>& denseIndices() const noexcept { return m_set.dense(); }

    /// Component values in dense order (parallel to denseIndices()).
    [[nodiscard]] const std::vector<T>& values() const noexcept { return m_values; }
    [[nodiscard]] std::vector<T>& values() noexcept { return m_values; }

private:
    SparseSet m_set;
    std::vector<T> m_values;
};

}  // namespace nova::scene

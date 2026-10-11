#pragma once

// Nova Engine — ECS iteration (M2).
//
// A View walks the entities that own *all* requested component types. It
// iterates the dense array of the smallest matching storage and filters
// candidates against the others, so cost is O(smallest) rather than O(all
// entities). Dereferencing yields std::tuple<Entity, Ts&...>, which supports
// structured bindings and in-place mutation.
//
//   for (auto [entity, transform, mesh] : world.view<Transform, Mesh>()) {
//       transform.position.y += 1.0f;
//   }
//
// Structural changes (World::create/destroy/add/remove) while iterating a view
// invalidate it: sparse-set erase is a swap-remove, so the candidate array can
// be reordered underneath the iterator. Collect entities first when a system
// needs to mutate structure.
//
// A View is a cheap, copyable handle; it does not own anything and must not
// outlive the World it was created from.
//
// Thread-safety: same as World — single simulation thread, no internal locking.

#include "core/Types.h"
#include "scene/ComponentStorage.h"
#include "scene/Entity.h"
#include "scene/World.h"

#include <cstddef>
#include <functional>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

namespace nova::scene {

/// Lazy intersection over entities owning every type in `Ts`.
template <typename... Ts>
class View {
    static_assert(sizeof...(Ts) > 0, "nova::scene::View requires at least one component type");

public:
    /// Forward iterator yielding std::tuple<Entity, Ts&...>.
    class iterator {
    public:
        using value_type = std::tuple<Entity, Ts&...>;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = value_type;
        using iterator_category = std::forward_iterator_tag;

        iterator() = default;
        iterator(const View* view, usize position) : m_view(view), m_position(position) {
            skipNonMatching();
        }

        /// Current entity handle (alive by construction).
        [[nodiscard]] Entity entity() const { return currentEntity(); }

        value_type operator*() const {
            const Entity handle = currentEntity();
            return value_type{handle, *m_view->m_world->get<Ts>(handle)...};
        }

        /// Component tuple without the handle, for systems that ignore it.
        [[nodiscard]] std::tuple<Ts&...> components() const {
            const Entity handle = currentEntity();
            return std::tuple<Ts&...>{*m_view->m_world->get<Ts>(handle)...};
        }

        iterator& operator++() {
            ++m_position;
            skipNonMatching();
            return *this;
        }

        iterator operator++(int) {
            iterator previous = *this;
            ++(*this);
            return previous;
        }

        friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept {
            return lhs.m_position == rhs.m_position;
        }
        friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept {
            return lhs.m_position != rhs.m_position;
        }

    private:
        [[nodiscard]] Entity currentEntity() const {
            return m_view->m_world->entityAt((*m_view->m_candidates)[m_position]);
        }

        void skipNonMatching() {
            while (m_position < m_view->m_candidates->size() &&
                   !m_view->matches((*m_view->m_candidates)[m_position])) {
                ++m_position;
            }
        }

        const View* m_view = nullptr;
        usize m_position = 0;
    };

    /// Build a view over `world`. Picks the smallest storage as the candidate
    /// list; storages that were never created make the view empty.
    explicit View(World& world) : m_world(&world) {
        // m_candidates defaults to the shared empty list, so a requested type
        // that was never used leaves the view empty instead of dangling.
        usize smallest = kNoStorage;
        ((pickCandidate<Ts>(smallest)), ...);
    }

    [[nodiscard]] iterator begin() const { return iterator(this, 0); }
    [[nodiscard]] iterator end() const { return iterator(this, m_candidates->size()); }

    /// Number of matching entities. O(smallest storage) — cheap enough for
    /// diagnostics, but iterate instead of counting in hot paths.
    [[nodiscard]] usize size() const {
        usize total = 0;
        for (usize position = 0; position < m_candidates->size(); ++position) {
            if (matches((*m_candidates)[position])) {
                ++total;
            }
        }
        return total;
    }

    [[nodiscard]] bool empty() const { return size() == 0; }

    /// Call `func(entity, components...)` for every match; returns the count.
    template <typename Func>
    usize each(Func&& func) const {
        usize total = 0;
        for (iterator it = begin(); it != end(); ++it) {
            std::apply(func, *it);
            ++total;
        }
        return total;
    }

    /// Collect the matching handles (structural changes invalidate the view,
    /// so systems that destroy entities gather first, then act).
    void collectEntities(std::vector<Entity>& out) const {
        for (iterator it = begin(); it != end(); ++it) {
            out.push_back(it.entity());
        }
    }

private:
    /// Sentinel meaning "no storage exists for any requested type".
    static constexpr usize kNoStorage = static_cast<usize>(-1);

    /// Shared empty candidate list for views over unused component types.
    static const std::vector<u32>& emptyIndices() {
        static const std::vector<u32> empty;
        return empty;
    }

    template <typename T>
    void pickCandidate(usize& smallest) {
        const ComponentStorage<T>* candidate = m_world->storage<T>();
        if (candidate != nullptr && candidate->size() < smallest) {
            smallest = candidate->size();
            m_candidates = &candidate->denseIndices();
        }
    }

    /// True when the entity index owns every requested component.
    [[nodiscard]] bool matches(u32 index) const { return (m_world->hasIndex<Ts>(index) && ...); }

    World* m_world = nullptr;
    const std::vector<u32>* m_candidates = &emptyIndices();
};

/// Definition of World::view() — declared in scene/World.h.
template <typename... Ts>
View<Ts...> World::view() {
    return View<Ts...>(*this);
}

}  // namespace nova::scene

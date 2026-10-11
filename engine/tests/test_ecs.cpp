// Unit tests — M2 ECS: entity handles, sparse-set storages, World, Views.
#include <doctest.h>

#include "scene/ComponentStorage.h"
#include "scene/Entity.h"
#include "scene/View.h"
#include "scene/World.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using nova::scene::ComponentStorage;
using nova::scene::Entity;
using nova::scene::kNullEntity;
using nova::scene::SparseSet;
using nova::scene::View;
using nova::scene::World;

namespace {

// Components exercised by these tests. Trivial ones cover the common case;
// Label covers non-trivial move semantics; Tag covers a zero-payload marker.
struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

struct Label {
    std::string text;
};

struct Tag {};

/// Sorted list of entity indices, for order-independent comparisons (sparse-set
/// iteration order is deliberately unspecified).
std::vector<nova::u32> sortedIndices(const std::vector<Entity>& entities) {
    std::vector<nova::u32> out;
    out.reserve(entities.size());
    for (const Entity& entity : entities) {
        out.push_back(entity.index);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

// --------------------------------------------------------------------- Entity

TEST_CASE("Entity handles default to null and compare by value") {
    const Entity nullHandle;
    CHECK(nullHandle.isNull());
    CHECK(nullHandle.index == Entity::kInvalidIndex);
    CHECK(nullHandle.generation == 0);
    CHECK(nullHandle == kNullEntity);

    const Entity a{3, 1};
    const Entity b{3, 1};
    const Entity c{3, 2};
    CHECK(a == b);
    CHECK(a != c);
    CHECK(!a.isNull());
}

TEST_CASE("Entity can key unordered containers") {
    std::unordered_map<Entity, int> table;
    const Entity a{1, 0};
    const Entity b{1, 1};  // same slot, later generation
    table[a] = 10;
    table[b] = 20;
    REQUIRE(table.size() == 2);
    CHECK(table[a] == 10);
    CHECK(table[b] == 20);

    std::unordered_set<Entity> set{a, b, a};
    CHECK(set.size() == 2);
}

// ------------------------------------------------------------------ SparseSet

TEST_CASE("SparseSet inserts, finds and swap-removes in O(1)") {
    SparseSet set;
    CHECK(set.size() == 0);
    CHECK(!set.contains(7));

    const nova::u32 posA = set.insert(7);
    const nova::u32 posB = set.insert(9);
    const nova::u32 posC = set.insert(2);
    CHECK(posA == 0);
    CHECK(posB == 1);
    CHECK(posC == 2);
    CHECK(set.size() == 3);
    CHECK(set.densePosition(9) == 1);
    CHECK(set.denseIndexAt(1) == 9);

    SUBCASE("removing a middle entry moves the tail into the hole") {
        set.erase(9);
        CHECK(set.size() == 2);
        CHECK(!set.contains(9));
        CHECK(set.densePosition(9) == SparseSet::kInvalid);
        // Index 2 took the vacated dense slot.
        CHECK(set.densePosition(2) == 1);
        CHECK(set.denseIndexAt(1) == 2);
        CHECK(set.denseIndexAt(0) == 7);
    }

    SUBCASE("removing the tail leaves the rest untouched") {
        set.erase(2);
        CHECK(set.size() == 2);
        CHECK(set.densePosition(7) == 0);
        CHECK(set.densePosition(9) == 1);
    }

    SUBCASE("erasing an absent index is a no-op") {
        set.erase(1234);
        CHECK(set.size() == 3);
    }

    SUBCASE("clear removes every entry") {
        set.clear();
        CHECK(set.size() == 0);
        CHECK(!set.contains(7));
        CHECK(!set.contains(9));
    }
}

// ------------------------------------------------------------ ComponentStorage

TEST_CASE("ComponentStorage keeps values parallel to entity indices") {
    ComponentStorage<Position> storage;
    CHECK(storage.size() == 0);
    CHECK(storage.componentSize() == sizeof(Position));

    Position& first = storage.emplace(4, Position{1.0f, 2.0f, 3.0f});
    first.z = 9.0f;
    storage.emplace(1, Position{7.0f, 7.0f, 7.0f});
    REQUIRE(storage.size() == 2);

    const Position* found = storage.find(4);
    REQUIRE(found != nullptr);
    CHECK(found->x == 1.0f);
    CHECK(found->z == 9.0f);
    CHECK(storage.find(99) == nullptr);

    SUBCASE("re-emplacing replaces the value in place") {
        storage.emplace(4, Position{-1.0f, -1.0f, -1.0f});
        CHECK(storage.size() == 2);
        REQUIRE(storage.find(4) != nullptr);
        CHECK(storage.find(4)->x == -1.0f);
    }

    SUBCASE("erasing preserves the surviving values") {
        storage.eraseEntity(4);
        CHECK(storage.size() == 1);
        CHECK(storage.find(4) == nullptr);
        REQUIRE(storage.find(1) != nullptr);
        CHECK(storage.find(1)->x == 7.0f);
        CHECK(storage.denseIndices().size() == storage.size());
    }
}

TEST_CASE("ComponentStorage moves non-trivial components without self-move") {
    ComponentStorage<Label> storage;
    storage.emplace(0, Label{"zero"});
    storage.emplace(1, Label{"one"});
    storage.emplace(2, Label{"two"});

    // Erase the tail first (the self-move path), then a middle entry.
    storage.eraseEntity(2);
    storage.eraseEntity(0);
    REQUIRE(storage.size() == 1);
    const Label* remaining = storage.find(1);
    REQUIRE(remaining != nullptr);
    CHECK(remaining->text == "one");
}

TEST_CASE("Component type ids are unique per type and stable per process") {
    const nova::u32 positionId = nova::scene::componentTypeId<Position>();
    const nova::u32 velocityId = nova::scene::componentTypeId<Velocity>();
    CHECK(positionId != velocityId);
    CHECK(positionId == nova::scene::componentTypeId<Position>());
}

// ---------------------------------------------------------------------- World

TEST_CASE("World creates entities with dense indices and tracks liveness") {
    World world;
    CHECK(world.aliveCount() == 0);
    CHECK(world.entitySlots() == 0);

    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();

    CHECK(a.index == 0);
    CHECK(b.index == 1);
    CHECK(c.index == 2);
    CHECK(world.aliveCount() == 3);
    CHECK(world.isAlive(a));
    CHECK(world.isAlive(b));
    CHECK(world.isAlive(c));
    CHECK(!world.isAlive(kNullEntity));
    CHECK(world.entityAt(1) == b);

    std::vector<Entity> batch;
    world.create(batch, 4);
    REQUIRE(batch.size() == 4);
    CHECK(batch.front().index == 3);
    CHECK(world.aliveCount() == 7);
}

TEST_CASE("Destroying an entity invalidates stale handles and recycles the slot") {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();

    CHECK(world.destroy(b));
    CHECK(!world.isAlive(b));
    CHECK(world.aliveCount() == 2);
    CHECK(world.entityAt(b.index) == kNullEntity);

    // The slot is reused, but with a bumped generation: the old handle stays
    // dead while the new one is alive.
    const Entity recycled = world.create();
    CHECK(recycled.index == b.index);
    CHECK(recycled.generation != b.generation);
    CHECK(!world.isAlive(b));
    CHECK(world.isAlive(recycled));
    CHECK(world.entityAt(recycled.index) == recycled);
    CHECK(world.aliveCount() == 3);

    // Other handles are unaffected.
    CHECK(world.isAlive(a));
    CHECK(world.isAlive(c));

    SUBCASE("destroying twice reports failure the second time") {
        CHECK(!world.destroy(b));
        CHECK(world.aliveCount() == 3);
    }
}

TEST_CASE("Components are added, replaced, read and removed") {
    World world;
    const Entity entity = world.create();

    CHECK(world.get<Position>(entity) == nullptr);
    CHECK(!world.has<Position>(entity));
    CHECK(world.count<Position>() == 0);

    Position* added = world.add<Position>(entity, Position{1.0f, 2.0f, 3.0f});
    REQUIRE(added != nullptr);
    CHECK(added->y == 2.0f);
    CHECK(world.has<Position>(entity));
    CHECK(world.count<Position>() == 1);

    // Mutation through the returned pointer is visible through get().
    added->x = 42.0f;
    REQUIRE(world.get<Position>(entity) != nullptr);
    CHECK(world.get<Position>(entity)->x == 42.0f);

    // Adding again replaces in place rather than duplicating.
    Position* replaced = world.add<Position>(entity, Position{-1.0f, -1.0f, -1.0f});
    REQUIRE(replaced != nullptr);
    CHECK(replaced->x == -1.0f);
    CHECK(world.count<Position>() == 1);

    // Zero-argument construction value-initializes.
    const Entity other = world.create();
    REQUIRE(world.add<Velocity>(other) != nullptr);
    CHECK(world.get<Velocity>(other)->dx == 0.0f);

    CHECK(world.remove<Position>(entity));
    CHECK(!world.has<Position>(entity));
    CHECK(world.count<Position>() == 0);
    CHECK(!world.remove<Position>(entity));  // already gone
}

TEST_CASE("Component operations on dead or null entities are rejected") {
    World world;
    const Entity alive = world.create();
    const Entity dead = world.create();
    world.add<Position>(dead, Position{1.0f, 1.0f, 1.0f});
    CHECK(world.destroy(dead));

    CHECK(world.add<Position>(dead, Position{2.0f, 2.0f, 2.0f}) == nullptr);
    CHECK(world.add<Position>(kNullEntity) == nullptr);
    CHECK(world.get<Position>(dead) == nullptr);
    CHECK(world.get<Position>(kNullEntity) == nullptr);
    CHECK(!world.has<Position>(dead));
    CHECK(!world.remove<Position>(dead));
    CHECK(!world.destroy(dead));
    CHECK(!world.destroy(kNullEntity));

    // The rejection must not disturb live state.
    CHECK(world.aliveCount() == 1);
    CHECK(world.isAlive(alive));
    CHECK(world.count<Position>() == 0);
}

TEST_CASE("Destroying an entity purges its components from every storage") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 3);
    for (std::size_t i = 0; i < entities.size(); ++i) {
        world.add<Position>(entities[i], Position{static_cast<float>(i), 0.0f, 0.0f});
        world.add<Label>(entities[i], Label{"entity-" + std::to_string(i)});
        if (i == 1) {
            world.add<Velocity>(entities[i], Velocity{5.0f, 5.0f});
        }
    }
    REQUIRE(world.count<Position>() == 3);
    REQUIRE(world.count<Label>() == 3);
    REQUIRE(world.count<Velocity>() == 1);

    CHECK(world.destroy(entities[1]));

    CHECK(world.count<Position>() == 2);
    CHECK(world.count<Label>() == 2);
    CHECK(world.count<Velocity>() == 0);
    CHECK(world.get<Position>(entities[1]) == nullptr);

    // Survivors keep their exact values.
    REQUIRE(world.get<Label>(entities[0]) != nullptr);
    CHECK(world.get<Label>(entities[0])->text == "entity-0");
    REQUIRE(world.get<Label>(entities[2]) != nullptr);
    CHECK(world.get<Label>(entities[2])->text == "entity-2");
    REQUIRE(world.get<Position>(entities[2]) != nullptr);
    CHECK(world.get<Position>(entities[2])->x == 2.0f);
}

TEST_CASE("Non-trivial components survive interleaved destruction") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 6);
    const char* names[] = {"a", "b", "c", "d", "e", "f"};
    for (std::size_t i = 0; i < entities.size(); ++i) {
        world.add<Label>(entities[i], Label{names[i]});
    }

    // Destroy a middle, the tail, then another middle: exercises both the
    // swap-remove path and the self-move path in every storage.
    CHECK(world.destroy(entities[2]));
    CHECK(world.destroy(entities[5]));
    CHECK(world.destroy(entities[0]));
    REQUIRE(world.count<Label>() == 3);

    REQUIRE(world.get<Label>(entities[1]) != nullptr);
    CHECK(world.get<Label>(entities[1])->text == "b");
    REQUIRE(world.get<Label>(entities[3]) != nullptr);
    CHECK(world.get<Label>(entities[3])->text == "d");
    REQUIRE(world.get<Label>(entities[4]) != nullptr);
    CHECK(world.get<Label>(entities[4])->text == "e");
}

TEST_CASE("Zero-payload marker components are storable and countable") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 4);
    world.add<Tag>(entities[0]);
    world.add<Tag>(entities[3]);
    CHECK(world.count<Tag>() == 2);
    CHECK(world.has<Tag>(entities[0]));
    CHECK(!world.has<Tag>(entities[1]));
    CHECK(world.get<Tag>(entities[0]) != nullptr);
}

TEST_CASE("World stats report entities, component types and memory") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 5);
    for (const Entity& entity : entities) {
        world.add<Position>(entity);
    }
    world.add<Label>(entities[0], Label{"hello"});
    world.destroy(entities[4]);

    const World::Stats stats = world.stats();
    CHECK(stats.entitySlots == 5);
    CHECK(stats.aliveEntities == 4);
    CHECK(stats.componentTypes == 2);
    CHECK(stats.components == 5);  // 4 Position + 1 Label
    CHECK(stats.memoryBytes > 0);
    CHECK(stats.memoryBytes == world.componentMemoryBytes());
}

// ---------------------------------------------------------------------- Views

TEST_CASE("A view visits exactly the entities owning all requested components") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 6);

    // Position on 0, 2, 4 · Velocity on 2, 4, 5 · both on 2, 4
    world.add<Position>(entities[0]);
    world.add<Position>(entities[2]);
    world.add<Position>(entities[4]);
    world.add<Velocity>(entities[2]);
    world.add<Velocity>(entities[4]);
    world.add<Velocity>(entities[5]);

    std::vector<Entity> visited;
    for (auto entry : world.view<Position, Velocity>()) {
        visited.push_back(std::get<Entity>(entry));
    }
    CHECK(sortedIndices(visited) == std::vector<nova::u32>{2, 4});

    SUBCASE("single-component view sees every owner") {
        std::vector<Entity> positions;
        world.view<Position>().collectEntities(positions);
        CHECK(sortedIndices(positions) == std::vector<nova::u32>{0, 2, 4});
        CHECK(world.view<Position>().size() == 3);
        CHECK(world.view<Velocity>().size() == 3);
    }

    SUBCASE("size() and iteration agree") {
        CHECK(world.view<Position, Velocity>().size() == 2);
        CHECK(!world.view<Position, Velocity>().empty());
    }
}

TEST_CASE("Views expose components for in-place mutation") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 3);
    for (std::size_t i = 0; i < entities.size(); ++i) {
        world.add<Position>(entities[i], Position{static_cast<float>(i), 0.0f, 0.0f});
        world.add<Velocity>(entities[i], Velocity{1.0f, 2.0f});
    }

    // Structured bindings over the tuple yielded by the iterator.
    for (auto [entity, position, velocity] : world.view<Position, Velocity>()) {
        CHECK(world.isAlive(entity));
        position.x += velocity.dx;
        position.y += velocity.dy;
    }

    for (std::size_t i = 0; i < entities.size(); ++i) {
        const Position* position = world.get<Position>(entities[i]);
        REQUIRE(position != nullptr);
        CHECK(position->x == static_cast<float>(i) + 1.0f);
        CHECK(position->y == 2.0f);
    }
}

TEST_CASE("A view over a component type that was never used is empty") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 3);
    world.add<Position>(entities[0]);

    CHECK(world.storage<Velocity>() == nullptr);
    CHECK(world.view<Velocity>().empty());
    CHECK(world.view<Velocity>().size() == 0);
    CHECK(world.view<Velocity>().begin() == world.view<Velocity>().end());
    // Mixed request where one storage does not exist yet.
    CHECK(world.view<Position, Velocity>().empty());

    std::size_t calls = 0;
    world.view<Velocity>().each([&](Entity, Velocity&) { ++calls; });
    CHECK(calls == 0);
}

TEST_CASE("Views pick the smallest storage and still match correctly") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 100);

    // Position: 50 owners · Velocity: 5 owners · both: 3
    for (std::size_t i = 0; i < 50; ++i) {
        world.add<Position>(entities[i], Position{static_cast<float>(i), 0.0f, 0.0f});
    }
    for (std::size_t i = 0; i < 5; ++i) {
        world.add<Velocity>(entities[40 + i], Velocity{1.0f, 1.0f});
    }

    CHECK(world.count<Position>() == 50);
    CHECK(world.count<Velocity>() == 5);

    std::vector<Entity> both;
    world.view<Position, Velocity>().collectEntities(both);
    // Position covers entities[0..49], Velocity covers entities[40..44], so
    // the intersection is exactly indices 40..44.
    CHECK(sortedIndices(both) == std::vector<nova::u32>{40, 41, 42, 43, 44});
    CHECK(world.view<Position, Velocity>().size() == 5);

    // Order of the template arguments must not change the result.
    CHECK(world.view<Velocity, Position>().size() == 5);
}

TEST_CASE("View::each visits every match exactly once") {
    World world;
    std::vector<Entity> entities;
    world.create(entities, 8);
    for (std::size_t i = 0; i < entities.size(); i += 2) {
        world.add<Position>(entities[i], Position{static_cast<float>(i), 0.0f, 0.0f});
        world.add<Velocity>(entities[i]);
    }

    float sum = 0.0f;
    const std::size_t visited = world.view<Position, Velocity>().each(
        [&sum](Entity, Position& position, Velocity&) { sum += position.x; });

    CHECK(visited == 4);
    CHECK(sum == (0.0f + 2.0f + 4.0f + 6.0f));
}

TEST_CASE("Bulk create, component churn and destroy keep the registry consistent") {
    constexpr std::size_t kEntityCount = 10000;

    World world;
    std::vector<Entity> entities;
    world.create(entities, kEntityCount);
    REQUIRE(entities.size() == kEntityCount);
    CHECK(world.aliveCount() == kEntityCount);

    for (std::size_t i = 0; i < kEntityCount; ++i) {
        world.add<Position>(entities[i], Position{static_cast<float>(i), 0.0f, 0.0f});
        if (i % 3 == 0) {
            world.add<Velocity>(entities[i], Velocity{static_cast<float>(i), 0.0f});
        }
    }
    CHECK(world.count<Position>() == kEntityCount);
    CHECK(world.count<Velocity>() == 3334);

    // Views still intersect correctly at scale.
    std::size_t bothCount = 0;
    for ([[maybe_unused]] auto entry : world.view<Position, Velocity>()) {
        ++bothCount;
    }
    CHECK(bothCount == 3334);

    // Destroy every even slot.
    for (std::size_t i = 0; i < kEntityCount; i += 2) {
        CHECK(world.destroy(entities[i]));
    }
    CHECK(world.aliveCount() == kEntityCount / 2);
    CHECK(world.count<Position>() == kEntityCount / 2);

    // Survivors keep their payload; stale handles are dead.
    for (std::size_t i = 1; i < kEntityCount; i += 2) {
        REQUIRE(world.isAlive(entities[i]));
        const Position* position = world.get<Position>(entities[i]);
        REQUIRE(position != nullptr);
        CHECK(position->x == static_cast<float>(i));
    }
    for (std::size_t i = 0; i < kEntityCount; i += 2) {
        CHECK(!world.isAlive(entities[i]));
        CHECK(world.get<Position>(entities[i]) == nullptr);
    }

    // Recycled slots get fresh generations and are reusable.
    std::vector<Entity> reused;
    world.create(reused, 100);
    CHECK(world.aliveCount() == kEntityCount / 2 + 100);
    for (const Entity& entity : reused) {
        CHECK(world.isAlive(entity));
        CHECK(world.add<Label>(entity, Label{"reused"}) != nullptr);
    }
    CHECK(world.count<Label>() == 100);
}

// Unit tests — M2 transform hierarchy: components, parenting, world matrices.
#include <doctest.h>

#include "core/Math.h"
#include "scene/Entity.h"
#include "scene/SceneHierarchy.h"
#include "scene/Transform.h"
#include "scene/View.h"
#include "scene/World.h"

#include <algorithm>
#include <type_traits>
#include <vector>

using nova::math::Mat4;
using nova::math::Vec3;
using nova::usize;
using nova::scene::Entity;
using nova::scene::kNullEntity;
using nova::scene::Parent;
using nova::scene::SceneHierarchy;
using nova::scene::Transform;
using nova::scene::World;
using nova::scene::WorldTransform;

namespace {

struct Marker {};

Vec3 positionOf(const World& world, Entity entity) {
    const WorldTransform* cached = world.get<WorldTransform>(entity);
    return cached != nullptr ? cached->position() : Vec3(0.0f);
}

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

// ----------------------------------------------------------------- components

TEST_CASE("Transform components stay trivially copyable") {
    CHECK(std::is_trivially_copyable_v<Transform>);
    CHECK(std::is_trivially_copyable_v<Parent>);
    CHECK(std::is_trivially_copyable_v<WorldTransform>);
}

TEST_CASE("Transform::localMatrix composes translation, rotation and scale") {
    SUBCASE("default transform is the identity") {
        const Transform transform{};
        const Mat4 matrix = transform.localMatrix();
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                CHECK(matrix[column][row] == doctest::Approx(column == row ? 1.0f : 0.0f));
            }
        }
    }

    SUBCASE("translation lands in the fourth column") {
        const Transform transform = Transform::fromPosition(Vec3(2.0f, 3.0f, 4.0f));
        const Mat4 matrix = transform.localMatrix();
        CHECK(matrix[3][0] == doctest::Approx(2.0f));
        CHECK(matrix[3][1] == doctest::Approx(3.0f));
        CHECK(matrix[3][2] == doctest::Approx(4.0f));
        CHECK(transform.position.x == doctest::Approx(2.0f));
    }

    SUBCASE("scale multiplies the basis axes") {
        Transform transform{};
        transform.scale = Vec3(2.0f, 3.0f, 4.0f);
        const Mat4 matrix = transform.localMatrix();
        CHECK(matrix[0][0] == doctest::Approx(2.0f));
        CHECK(matrix[1][1] == doctest::Approx(3.0f));
        CHECK(matrix[2][2] == doctest::Approx(4.0f));
    }

    SUBCASE("a 90 degree Z rotation maps +X onto +Y") {
        const Transform transform =
            Transform::fromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), nova::math::kHalfPi);
        const Mat4 matrix = transform.localMatrix();
        const Vec3 rotated = Vec3(matrix * nova::math::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
        CHECK(rotated.x == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(rotated.y == doctest::Approx(1.0f).epsilon(1e-5));
        CHECK(rotated.z == doctest::Approx(0.0f).epsilon(1e-5));
    }
}

// ------------------------------------------------------------------ parenting

TEST_CASE("setParent links entities and detach makes them roots again") {
    World world;
    SceneHierarchy hierarchy;
    const Entity root = world.create();
    const Entity child = world.create();
    const Entity grandChild = world.create();

    CHECK(hierarchy.setParent(world, child, root));
    CHECK(hierarchy.setParent(world, grandChild, child));

    REQUIRE(world.has<Parent>(child));
    CHECK(world.get<Parent>(child)->entity == root);
    REQUIRE(world.has<Transform>(child));  // added on demand
    // The parent gets an identity Transform too, otherwise the link would be
    // unusable and the child would be demoted to an orphan root.
    REQUIRE(world.has<Transform>(root));
    CHECK(world.get<Transform>(root)->localMatrix() == nova::math::identity());

    CHECK(hierarchy.depthOf(world, root) == 0);
    CHECK(hierarchy.depthOf(world, child) == 1);
    CHECK(hierarchy.depthOf(world, grandChild) == 2);

    CHECK(hierarchy.isAncestorOf(world, grandChild, root));
    CHECK(hierarchy.isAncestorOf(world, grandChild, child));
    CHECK(hierarchy.isAncestorOf(world, grandChild, grandChild));  // self counts
    CHECK(!hierarchy.isAncestorOf(world, root, grandChild));

    // Children become visible after an update() builds the CSR lists.
    hierarchy.update(world);
    std::vector<Entity> children;
    hierarchy.childrenOf(world, root, children);
    CHECK(sortedIndices(children) == std::vector<nova::u32>{child.index});
    children.clear();
    hierarchy.childrenOf(world, child, children);
    CHECK(sortedIndices(children) == std::vector<nova::u32>{grandChild.index});
    children.clear();
    hierarchy.childrenOf(world, grandChild, children);
    CHECK(children.empty());

    SUBCASE("detach removes the link") {
        CHECK(hierarchy.detach(world, grandChild));
        REQUIRE(world.has<Parent>(grandChild));
        CHECK(world.get<Parent>(grandChild)->entity == kNullEntity);
        CHECK(hierarchy.depthOf(world, grandChild) == 0);
        CHECK(!hierarchy.isAncestorOf(world, grandChild, child));
    }
}

TEST_CASE("setParent rejects dead handles, self-parenting and cycles") {
    World world;
    SceneHierarchy hierarchy;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    const Entity dead = world.create();
    CHECK(world.destroy(dead));

    CHECK(!hierarchy.setParent(world, dead, a));
    CHECK(!hierarchy.setParent(world, a, dead));
    CHECK(!hierarchy.setParent(world, a, a));  // an entity cannot parent itself

    CHECK(hierarchy.setParent(world, b, a));
    CHECK(hierarchy.setParent(world, c, b));
    // a -> b -> c already exists; linking a under c would close the loop.
    CHECK(!hierarchy.setParent(world, a, c));
    CHECK(!hierarchy.setParent(world, a, b));
    // The existing structure is untouched.
    CHECK(hierarchy.depthOf(world, c) == 2);
    const Parent* parentOfA = world.get<Parent>(a);
    const bool aIsStillRoot = (parentOfA == nullptr) || parentOfA->entity.isNull();
    CHECK(aIsStillRoot);
}

// ------------------------------------------------------- world matrix updates

TEST_CASE("update() creates WorldTransform and chains translations top-down") {
    World world;
    SceneHierarchy hierarchy;
    const Entity root = world.create();
    const Entity child = world.create();
    world.add<Transform>(root, Transform::fromPosition(Vec3(10.0f, 0.0f, 0.0f)));
    world.add<Transform>(child, Transform::fromPosition(Vec3(1.0f, 2.0f, 3.0f)));
    CHECK(!world.has<WorldTransform>(root));

    CHECK(hierarchy.setParent(world, child, root));
    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);

    CHECK(world.has<WorldTransform>(root));
    CHECK(world.has<WorldTransform>(child));
    CHECK(stats.roots == 1);
    CHECK(stats.transformsWritten == 2);
    CHECK(stats.orphans == 0);
    CHECK(stats.cyclic == 0);
    CHECK(stats.deepestLevel == 1);

    const Vec3 rootWorld = positionOf(world, root);
    CHECK(rootWorld.x == doctest::Approx(10.0f));
    const Vec3 childWorld = positionOf(world, child);
    CHECK(childWorld.x == doctest::Approx(11.0f));
    CHECK(childWorld.y == doctest::Approx(2.0f));
    CHECK(childWorld.z == doctest::Approx(3.0f));
}

TEST_CASE("update() inherits parent rotation") {
    World world;
    SceneHierarchy hierarchy;
    const Entity root = world.create();
    const Entity child = world.create();
    world.add<Transform>(
        root, Transform::fromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), nova::math::kHalfPi));
    world.add<Transform>(child, Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
    CHECK(hierarchy.setParent(world, child, root));

    hierarchy.update(world);

    // Rotating +X by 90 degrees about Z puts the child on +Y.
    const Vec3 childWorld = positionOf(world, child);
    CHECK(childWorld.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(childWorld.y == doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(childWorld.z == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("update() inherits parent scale") {
    World world;
    SceneHierarchy hierarchy;
    const Entity root = world.create();
    const Entity child = world.create();
    Transform rootTransform{};
    rootTransform.scale = Vec3(3.0f, 3.0f, 3.0f);
    world.add<Transform>(root, rootTransform);
    world.add<Transform>(child, Transform::fromPosition(Vec3(2.0f, 1.0f, 0.0f)));
    CHECK(hierarchy.setParent(world, child, root));

    hierarchy.update(world);

    const Vec3 childWorld = positionOf(world, child);
    CHECK(childWorld.x == doctest::Approx(6.0f));
    CHECK(childWorld.y == doctest::Approx(3.0f));
}

TEST_CASE("Three-level hierarchies accumulate every ancestor") {
    World world;
    SceneHierarchy hierarchy;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    world.add<Transform>(a, Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
    world.add<Transform>(b, Transform::fromPosition(Vec3(2.0f, 0.0f, 0.0f)));
    world.add<Transform>(c, Transform::fromPosition(Vec3(4.0f, 0.0f, 0.0f)));
    CHECK(hierarchy.setParent(world, b, a));
    CHECK(hierarchy.setParent(world, c, b));

    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);
    CHECK(stats.roots == 1);
    CHECK(stats.deepestLevel == 2);
    CHECK(stats.transformsWritten == 3);
    CHECK(positionOf(world, a).x == doctest::Approx(1.0f));
    CHECK(positionOf(world, b).x == doctest::Approx(3.0f));
    CHECK(positionOf(world, c).x == doctest::Approx(7.0f));
}

TEST_CASE("Repeated updates are idempotent and pick up edits") {
    World world;
    SceneHierarchy hierarchy;
    const Entity root = world.create();
    const Entity child = world.create();
    world.add<Transform>(root, Transform::fromPosition(Vec3(5.0f, 0.0f, 0.0f)));
    world.add<Transform>(child, Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
    CHECK(hierarchy.setParent(world, child, root));

    hierarchy.update(world);
    const Vec3 first = positionOf(world, child);
    hierarchy.update(world);
    const Vec3 second = positionOf(world, child);
    CHECK(first.x == doctest::Approx(second.x));
    CHECK(first.y == doctest::Approx(second.y));

    // Moving the parent moves the child on the next update.
    world.get<Transform>(root)->position = Vec3(20.0f, 0.0f, 0.0f);
    hierarchy.update(world);
    CHECK(positionOf(world, child).x == doctest::Approx(21.0f));

    // Detaching restores the child's local frame.
    CHECK(hierarchy.detach(world, child));
    hierarchy.update(world);
    CHECK(positionOf(world, child).x == doctest::Approx(1.0f));
}

// --------------------------------------------------------------- edge cases

TEST_CASE("Entities without a Transform are ignored by the hierarchy") {
    World world;
    SceneHierarchy hierarchy;
    const Entity withTransform = world.create();
    const Entity withoutTransform = world.create();
    world.add<Transform>(withTransform, Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
    world.add<Marker>(withoutTransform);

    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);
    CHECK(stats.roots == 1);
    CHECK(stats.transformsWritten == 1);
    CHECK(!world.has<WorldTransform>(withoutTransform));
}

TEST_CASE("A destroyed parent leaves the child an orphan root") {
    World world;
    SceneHierarchy hierarchy;
    const Entity root = world.create();
    const Entity child = world.create();
    world.add<Transform>(root, Transform::fromPosition(Vec3(10.0f, 0.0f, 0.0f)));
    world.add<Transform>(child, Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
    CHECK(hierarchy.setParent(world, child, root));
    hierarchy.update(world);
    CHECK(positionOf(world, child).x == doctest::Approx(11.0f));

    CHECK(world.destroy(root));
    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);

    CHECK(stats.roots == 1);
    CHECK(stats.orphans == 1);
    CHECK(stats.transformsWritten == 1);
    // The stale world matrix is recomputed from the local transform only.
    CHECK(positionOf(world, child).x == doctest::Approx(1.0f));
}

TEST_CASE("Parent components pointing at a Transform-less entity orphan the child") {
    World world;
    SceneHierarchy hierarchy;
    const Entity anchor = world.create();  // alive, but no Transform
    const Entity child = world.create();
    world.add<Transform>(child, Transform::fromPosition(Vec3(4.0f, 0.0f, 0.0f)));
    world.add<Parent>(child, Parent{anchor});

    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);
    CHECK(stats.roots == 1);
    CHECK(stats.orphans == 1);
    CHECK(positionOf(world, child).x == doctest::Approx(4.0f));
}

TEST_CASE("Cycles written directly into Parent components are reported, not fatal") {
    World world;
    SceneHierarchy hierarchy;
    const Entity a = world.create();
    const Entity b = world.create();
    world.add<Transform>(a, Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
    world.add<Transform>(b, Transform::fromPosition(Vec3(2.0f, 0.0f, 0.0f)));
    // Bypass setParent to build a cycle the way corrupted data would.
    world.add<Parent>(a, Parent{b});
    world.add<Parent>(b, Parent{a});

    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);
    CHECK(stats.roots == 0);
    CHECK(stats.transformsWritten == 0);
    CHECK(stats.cyclic == 2);
    // Best effort: both fall back to their local matrices.
    CHECK(positionOf(world, a).x == doctest::Approx(1.0f));
    CHECK(positionOf(world, b).x == doctest::Approx(2.0f));
}

TEST_CASE("A self-referencing Parent component is treated as cyclic, not as a root") {
    World world;
    SceneHierarchy hierarchy;
    const Entity entity = world.create();
    world.add<Transform>(entity, Transform::fromPosition(Vec3(7.0f, 0.0f, 0.0f)));
    world.add<Parent>(entity, Parent{entity});

    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);
    CHECK(stats.cyclic == 1);
    CHECK(stats.roots == 0);
    CHECK(positionOf(world, entity).x == doctest::Approx(7.0f));
}

TEST_CASE("Deep chains evaluate without recursion and keep exact accumulation") {
    constexpr usize kDepth = 1000;

    World world;
    SceneHierarchy hierarchy;
    std::vector<Entity> chain;
    world.create(chain, kDepth);
    for (const Entity& entity : chain) {
        world.add<Transform>(entity, Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
    }
    for (usize i = 1; i < kDepth; ++i) {
        REQUIRE(hierarchy.setParent(world, chain[i], chain[i - 1]));
    }

    const SceneHierarchy::UpdateStats stats = hierarchy.update(world);
    CHECK(stats.roots == 1);
    CHECK(stats.deepestLevel == kDepth - 1);
    CHECK(stats.transformsWritten == kDepth);
    CHECK(hierarchy.depthOf(world, chain.back()) == kDepth - 1);
    CHECK(positionOf(world, chain.back()).x == doctest::Approx(static_cast<float>(kDepth)));
    CHECK(positionOf(world, chain[kDepth / 2]).x ==
          doctest::Approx(static_cast<float>(kDepth / 2 + 1)));
}

TEST_CASE("Wide hierarchies and repeated updates stay consistent at scale") {
    constexpr usize kRoots = 64;
    constexpr usize kChildrenPerRoot = 128;

    World world;
    SceneHierarchy hierarchy;
    std::vector<Entity> roots;
    world.create(roots, kRoots);
    for (std::size_t r = 0; r < roots.size(); ++r) {
        world.add<Transform>(
            roots[r], Transform::fromPosition(Vec3(static_cast<float>(r) * 100.0f, 0.0f, 0.0f)));
    }
    std::vector<Entity> children;
    world.create(children, kRoots * kChildrenPerRoot);
    for (std::size_t i = 0; i < children.size(); ++i) {
        world.add<Transform>(children[i], Transform::fromPosition(Vec3(1.0f, 0.0f, 0.0f)));
        REQUIRE(hierarchy.setParent(world, children[i], roots[i / kChildrenPerRoot]));
    }

    SceneHierarchy::UpdateStats stats = hierarchy.update(world);
    CHECK(stats.roots == kRoots);
    CHECK(stats.deepestLevel == 1);
    CHECK(stats.transformsWritten == kRoots + kRoots * kChildrenPerRoot);
    CHECK(stats.cyclic == 0);

    for (std::size_t i = 0; i < children.size(); ++i) {
        const float expected = static_cast<float>(i / kChildrenPerRoot) * 100.0f + 1.0f;
        CHECK(positionOf(world, children[i]).x == doctest::Approx(expected));
    }

    // Destroy a whole subtree root: its children become orphan roots.
    CHECK(world.destroy(roots[3]));
    stats = hierarchy.update(world);
    CHECK(stats.orphans == kChildrenPerRoot);
    CHECK(stats.roots == kRoots - 1 + kChildrenPerRoot);
    CHECK(stats.cyclic == 0);

    std::vector<Entity> childrenOfMissing;
    hierarchy.childrenOf(world, roots[3], childrenOfMissing);
    CHECK(childrenOfMissing.empty());  // dead handle yields nothing

    hierarchy.reset();
    childrenOfMissing.clear();
    hierarchy.childrenOf(world, roots[0], childrenOfMissing);
    CHECK(childrenOfMissing.empty());  // scratch dropped; rebuilds on next update
}

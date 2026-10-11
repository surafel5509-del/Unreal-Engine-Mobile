// Unit tests — M2 cameras, frustum culling and draw-list ordering.
#include <doctest.h>

#include "core/Math.h"
#include "scene/Camera.h"
#include "scene/Culling.h"
#include "scene/Frustum.h"
#include "scene/SceneHierarchy.h"
#include "scene/Transform.h"
#include "scene/View.h"
#include "scene/World.h"

#include <algorithm>
#include <type_traits>
#include <vector>

using nova::math::Mat4;
using nova::math::Vec3;
using nova::math::Vec4;
using nova::usize;
using nova::scene::Bounds;
using nova::scene::Camera;
using nova::scene::CullStats;
using nova::scene::Entity;
using nova::scene::Frustum;
using nova::scene::ProjectionType;
using nova::scene::SceneHierarchy;
using nova::scene::Transform;
using nova::scene::World;
using nova::scene::WorldTransform;

namespace {

/// Perspective camera at `position` looking down -Z (identity rotation).
Camera makePerspective(float aspect = 1.0f, float nearPlane = 0.1f, float farPlane = 100.0f) {
    Camera camera{};
    camera.projection = ProjectionType::Perspective;
    camera.fovYDegrees = 60.0f;
    camera.aspectRatio = aspect;
    camera.nearPlane = nearPlane;
    camera.farPlane = farPlane;
    return camera;
}

/// Place a camera entity: Transform + matching WorldTransform + Camera.
Entity placeCamera(World& world, const Vec3& position, const Camera& camera) {
    const Entity entity = world.create();
    world.add<Transform>(entity, Transform::fromPosition(position));
    world.add<WorldTransform>(entity, WorldTransform{nova::math::translation(position)});
    world.add<Camera>(entity, camera);
    return entity;
}

/// Place a renderable entity: Transform + matching WorldTransform + Bounds.
Entity placeObject(World& world, const Vec3& position, const Vec3& halfExtent = Vec3(0.5f)) {
    const Entity entity = world.create();
    world.add<Transform>(entity, Transform::fromPosition(position));
    world.add<WorldTransform>(entity, WorldTransform{nova::math::translation(position)});
    world.add<Bounds>(entity, Bounds{Vec3(0.0f), halfExtent});
    return entity;
}

/// Clip space -> NDC.
Vec3 toNdc(const Mat4& viewProjection, const Vec3& point) {
    const Vec4 clip = viewProjection * Vec4(point, 1.0f);
    REQUIRE(clip.w != 0.0f);
    return Vec3(clip) / clip.w;
}

bool containsEntity(const std::vector<Entity>& list, Entity entity) {
    return std::find(list.begin(), list.end(), entity) != list.end();
}

}  // namespace

// --------------------------------------------------------------------- camera

TEST_CASE("Camera and Bounds stay trivially copyable") {
    CHECK(std::is_trivially_copyable_v<Camera>);
    CHECK(std::is_trivially_copyable_v<Bounds>);
}

TEST_CASE("Perspective projection follows the Vulkan conventions") {
    const Camera camera = makePerspective(2.0f, 0.1f, 100.0f);
    const Mat4 projection = nova::scene::projectionMatrix(camera);

    // Same matrix the M1 renderer uses for its hard-coded camera.
    const Mat4 expected = nova::math::perspectiveVk(nova::math::toRadians(60.0f), 2.0f, 0.1f,
                                                    100.0f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(projection[column][row] == doctest::Approx(expected[column][row]));
        }
    }

    // Y is flipped so Y-up content renders upright.
    CHECK(projection[1][1] < 0.0f);

    // Identity view: depth maps near -> 0 and far -> 1.
    const WorldTransform atOrigin{nova::math::identity()};
    const Mat4 viewProjection = projection * nova::scene::viewMatrix(atOrigin);
    CHECK(toNdc(viewProjection, Vec3(0.0f, 0.0f, -0.1f)).z == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(toNdc(viewProjection, Vec3(0.0f, 0.0f, -100.0f)).z == doctest::Approx(1.0f).epsilon(1e-4));

    // A point above the axis lands below the centre after the flip.
    CHECK(toNdc(viewProjection, Vec3(0.0f, 1.0f, -10.0f)).y < 0.0f);
}

TEST_CASE("Orthographic projection maps the view volume onto clip space") {
    Camera camera{};
    camera.projection = ProjectionType::Orthographic;
    camera.orthoHalfHeight = 5.0f;
    camera.aspectRatio = 2.0f;  // half width = 10
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    const WorldTransform atOrigin{nova::math::identity()};
    const Mat4 viewProjection =
        nova::scene::projectionMatrix(camera) * nova::scene::viewMatrix(atOrigin);

    // Top-right corner at the near plane -> (1, -1, 0) after the Y flip.
    const Vec3 corner = toNdc(viewProjection, Vec3(10.0f, 5.0f, -0.1f));
    CHECK(corner.x == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(corner.y == doctest::Approx(-1.0f).epsilon(1e-4));
    CHECK(corner.z == doctest::Approx(0.0f).epsilon(1e-4));

    // Far plane maps to depth 1, and the volume is not perspective-divided.
    CHECK(toNdc(viewProjection, Vec3(0.0f, 0.0f, -100.0f)).z ==
          doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(toNdc(viewProjection, Vec3(-10.0f, -5.0f, -50.0f)).x ==
          doctest::Approx(-1.0f).epsilon(1e-4));
}

TEST_CASE("viewMatrix is the inverse of the camera world transform") {
    const WorldTransform placed{nova::math::translation(Vec3(0.0f, 0.0f, 5.0f))};
    const Mat4 view = nova::scene::viewMatrix(placed);

    // The world origin sits 5 units in front of the camera (down its -Z axis).
    const Vec4 originInView = view * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(originInView.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(originInView.y == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(originInView.z == doctest::Approx(-5.0f).epsilon(1e-5));

    CHECK(nova::scene::cameraPosition(placed) == Vec3(0.0f, 0.0f, 5.0f));
}

// -------------------------------------------------------------------- frustum

TEST_CASE("Frustum classifies points along the view axis") {
    const Camera camera = makePerspective(1.0f, 0.1f, 100.0f);
    const WorldTransform atOrigin{nova::math::identity()};
    const Frustum frustum(
        nova::scene::viewProjectionMatrix(camera, atOrigin));

    CHECK(frustum.contains(Vec3(0.0f, 0.0f, -10.0f)));    // in front
    CHECK(frustum.contains(Vec3(0.0f, 0.0f, -0.1f)));     // exactly at near
    CHECK(!frustum.contains(Vec3(0.0f, 0.0f, 10.0f)));    // behind the camera
    CHECK(!frustum.contains(Vec3(0.0f, 0.0f, -200.0f)));  // beyond the far plane
    CHECK(!frustum.contains(Vec3(0.0f, 0.0f, -0.01f)));   // before the near plane
    CHECK(!frustum.contains(Vec3(1000.0f, 0.0f, -10.0f)));  // far outside the sides

    // Near/far distances are true world-space distances after normalization.
    CHECK(frustum.distanceToPlane(Frustum::kNear, Vec3(0.0f, 0.0f, -0.1f)) ==
          doctest::Approx(0.0f).epsilon(1e-3));
}

TEST_CASE("Frustum sphere tests are conservative toward keeping geometry") {
    const Camera camera = makePerspective(1.0f, 0.1f, 100.0f);
    const WorldTransform atOrigin{nova::math::identity()};
    const Frustum frustum(nova::scene::viewProjectionMatrix(camera, atOrigin));

    CHECK(frustum.intersectsSphere(Vec3(0.0f, 0.0f, -10.0f), 1.0f));
    // Straddles the far plane: kept.
    CHECK(frustum.intersectsSphere(Vec3(0.0f, 0.0f, -101.0f), 5.0f));
    // Fully beyond the far plane: dropped.
    CHECK(!frustum.intersectsSphere(Vec3(0.0f, 0.0f, -200.0f), 5.0f));
    // Fully behind the camera: dropped.
    CHECK(!frustum.intersectsSphere(Vec3(0.0f, 0.0f, 20.0f), 5.0f));
}

TEST_CASE("Frustum AABB tests keep boxes that straddle a plane") {
    const Camera camera = makePerspective(1.0f, 0.1f, 100.0f);
    const WorldTransform atOrigin{nova::math::identity()};
    const Frustum frustum(nova::scene::viewProjectionMatrix(camera, atOrigin));

    CHECK(frustum.intersectsAabb(Vec3(0.0f, 0.0f, -10.0f), Vec3(1.0f)));
    // Half in, half out of the far plane.
    CHECK(frustum.intersectsAabb(Vec3(0.0f, 0.0f, -102.0f), Vec3(5.0f)));
    CHECK(!frustum.intersectsAabb(Vec3(0.0f, 0.0f, -200.0f), Vec3(1.0f)));
    CHECK(!frustum.intersectsAabb(Vec3(0.0f, 0.0f, 50.0f), Vec3(1.0f)));
    // A huge box centred behind the camera still reaches into the frustum.
    CHECK(frustum.intersectsAabb(Vec3(0.0f, 0.0f, 40.0f), Vec3(100.0f)));
}

// -------------------------------------------------------------------- culling

TEST_CASE("worldBounds derives a world AABB from transform and local bounds") {
    World world;
    const Entity entity = world.create();
    world.add<Transform>(entity, Transform::fromPosition(Vec3(10.0f, 0.0f, 0.0f)));
    world.add<WorldTransform>(entity,
                             WorldTransform{nova::math::translation(Vec3(10.0f, 0.0f, 0.0f))});
    world.add<Bounds>(entity, Bounds{Vec3(1.0f, 0.0f, 0.0f), Vec3(0.5f)});

    Vec3 center(0.0f);
    Vec3 halfExtent(0.0f);
    REQUIRE(nova::scene::worldBounds(world, entity, center, halfExtent));
    CHECK(center.x == doctest::Approx(11.0f));
    CHECK(halfExtent.x == doctest::Approx(0.5f));
    CHECK(halfExtent.y == doctest::Approx(0.5f));

    SUBCASE("entities without Bounds report failure") {
        const Entity bare = world.create();
        world.add<Transform>(bare);
        CHECK(!nova::scene::worldBounds(world, bare, center, halfExtent));
    }

    SUBCASE("rotation expands the half extents by the absolute basis") {
        const Entity rotated = world.create();
        const Mat4 spin = nova::math::rotationZ(nova::math::kHalfPi);
        world.add<WorldTransform>(rotated, WorldTransform{spin});
        world.add<Bounds>(rotated, Bounds{Vec3(1.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f)});
        REQUIRE(nova::scene::worldBounds(world, rotated, center, halfExtent));
        // Local +X (length 1) rotates onto world +Y; the 2-unit X extent rotates
        // onto Y as well, so the AABB grows in Y and collapses in X.
        CHECK(center.y == doctest::Approx(1.0f).epsilon(1e-5));
        CHECK(halfExtent.y == doctest::Approx(2.0f).epsilon(1e-5));
        CHECK(halfExtent.x == doctest::Approx(0.0f).epsilon(1e-5));
    }
}

TEST_CASE("cullFrustum keeps what is in front and drops what is not") {
    World world;
    const Entity camera = placeCamera(world, Vec3(0.0f, 0.0f, 0.0f), makePerspective());
    const Entity inFront = placeObject(world, Vec3(0.0f, 0.0f, -10.0f));
    const Entity behind = placeObject(world, Vec3(0.0f, 0.0f, 10.0f));
    const Entity tooFar = placeObject(world, Vec3(0.0f, 0.0f, -500.0f));
    const Entity offAxis = placeObject(world, Vec3(1000.0f, 0.0f, -10.0f));

    std::vector<Entity> visible;
    const CullStats stats = nova::scene::cullFrustum(
        world, Frustum(nova::scene::viewProjectionMatrix(
                   *world.get<Camera>(camera), *world.get<WorldTransform>(camera))),
        visible);

    CHECK(stats.candidates == 5);  // camera included: it has a WorldTransform
    CHECK(stats.visible == 2);     // inFront + the unbounded camera
    CHECK(stats.culled == 3);
    CHECK(stats.unbounded == 1);
    CHECK(containsEntity(visible, inFront));
    CHECK(!containsEntity(visible, behind));
    CHECK(!containsEntity(visible, tooFar));
    CHECK(!containsEntity(visible, offAxis));
}

TEST_CASE("cullForCamera excludes the camera from its own draw list") {
    World world;
    const Entity camera = placeCamera(world, Vec3(0.0f, 0.0f, 0.0f), makePerspective());
    const Entity inFront = placeObject(world, Vec3(0.0f, 0.0f, -10.0f));
    const Entity behind = placeObject(world, Vec3(0.0f, 0.0f, 10.0f));

    std::vector<Entity> visible;
    CullStats stats{};
    REQUIRE(nova::scene::cullForCamera(world, camera, visible, stats));

    CHECK(stats.candidates == 2);
    CHECK(stats.visible == 1);
    CHECK(stats.culled == 1);
    CHECK(stats.unbounded == 0);
    REQUIRE(visible.size() == 1);
    CHECK(visible.front() == inFront);
    CHECK(!containsEntity(visible, camera));
    CHECK(!containsEntity(visible, behind));
}

TEST_CASE("cullForCamera fails for entities that are not cameras") {
    World world;
    const Entity plain = placeObject(world, Vec3(0.0f, 0.0f, -5.0f));
    std::vector<Entity> visible;
    visible.push_back(plain);  // must be cleared even on failure
    CullStats stats{};

    CHECK(!nova::scene::cullForCamera(world, plain, visible, stats));
    CHECK(visible.empty());
    CHECK(stats.candidates == 0);

    const Entity dead = world.create();
    world.destroy(dead);
    CHECK(!nova::scene::cullForCamera(world, dead, visible, stats));
    CHECK(!nova::scene::cullForCamera(world, nova::scene::kNullEntity, visible, stats));
}

TEST_CASE("An orthographic camera culls against its box volume") {
    World world;
    Camera ortho{};
    ortho.projection = ProjectionType::Orthographic;
    ortho.orthoHalfHeight = 5.0f;
    ortho.aspectRatio = 1.0f;  // half width 5
    ortho.nearPlane = 0.1f;
    ortho.farPlane = 100.0f;

    const Entity camera = placeCamera(world, Vec3(0.0f, 0.0f, 0.0f), ortho);
    const Entity inside = placeObject(world, Vec3(4.0f, 4.0f, -10.0f), Vec3(0.5f));
    const Entity outsideX = placeObject(world, Vec3(50.0f, 0.0f, -10.0f), Vec3(0.5f));
    // Distance does not shrink the volume: far away but still inside the box.
    const Entity deepInside = placeObject(world, Vec3(0.0f, 0.0f, -90.0f), Vec3(0.5f));

    std::vector<Entity> visible;
    CullStats stats{};
    REQUIRE(nova::scene::cullForCamera(world, camera, visible, stats));

    CHECK(containsEntity(visible, inside));
    CHECK(containsEntity(visible, deepInside));
    CHECK(!containsEntity(visible, outsideX));
    CHECK(stats.culled == 1);
}

TEST_CASE("A parented camera culls from the accumulated world transform") {
    World world;
    SceneHierarchy hierarchy;

    const Entity rig = world.create();
    world.add<Transform>(rig, Transform::fromPosition(Vec3(0.0f, 0.0f, 10.0f)));

    const Entity cameraEntity = world.create();
    world.add<Transform>(cameraEntity);  // local identity: inherits the rig
    world.add<Camera>(cameraEntity, makePerspective());
    REQUIRE(hierarchy.setParent(world, cameraEntity, rig));

    // In front of the rig (world z = 0 is 10 units down the camera's -Z).
    const Entity visibleObject = placeObject(world, Vec3(0.0f, 0.0f, 0.0f));
    // Behind the rig.
    const Entity behindObject = placeObject(world, Vec3(0.0f, 0.0f, 20.0f));

    hierarchy.update(world);

    std::vector<Entity> visible;
    CullStats stats{};
    REQUIRE(nova::scene::cullForCamera(world, cameraEntity, visible, stats));

    CHECK(containsEntity(visible, visibleObject));
    CHECK(!containsEntity(visible, behindObject));
    // The rig itself has a WorldTransform but no Bounds, so it survives as an
    // unbounded candidate; the camera is excluded from its own list.
    CHECK(stats.candidates == 3);
    CHECK(stats.visible == 2);
    CHECK(stats.culled == 1);
    CHECK(stats.unbounded == 1);
    CHECK(!containsEntity(visible, cameraEntity));
}

TEST_CASE("sortFrontToBack orders a draw list by distance from the camera") {
    World world;
    const Entity far = placeObject(world, Vec3(0.0f, 0.0f, -30.0f));
    const Entity near = placeObject(world, Vec3(0.0f, 0.0f, -2.0f));
    const Entity middle = placeObject(world, Vec3(3.0f, 4.0f, -5.0f));  // distance 5*sqrt(2)

    std::vector<Entity> list{far, near, middle};
    nova::scene::sortFrontToBack(world, Vec3(0.0f, 0.0f, 0.0f), list);

    REQUIRE(list.size() == 3);
    CHECK(list[0] == near);
    CHECK(list[1] == middle);
    CHECK(list[2] == far);

    SUBCASE("sorting an empty or single-element list is safe") {
        std::vector<Entity> empty;
        nova::scene::sortFrontToBack(world, Vec3(0.0f), empty);
        CHECK(empty.empty());
        std::vector<Entity> single{near};
        nova::scene::sortFrontToBack(world, Vec3(0.0f), single);
        CHECK(single.size() == 1);
    }
}

TEST_CASE("Culling a large scene produces consistent statistics") {
    constexpr usize kObjects = 4000;
    constexpr float kHalfExtent = 0.25f;
    constexpr float kNear = 0.1f;
    constexpr float kFar = 1000.0f;

    World world;
    const Entity camera =
        placeCamera(world, Vec3(0.0f, 0.0f, 0.0f), makePerspective(1.0f, kNear, kFar));

    // Classify by the depth slab only, with a margin so no expectation depends
    // on a conservative AABB decision: boxes that straddle the near or far
    // plane go into `ambiguous` and are asserted through the aggregate counts.
    std::vector<Entity> expectedVisible;
    std::vector<Entity> expectedCulled;
    std::vector<Entity> ambiguous;
    for (usize i = 0; i < kObjects; ++i) {
        const float z = static_cast<float>(i) - static_cast<float>(kObjects) / 2.0f;
        const Entity object = placeObject(world, Vec3(0.0f, 0.0f, z), Vec3(kHalfExtent));
        // Camera looks down -Z, so `nearest` is the largest z of the box (the
        // face closest to the camera) and `furthest` the smallest.
        const float nearest = z + kHalfExtent;
        const float furthest = z - kHalfExtent;
        if (nearest <= -0.5f && furthest >= -kFar + 0.5f) {
            // Whole box inside the depth slab, with margin for the side planes.
            expectedVisible.push_back(object);
        } else if (furthest > -kNear || nearest < -kFar) {
            // Whole box behind the near plane, or wholly beyond the far plane.
            expectedCulled.push_back(object);
        } else {
            // Straddles the near or far plane: the AABB test is conservative and
            // keeps these, so they are only asserted through the totals.
            ambiguous.push_back(object);
        }
    }
    REQUIRE(expectedVisible.size() + expectedCulled.size() + ambiguous.size() == kObjects);

    std::vector<Entity> visible;
    CullStats stats{};
    REQUIRE(nova::scene::cullForCamera(world, camera, visible, stats));

    CHECK(stats.candidates == kObjects);
    CHECK(stats.visible + stats.culled == kObjects);
    CHECK(stats.unbounded == 0);
    CHECK(visible.size() == stats.visible);

    usize keptAmbiguous = 0;
    for (const Entity& entity : ambiguous) {
        if (containsEntity(visible, entity)) {
            ++keptAmbiguous;
        }
    }

    // Every object safely inside the slab survives...
    usize keptVisible = 0;
    for (const Entity& entity : expectedVisible) {
        if (containsEntity(visible, entity)) {
            ++keptVisible;
        }
    }
    CHECK(keptVisible == expectedVisible.size());

    // ...and nothing safely outside does.
    for (const Entity& entity : expectedCulled) {
        CHECK(!containsEntity(visible, entity));
    }

    CHECK(stats.visible == keptVisible + keptAmbiguous);
    CHECK(stats.visible > 0);
    CHECK(stats.culled > 0);
}

// Unit tests — the scene -> renderer draw list bridge (M2).
//
// These cover buildDrawCommands() (pure data, no device) and the full scene
// stage the engine runs per frame: hierarchy -> cull -> sort -> draw list.
// The last case pins the engine's camera entity to the M1 reference matrix so
// the on-device image cannot drift when the renderer stops computing the
// view-projection itself.
#include <doctest.h>

#include "renderer/DrawList.h"
#include "renderer/Renderer.h"
#include "scene/Camera.h"
#include "scene/Culling.h"
#include "scene/SceneHierarchy.h"
#include "scene/Transform.h"
#include "scene/World.h"

#include <algorithm>
#include <vector>

namespace {

using namespace nova;
using nova::usize;

/// A drawable object: Transform + Bounds + MeshRef.
scene::Entity makeObject(scene::World& world, const math::Vec3& position,
                         renderer::BuiltinMesh mesh = renderer::BuiltinMesh::kTriangle,
                         const math::Vec4& tint = math::Vec4(1.0f)) {
    const scene::Entity entity = world.create();
    world.add<scene::Transform>(entity, scene::Transform::fromPosition(position));
    world.add<scene::Bounds>(entity, scene::Bounds{math::Vec3(0.0f), math::Vec3(0.5f)});
    renderer::MeshRef meshRef{};
    meshRef.mesh = mesh;
    meshRef.tint = tint;
    world.add<renderer::MeshRef>(entity, meshRef);
    return entity;
}

/// The M1/M2 camera: (0, 0, 3) looking down -Z, 60° vertical FOV, [0.1, 100].
scene::Entity makeCamera(scene::World& world, float aspect = 16.0f / 9.0f) {
    const scene::Entity entity = world.create();
    world.add<scene::Transform>(entity,
                                scene::Transform::fromPosition(math::Vec3(0.0f, 0.0f, 3.0f)));
    scene::Camera camera{};
    camera.fovYDegrees = 60.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;
    camera.aspectRatio = aspect;
    world.add<scene::Camera>(entity, camera);
    return entity;
}

}  // namespace

TEST_CASE("buildDrawCommands returns nothing for an empty visible list") {
    scene::World world;
    std::vector<scene::Entity> visible;
    std::vector<renderer::DrawCommand> out;

    CHECK(renderer::buildDrawCommands(world, visible, out) == 0);
    CHECK(out.empty());
}

TEST_CASE("buildDrawCommands copies the world matrix and tint of a drawable") {
    scene::World world;
    const scene::Entity entity = makeObject(world, math::Vec3(1.0f, 2.0f, 3.0f),
                                            renderer::BuiltinMesh::kTriangle,
                                            math::Vec4(0.5f, 0.25f, 1.0f, 1.0f));
    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);

    std::vector<renderer::DrawCommand> out;
    const usize appended = renderer::buildDrawCommands(world, {entity}, out);

    CHECK(appended == 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].mesh == renderer::BuiltinMesh::kTriangle);
    CHECK(out[0].tint.x == doctest::Approx(0.5f));
    CHECK(out[0].tint.z == doctest::Approx(1.0f));
    // Translation column of the entity's world matrix.
    CHECK(out[0].world[3].x == doctest::Approx(1.0f));
    CHECK(out[0].world[3].y == doctest::Approx(2.0f));
    CHECK(out[0].world[3].z == doctest::Approx(3.0f));
}

TEST_CASE("buildDrawCommands preserves the caller's draw order") {
    scene::World world;
    const scene::Entity far = makeObject(world, math::Vec3(0.0f, 0.0f, -9.0f));
    const scene::Entity near = makeObject(world, math::Vec3(0.0f, 0.0f, -1.0f));
    const scene::Entity mid = makeObject(world, math::Vec3(0.0f, 0.0f, -5.0f));
    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);

    std::vector<renderer::DrawCommand> out;
    const std::vector<scene::Entity> visible{far, near, mid};
    CHECK(renderer::buildDrawCommands(world, visible, out) == 3);
    REQUIRE(out.size() == 3);
    CHECK(out[0].world[3].z == doctest::Approx(-9.0f));
    CHECK(out[1].world[3].z == doctest::Approx(-1.0f));
    CHECK(out[2].world[3].z == doctest::Approx(-5.0f));
}

TEST_CASE("buildDrawCommands skips entities that cannot be drawn") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    const scene::Entity noneMesh = makeObject(world, math::Vec3(0.0f), renderer::BuiltinMesh::kNone);
    const scene::Entity drawable = makeObject(world, math::Vec3(1.0f, 0.0f, 0.0f));

    // A MeshRef whose owner never went through the hierarchy has no world
    // matrix: it must be skipped rather than drawn at the origin.
    const scene::Entity stale = world.create();
    renderer::MeshRef staleMesh{};
    staleMesh.mesh = renderer::BuiltinMesh::kTriangle;
    world.add<renderer::MeshRef>(stale, staleMesh);

    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);

    std::vector<renderer::DrawCommand> out;
    const std::vector<scene::Entity> visible{camera, noneMesh, stale, drawable};
    const usize appended = renderer::buildDrawCommands(world, visible, out);

    CHECK(appended == 1);
    REQUIRE(out.size() == 1);
    CHECK(out[0].world[3].x == doctest::Approx(1.0f));
}

TEST_CASE("buildDrawCommands appends without clearing the caller's vector") {
    scene::World world;
    const scene::Entity first = makeObject(world, math::Vec3(0.0f, 0.0f, -1.0f));
    const scene::Entity second = makeObject(world, math::Vec3(0.0f, 0.0f, -2.0f));
    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);

    std::vector<renderer::DrawCommand> out;
    CHECK(renderer::buildDrawCommands(world, {first}, out) == 1);
    CHECK(renderer::buildDrawCommands(world, {second}, out) == 1);
    REQUIRE(out.size() == 2);
    CHECK(out[0].world[3].z == doctest::Approx(-1.0f));
    CHECK(out[1].world[3].z == doctest::Approx(-2.0f));
}

TEST_CASE("Scene stage turns a world into a culled, sorted draw list") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    const scene::Entity a = makeObject(world, math::Vec3(0.0f, 0.0f, 1.0f));    // distance 2
    const scene::Entity b = makeObject(world, math::Vec3(0.0f, 0.0f, -5.0f));   // distance 8
    const scene::Entity c = makeObject(world, math::Vec3(2.0f, 0.0f, 0.0f));    // distance ~3.6
    const scene::Entity behind = makeObject(world, math::Vec3(0.0f, 0.0f, 20.0f));
    const scene::Entity tooFar = makeObject(world, math::Vec3(0.0f, 0.0f, -500.0f));
    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);
    CHECK(hierarchy.lastStats().transformsWritten == 6);

    std::vector<scene::Entity> visible;
    scene::CullStats stats{};
    REQUIRE(scene::cullForCamera(world, camera, visible, stats));
    CHECK(stats.candidates == 5);   // the camera itself is excluded
    CHECK(stats.visible == 3);
    CHECK(stats.culled == 2);
    CHECK(stats.unbounded == 0);

    const scene::WorldTransform* cameraTransform = world.get<scene::WorldTransform>(camera);
    REQUIRE(cameraTransform != nullptr);
    scene::sortFrontToBack(world, scene::cameraPosition(*cameraTransform), visible);

    std::vector<renderer::DrawCommand> commands;
    CHECK(renderer::buildDrawCommands(world, visible, commands) == 3);
    REQUIRE(commands.size() == 3);

    // Front-to-back: a (2), c (~3.6), b (8).
    CHECK(commands[0].world[3].z == doctest::Approx(1.0f));
    CHECK(commands[1].world[3].x == doctest::Approx(2.0f));
    CHECK(commands[2].world[3].z == doctest::Approx(-5.0f));

    // The culled pair never reached the draw list; the visible trio did.
    const bool keptInside = std::find(visible.begin(), visible.end(), a) != visible.end() &&
                            std::find(visible.begin(), visible.end(), b) != visible.end() &&
                            std::find(visible.begin(), visible.end(), c) != visible.end();
    const bool droppedOutside = std::find(visible.begin(), visible.end(), behind) == visible.end() &&
                                std::find(visible.begin(), visible.end(), tooFar) == visible.end();
    CHECK(keptInside);
    CHECK(droppedOutside);
}

TEST_CASE("Scene stage reproduces the M1 reference view-projection") {
    scene::World world;
    const scene::Entity camera = makeCamera(world, 16.0f / 9.0f);
    makeObject(world, math::Vec3(0.0f));

    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);

    const scene::Camera* cameraComponent = world.get<scene::Camera>(camera);
    const scene::WorldTransform* cameraTransform = world.get<scene::WorldTransform>(camera);
    REQUIRE(cameraComponent != nullptr);
    REQUIRE(cameraTransform != nullptr);

    const math::Mat4 sceneMatrix = scene::viewProjectionMatrix(*cameraComponent, *cameraTransform);
    const math::Mat4 reference = renderer::Renderer::viewProjection(16.0f / 9.0f);

    // The engine's camera entity must produce exactly the matrix the M1
    // renderer hard-coded, or the on-device image would change.
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(sceneMatrix[column][row] ==
                  doctest::Approx(reference[column][row]).epsilon(1e-5));
        }
    }
}

TEST_CASE("Scene stage keeps the hero triangle tumbling like M1 did") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    const scene::Entity hero = makeObject(world, math::Vec3(0.0f));

    scene::SceneHierarchy hierarchy;

    // Engine::updateScene() writes the tumble into the Transform's rotation.
    const float time = 2.5f;
    scene::Transform* transform = world.get<scene::Transform>(hero);
    REQUIRE(transform != nullptr);
    transform->rotation = math::quatRotationY(time * 0.9f) * math::quatRotationZ(time * 0.55f);

    hierarchy.update(world);

    std::vector<scene::Entity> visible;
    scene::CullStats stats{};
    REQUIRE(scene::cullForCamera(world, camera, visible, stats));
    CHECK(stats.visible == 1);

    std::vector<renderer::DrawCommand> commands;
    REQUIRE(renderer::buildDrawCommands(world, visible, commands) == 1);

    // Expected world matrix: rotation-only tumble at the origin (M1 multiplied
    // rotationY * rotationZ onto the view-projection).
    const math::Mat4 expected = math::rotationY(time * 0.9f) * math::rotationZ(time * 0.55f);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            CHECK(commands[0].world[column][row] ==
                  doctest::Approx(expected[column][row]).epsilon(1e-5));
        }
    }
}

TEST_CASE("Scene stage produces an empty draw list when the camera is gone") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    makeObject(world, math::Vec3(0.0f));

    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);
    world.destroy(camera);
    hierarchy.update(world);

    std::vector<scene::Entity> visible;
    scene::CullStats stats{};
    CHECK_FALSE(scene::cullForCamera(world, camera, visible, stats));
    CHECK(visible.empty());

    std::vector<renderer::DrawCommand> commands;
    CHECK(renderer::buildDrawCommands(world, visible, commands) == 0);
    CHECK(commands.empty());
}

TEST_CASE("runSceneStage fills a snapshot straight from the world") {
    scene::World world;
    const scene::Entity camera = makeCamera(world, 4.0f / 3.0f);
    makeObject(world, math::Vec3(0.0f, 0.0f, 1.0f));    // nearest
    makeObject(world, math::Vec3(0.0f, 0.0f, -4.0f));   // farthest
    makeObject(world, math::Vec3(0.0f, 0.0f, 30.0f));   // behind the camera
    scene::SceneHierarchy hierarchy;

    std::vector<scene::Entity> visible;
    std::vector<renderer::DrawCommand> commands;
    renderer::SceneSnapshot snapshot{};
    renderer::SceneStageInput input{};
    input.world = &world;
    input.hierarchy = &hierarchy;
    input.cameraEntity = camera;
    input.timeSeconds = 7.5f;
    input.aspect = 4.0f / 3.0f;

    renderer::SceneStageOutput output{};
    REQUIRE(renderer::runSceneStage(input, visible, commands, snapshot, &output));

    CHECK(snapshot.timeSeconds == doctest::Approx(7.5f));
    CHECK(snapshot.aspect == doctest::Approx(4.0f / 3.0f));
    CHECK(snapshot.commands.size() == 2);
    CHECK(snapshot.commands.data() == commands.data());  // a view, not a copy
    CHECK(snapshot.commands[0].world[3].z == doctest::Approx(1.0f));
    CHECK(snapshot.commands[1].world[3].z == doctest::Approx(-4.0f));

    CHECK(output.commandCount == 2);
    CHECK(output.visibleCount == 2);
    CHECK(output.cull.candidates == 3);
    CHECK(output.cull.visible == 2);
    CHECK(output.cull.culled == 1);

    // The viewport aspect reached the camera component.
    const scene::Camera* cameraComponent = world.get<scene::Camera>(camera);
    REQUIRE(cameraComponent != nullptr);
    CHECK(cameraComponent->aspectRatio == doctest::Approx(4.0f / 3.0f));

    // The view-projection matches the standalone scene camera math.
    const scene::WorldTransform* cameraTransform = world.get<scene::WorldTransform>(camera);
    REQUIRE(cameraTransform != nullptr);
    const math::Mat4 expected = scene::viewProjectionMatrix(*cameraComponent, *cameraTransform);
    CHECK(snapshot.viewProjection[0].x == doctest::Approx(expected[0].x).epsilon(1e-6));
    CHECK(snapshot.viewProjection[1].y == doctest::Approx(expected[1].y).epsilon(1e-6));
}

TEST_CASE("runSceneStage evaluates the hierarchy before culling") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    const scene::Entity root = makeObject(world, math::Vec3(0.0f, 0.0f, 0.0f));
    const scene::Entity child = makeObject(world, math::Vec3(0.0f, 0.0f, -1.0f));
    scene::SceneHierarchy hierarchy;
    hierarchy.setParent(world, child, root);

    std::vector<scene::Entity> visible;
    std::vector<renderer::DrawCommand> commands;
    renderer::SceneSnapshot snapshot{};
    renderer::SceneStageInput input{};
    input.world = &world;
    input.hierarchy = &hierarchy;
    input.cameraEntity = camera;

    REQUIRE(renderer::runSceneStage(input, visible, commands, snapshot));
    REQUIRE(commands.size() == 2);

    // Move the parent after the hierarchy was built: the child must follow, so
    // runSceneStage really re-evaluated world matrices this frame.
    scene::Transform* rootTransform = world.get<scene::Transform>(root);
    REQUIRE(rootTransform != nullptr);
    rootTransform->position = math::Vec3(0.0f, 0.0f, 2.0f);
    REQUIRE(renderer::runSceneStage(input, visible, commands, snapshot));
    REQUIRE(commands.size() == 2);

    // Sorted front-to-back from the camera at z = 3: root (z = 2, distance 1)
    // then child (z = 1, distance 2) — and the child's matrix carries the
    // parent's translation, proving the hierarchy was re-evaluated.
    CHECK(commands[0].world[3].z == doctest::Approx(2.0f));
    CHECK(commands[1].world[3].z == doctest::Approx(1.0f));
}

TEST_CASE("runSceneStage reuses the caller's vectors across frames") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    makeObject(world, math::Vec3(0.0f, 0.0f, 0.0f));
    scene::SceneHierarchy hierarchy;

    std::vector<scene::Entity> visible;
    std::vector<renderer::DrawCommand> commands;
    renderer::SceneSnapshot snapshot{};
    renderer::SceneStageInput input{};
    input.world = &world;
    input.hierarchy = &hierarchy;
    input.cameraEntity = camera;

    for (int frame = 0; frame < 4; ++frame) {
        input.timeSeconds = static_cast<float>(frame) * 0.016f;
        REQUIRE(renderer::runSceneStage(input, visible, commands, snapshot));
        CHECK(commands.size() == 1);
        CHECK(snapshot.commands.size() == 1);
        CHECK(snapshot.timeSeconds == doctest::Approx(input.timeSeconds));
    }
}

TEST_CASE("runSceneStage fails cleanly without a usable camera") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    makeObject(world, math::Vec3(0.0f));
    scene::SceneHierarchy hierarchy;

    std::vector<scene::Entity> visible;
    std::vector<renderer::DrawCommand> commands;
    renderer::SceneSnapshot snapshot{};
    snapshot.timeSeconds = 3.0f;
    renderer::SceneStageInput input{};
    input.world = &world;
    input.hierarchy = &hierarchy;
    input.cameraEntity = scene::kNullEntity;

    renderer::SceneStageOutput output{};
    output.commandCount = 99;
    CHECK_FALSE(renderer::runSceneStage(input, visible, commands, snapshot, &output));
    CHECK(snapshot.commands.empty());
    CHECK(commands.empty());
    CHECK(visible.empty());
    CHECK(snapshot.viewProjection[3].x == doctest::Approx(0.0f));  // identity, not garbage
    CHECK(output.commandCount == 0);

    // A destroyed camera fails the same way instead of drawing everything.
    input.cameraEntity = camera;
    REQUIRE(renderer::runSceneStage(input, visible, commands, snapshot, &output));
    CHECK(output.commandCount == 1);
    world.destroy(camera);
    CHECK_FALSE(renderer::runSceneStage(input, visible, commands, snapshot, &output));
    CHECK(snapshot.commands.empty());
}

TEST_CASE("runSceneStage rejects missing world or hierarchy pointers") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);
    scene::SceneHierarchy hierarchy;

    std::vector<scene::Entity> visible;
    std::vector<renderer::DrawCommand> commands;
    renderer::SceneSnapshot snapshot{};

    renderer::SceneStageInput input{};
    input.cameraEntity = camera;
    CHECK_FALSE(renderer::runSceneStage(input, visible, commands, snapshot));

    input.world = &world;
    CHECK_FALSE(renderer::runSceneStage(input, visible, commands, snapshot));

    input.hierarchy = &hierarchy;
    CHECK(renderer::runSceneStage(input, visible, commands, snapshot));
    CHECK(snapshot.commands.empty());  // nothing drawable in this world
}

TEST_CASE("Scene stage scales to a few thousand drawables") {
    scene::World world;
    const scene::Entity camera = makeCamera(world);

    constexpr usize kObjects = 2000;
    std::vector<scene::Entity> objects;
    objects.reserve(kObjects);
    for (usize index = 0; index < kObjects; ++index) {
        const float z = -static_cast<float>(index % 90);  // 0 .. -89, inside the frustum
        const float x = static_cast<float>((index / 90) % 20) * 0.1f;
        objects.push_back(makeObject(world, math::Vec3(x, 0.0f, z)));
    }

    scene::SceneHierarchy hierarchy;
    hierarchy.update(world);

    std::vector<scene::Entity> visible;
    scene::CullStats stats{};
    REQUIRE(scene::cullForCamera(world, camera, visible, stats));
    CHECK(stats.candidates == kObjects);
    CHECK(stats.visible == visible.size());

    const scene::WorldTransform* cameraTransform = world.get<scene::WorldTransform>(camera);
    REQUIRE(cameraTransform != nullptr);
    scene::sortFrontToBack(world, scene::cameraPosition(*cameraTransform), visible);

    std::vector<renderer::DrawCommand> commands;
    commands.reserve(kObjects);
    CHECK(renderer::buildDrawCommands(world, visible, commands) == stats.visible);
    CHECK(commands.size() == stats.visible);

    // Sorted output: distances never decrease.
    bool sorted = true;
    float previous = -1.0f;
    for (const renderer::DrawCommand& command : commands) {
        const float distance = glm::length(math::Vec3(command.world[3].x, command.world[3].y,
                                                      command.world[3].z) -
                                           math::Vec3(0.0f, 0.0f, 3.0f));
        if (distance < previous) {
            sorted = false;
            break;
        }
        previous = distance;
    }
    CHECK(sorted);
}

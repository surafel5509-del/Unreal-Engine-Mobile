// Nova Engine — scene-driven draw list construction (M2).

#include "renderer/DrawList.h"

#include "core/Log.h"
#include "scene/Transform.h"

namespace nova::renderer {

using core::usize;

usize buildDrawCommands(const scene::World& world, const std::vector<scene::Entity>& visible,
                        std::vector<DrawCommand>& out) {
    usize appended = 0;
    for (const scene::Entity& entity : visible) {
        const MeshRef* meshRef = world.get<MeshRef>(entity);
        if (meshRef == nullptr || meshRef->mesh == BuiltinMesh::kNone) {
            continue;
        }
        const scene::WorldTransform* worldTransform = world.get<scene::WorldTransform>(entity);
        if (worldTransform == nullptr) {
            // A drawable without a world matrix has not been through
            // SceneHierarchy::update() yet; skipping is safer than drawing it
            // at the origin.
            continue;
        }

        DrawCommand command{};
        command.mesh = meshRef->mesh;
        command.world = worldTransform->matrix;
        command.tint = meshRef->tint;
        out.push_back(command);
        ++appended;
    }
    return appended;
}

namespace {

/// Failure path shared by every early return: leave the snapshot with an empty
/// draw list so a caller that ignores the bool cannot render garbage.
bool sceneStageFailed(const char* reason, SceneSnapshot& snapshot,
                      SceneStageOutput* outStats) {
    NOVA_LOG_ERROR("renderer: scene stage aborted — %s", reason);
    snapshot.viewProjection = math::identity();
    snapshot.commands = {};
    if (outStats != nullptr) {
        *outStats = SceneStageOutput{};
    }
    return false;
}

}  // namespace

bool runSceneStage(const SceneStageInput& input, std::vector<scene::Entity>& visible,
                   std::vector<DrawCommand>& commands, SceneSnapshot& snapshot,
                   SceneStageOutput* outStats) {
    commands.clear();
    visible.clear();
    snapshot.commands = {};

    if (input.world == nullptr || input.hierarchy == nullptr) {
        return sceneStageFailed("missing world or hierarchy", snapshot, outStats);
    }
    scene::World& world = *input.world;

    // The viewport owns the aspect ratio: the camera component only stores it.
    if (scene::Camera* camera = world.get<scene::Camera>(input.cameraEntity)) {
        camera->aspectRatio = input.aspect;
    }

    // World matrices must be current before culling reads them.
    input.hierarchy->update(world);

    const scene::Camera* camera = world.get<scene::Camera>(input.cameraEntity);
    const scene::WorldTransform* cameraTransform =
        world.get<scene::WorldTransform>(input.cameraEntity);
    if (camera == nullptr || cameraTransform == nullptr) {
        return sceneStageFailed("camera entity has no Camera/WorldTransform", snapshot, outStats);
    }

    scene::CullStats cull{};
    if (!scene::cullForCamera(world, input.cameraEntity, visible, cull)) {
        return sceneStageFailed("frustum culling failed", snapshot, outStats);
    }
    scene::sortFrontToBack(world, scene::cameraPosition(*cameraTransform), visible);

    const usize commandCount = buildDrawCommands(world, visible, commands);

    snapshot.viewProjection = scene::viewProjectionMatrix(*camera, *cameraTransform);
    snapshot.timeSeconds = input.timeSeconds;
    snapshot.aspect = input.aspect;
    snapshot.commands = commands;

    if (outStats != nullptr) {
        outStats->cull = cull;
        outStats->visibleCount = visible.size();
        outStats->commandCount = commandCount;
    }
    return true;
}

}  // namespace nova::renderer

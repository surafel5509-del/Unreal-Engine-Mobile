#pragma once

// Nova Engine — scene-driven draw list (M2).
//
// This is the seam between the ECS scene and the GPU: the scene stage produces
// a flat, ordered list of DrawCommands plus the camera's view-projection, and
// the renderer turns that into recorded commands. The renderer never touches a
// World, and the scene never touches an rhi resource — meshes are named by a
// BuiltinMesh id that the renderer owns the GPU resources for.
//
// Dependency direction: renderer -> scene -> core (never the reverse).
//
// Thread-safety: plain data plus a free function; run on the simulation/render
// thread that owns the World.

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Camera.h"
#include "scene/Culling.h"
#include "scene/Entity.h"
#include "scene/SceneHierarchy.h"
#include "scene/World.h"

#include <span>
#include <vector>

namespace nova::renderer {

/// Meshes the renderer owns GPU resources for. M1/M2 ship exactly one; glTF
/// meshes (M2) will add a resource-handle based path alongside this.
enum class BuiltinMesh : core::u8 {
    kNone = 0,
    kTriangle = 1,
};

/// Component marking an entity as drawable with a renderer-owned mesh.
///
/// Entities without it are simulated but never drawn (cameras, lights, logic).
struct MeshRef {
    BuiltinMesh mesh = BuiltinMesh::kNone;
    math::Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

/// One object to draw, already in draw order.
struct DrawCommand {
    BuiltinMesh mesh = BuiltinMesh::kNone;
    math::Mat4 world = math::identity();
    math::Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
};

/// Everything the renderer needs for one frame, produced by the scene stage.
///
/// `commands` is a non-owning view: the caller keeps the vector alive for the
/// duration of the render call (Engine reuses one vector across frames, so the
/// frame loop stays allocation-free).
struct SceneSnapshot {
    /// Camera view-projection in Vulkan clip space (Y flipped, depth [0, 1]).
    math::Mat4 viewProjection = math::identity();

    /// Seconds since engine start; drives time-based shading.
    core::f32 timeSeconds = 0.0f;

    /// Viewport width / height; 0 or negative lets the renderer fall back to
    /// the frame dimensions it was given.
    core::f32 aspect = 1.0f;

    /// Draw commands in order (front-to-back for opaque geometry).
    std::span<const DrawCommand> commands;
};

/// Append a DrawCommand for every entity in `visible` that owns both a MeshRef
/// (with a real mesh) and a WorldTransform. Returns the number appended.
///
/// Order is preserved, so callers control draw order by sorting `visible`
/// first (see scene::sortFrontToBack).
core::usize buildDrawCommands(const scene::World& world,
                              const std::vector<scene::Entity>& visible,
                              std::vector<DrawCommand>& out);

/// Inputs for one frame of scene work. All pointers must stay valid for the
/// call; `world` and `hierarchy` are borrowed, never owned.
struct SceneStageInput {
    scene::World* world = nullptr;
    scene::SceneHierarchy* hierarchy = nullptr;
    /// Entity owning the `Camera` used for culling and the view-projection.
    scene::Entity cameraEntity = scene::kNullEntity;
    /// Written into the snapshot; drives time-based shading.
    core::f32 timeSeconds = 0.0f;
    /// Viewport width / height; also written into the camera's `aspectRatio`.
    core::f32 aspect = 1.0f;
};

/// Diagnostics for one scene stage run (mirrors the `stats` console output).
struct SceneStageOutput {
    scene::CullStats cull{};
    core::usize visibleCount = 0;
    core::usize commandCount = 0;
};

/// One frame of scene work: aspect → hierarchy → cull → sort → draw list.
///
/// Fills `snapshot` (view-projection, time, aspect and a span over `commands`)
/// so it can be handed straight to Renderer::render(). `visible` and `commands`
/// are cleared and reused by the caller, which keeps the frame loop
/// allocation-free once the vectors have warmed up.
///
/// Returns false — with an empty draw list — when the camera entity has no
/// `Camera`/`WorldTransform` or when culling could not run; the caller should
/// skip the frame rather than render with an identity view-projection.
bool runSceneStage(const SceneStageInput& input, std::vector<scene::Entity>& visible,
                   std::vector<DrawCommand>& commands, SceneSnapshot& snapshot,
                   SceneStageOutput* outStats = nullptr);

}  // namespace nova::renderer

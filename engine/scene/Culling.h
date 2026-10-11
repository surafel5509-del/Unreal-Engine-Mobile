#pragma once

// Nova Engine — frustum culling and draw-list ordering (M2).
//
// Consumes WorldTransform (from SceneHierarchy) plus an optional Bounds
// component, and produces a list of visible entities for a camera. The output
// is the input of the renderer's draw-list stage: cull, then order.
//
// Policy decisions:
//   * Entities WITHOUT Bounds are kept (counted in CullStats::unbounded). Not
//     knowing an object's extent is not a reason to drop it; lights, cameras
//     and logical nodes fall into this bucket.
//   * Tests are conservative toward keeping geometry (see scene/Frustum.h).
//   * The camera entity itself is excluded from its own draw list.
//
// Thread-safety: not thread-safe; run on the simulation/render thread that
// owns the World, after SceneHierarchy::update() has refreshed WorldTransform.

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Camera.h"
#include "scene/Entity.h"
#include "scene/Frustum.h"
#include "scene/World.h"

#include <type_traits>
#include <vector>

namespace nova::scene {

/// Local-space axis-aligned bounds used for culling.
///
/// The world-space AABB is derived per frame from the entity's WorldTransform,
/// expanding the half extents by the absolute basis vectors so rotation and
/// scale are handled without storing a second AABB.
struct Bounds {
    math::Vec3 center{0.0f, 0.0f, 0.0f};
    math::Vec3 halfExtent{0.5f, 0.5f, 0.5f};
};

static_assert(std::is_trivially_copyable_v<Bounds>,
              "Bounds must stay trivially copyable for GPU upload");

/// Diagnostics for one culling pass.
struct CullStats {
    usize candidates = 0;  ///< entities owning a WorldTransform
    usize visible = 0;     ///< survivors written to the output list
    usize culled = 0;      ///< rejected by the frustum
    usize unbounded = 0;   ///< kept only because they have no Bounds
};

/// World-space AABB of an entity's Bounds.
/// Returns false when the entity has no WorldTransform or no Bounds.
bool worldBounds(World& world, Entity entity, math::Vec3& outCenter, math::Vec3& outHalfExtent);

/// Cull every entity owning a WorldTransform against `frustum`.
///
/// `outVisible` is cleared first and receives the survivors in unspecified
/// (sparse-set) order — call sortFrontToBack() when order matters. `exclude`
/// is skipped, which is how a camera keeps itself out of its own draw list.
CullStats cullFrustum(World& world, const Frustum& frustum, std::vector<Entity>& outVisible,
                      Entity exclude = kNullEntity);

/// Cull using the frustum of a camera entity (Camera + WorldTransform).
/// Returns false — leaving `outVisible` cleared — when the entity is not a
/// usable camera.
bool cullForCamera(World& world, Entity cameraEntity, std::vector<Entity>& outVisible,
                   CullStats& outStats);

/// Order a draw list front-to-back by squared distance from `origin`.
///
/// Front-to-back maximizes early-Z rejection for opaque geometry. The sort is
/// not stable (std::sort), so entities at identical distances may swap between
/// frames; transparent lists must be sorted back-to-front by the frame graph
/// instead, using an explicit key.
void sortFrontToBack(World& world, const math::Vec3& origin, std::vector<Entity>& entities);

}  // namespace nova::scene

#pragma once

// Nova Engine — camera component (M2).
//
// A Camera holds only the *intrinsic* parameters of a view volume. Its
// placement comes from the entity's WorldTransform, so cameras can be parented
// to anything in the scene graph (a head bone, a vehicle, a crane rig) and the
// hierarchy does the work.
//
// Projections use the Vulkan conventions established in core/Math.h: depth
// range [0, 1] and a flipped Y so Y-up world content renders upright.
//
// Thread-safety: plain data plus free functions; no shared state.

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Transform.h"

namespace nova::scene {

using core::f32;
using core::i32;

/// Projection model used by a Camera.
enum class ProjectionType : core::u8 {
    Perspective = 0,
    Orthographic = 1,
};

/// Intrinsic camera parameters.
///
/// `aspectRatio` is owned by the renderer: it is written from the current
/// surface size each frame (see Renderer's FrameUBO), never authored by hand.
struct Camera {
    ProjectionType projection = ProjectionType::Perspective;

    /// Vertical field of view in degrees (perspective only).
    f32 fovYDegrees = 60.0f;

    /// Half height of the view volume in world units (orthographic only). The
    /// half width is derived from aspectRatio.
    f32 orthoHalfHeight = 5.0f;

    /// Width / height of the target surface.
    f32 aspectRatio = 16.0f / 9.0f;

    f32 nearPlane = 0.1f;
    f32 farPlane = 100.0f;

    /// Cameras with a higher priority render later (frame graph, M2).
    i32 priority = 0;
};

/// Projection matrix for `camera` in Vulkan clip space.
[[nodiscard]] inline math::Mat4 projectionMatrix(const Camera& camera) {
    switch (camera.projection) {
        case ProjectionType::Orthographic: {
            const f32 halfHeight = camera.orthoHalfHeight;
            const f32 halfWidth = halfHeight * camera.aspectRatio;
            return math::orthographicVk(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                        camera.nearPlane, camera.farPlane);
        }
        case ProjectionType::Perspective:
        default:
            return math::perspectiveVk(math::toRadians(camera.fovYDegrees), camera.aspectRatio,
                                       camera.nearPlane, camera.farPlane);
    }
}

/// View matrix for a camera placed by `worldTransform` (its inverse).
[[nodiscard]] inline math::Mat4 viewMatrix(const WorldTransform& worldTransform) {
    return math::inverse(worldTransform.matrix);
}

/// Combined view-projection, ready for a uniform buffer or frustum extraction.
[[nodiscard]] inline math::Mat4 viewProjectionMatrix(const Camera& camera,
                                                     const WorldTransform& worldTransform) {
    return projectionMatrix(camera) * viewMatrix(worldTransform);
}

/// World-space eye position of a camera placed by `worldTransform`.
[[nodiscard]] inline math::Vec3 cameraPosition(const WorldTransform& worldTransform) {
    return worldTransform.position();
}

static_assert(std::is_trivially_copyable_v<Camera>,
              "Camera must stay trivially copyable for GPU upload");

}  // namespace nova::scene

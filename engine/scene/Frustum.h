#pragma once

// Nova Engine — view frustum and intersection tests (M2 culling).
//
// Planes are extracted from a view-projection matrix with the Gribb-Hartmann
// method and normalized, so a point test is one dot product per plane.
//
// Convention: a point `p` is INSIDE the frustum when
//     dot(plane.xyz, p) + plane.w >= 0
// for all six planes, i.e. the stored normals point inward. Sphere and AABB
// tests are conservative in the "keep it" direction: anything that may touch
// the frustum is reported as intersecting, so culling never drops visible
// geometry (it may keep a little extra, which is the correct trade on a GPU).
//
// The Vulkan Y flip applied by core/Math.h mirrors which extracted plane is
// named top vs bottom; the inside/outside semantics are unaffected, so no test
// or caller may depend on the names beyond near/far ordering.
//
// Thread-safety: value type, no shared state.

#include "core/Math.h"
#include "core/Types.h"

namespace nova::scene {

using core::f32;

/// Six inward-pointing clip planes of a view volume.
class Frustum {
public:
    /// Plane slots. kNear/kFar bracket the depth range [nearPlane, farPlane].
    /// Extraction assumes Vulkan's [0, 1] clip depth (see Frustum.cpp); the
    /// classic [-1, 1] near-plane formula would put the near plane at half the
    /// intended distance.
    enum Plane : core::u8 {
        kLeft = 0,
        kRight = 1,
        kBottom = 2,
        kTop = 3,
        kNear = 4,
        kFar = 5,
        kPlaneCount = 6,
    };

    Frustum() = default;

    /// Extract and normalize the planes of a view-projection matrix.
    explicit Frustum(const math::Mat4& viewProjection);

    /// Plane `index` as (normal.xyz, distance.w).
    [[nodiscard]] const math::Vec4& plane(Plane index) const { return m_planes[index]; }

    /// Signed distance to a plane; >= 0 means the point is on the inside.
    [[nodiscard]] f32 distanceToPlane(Plane index, const math::Vec3& point) const {
        const math::Vec4& planeVector = m_planes[index];
        return glm::dot(math::Vec3(planeVector), point) + planeVector.w;
    }

    /// Exact point containment.
    [[nodiscard]] bool contains(const math::Vec3& point) const {
        for (int i = 0; i < kPlaneCount; ++i) {
            if (distanceToPlane(static_cast<Plane>(i), point) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    /// Conservative sphere test.
    [[nodiscard]] bool intersectsSphere(const math::Vec3& center, f32 radius) const {
        for (int i = 0; i < kPlaneCount; ++i) {
            if (distanceToPlane(static_cast<Plane>(i), center) < -radius) {
                return false;
            }
        }
        return true;
    }

    /// Conservative AABB test using the positive vertex per plane.
    [[nodiscard]] bool intersectsAabb(const math::Vec3& center, const math::Vec3& halfExtent) const;

private:
    math::Vec4 m_planes[kPlaneCount] = {};
};

/// Free-function spelling of the constructor, for symmetry with the rest of the
/// scene API.
[[nodiscard]] inline Frustum frustumFromViewProjection(const math::Mat4& viewProjection) {
    return Frustum(viewProjection);
}

}  // namespace nova::scene

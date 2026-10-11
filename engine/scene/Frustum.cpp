// Nova Engine — frustum plane extraction and AABB test (M2 culling).

#include "scene/Frustum.h"

#include "core/Assert.h"

#include <cmath>

namespace nova::scene {

namespace {

/// Row `index` of a column-major GLM matrix, as a plane-coefficient vector.
math::Vec4 matrixRow(const math::Mat4& matrix, int index) {
    return math::Vec4(matrix[0][index], matrix[1][index], matrix[2][index], matrix[3][index]);
}

/// Normalize so that distanceToPlane returns a true world-space distance.
math::Vec4 normalizePlane(const math::Vec4& plane) {
    const float length = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    if (length <= 0.0f) {
        NOVA_LOG_ERROR("scene: degenerate frustum plane (zero-length normal)");
        return plane;
    }
    return plane * (1.0f / length);
}

}  // namespace

Frustum::Frustum(const math::Mat4& viewProjection) {
    // Gribb-Hartmann: combine the matrix rows that describe the clip-space
    // half-spaces. Depth is [0, 1] in Vulkan (GLM_FORCE_DEPTH_ZERO_TO_ONE), so
    // the near plane is clip.z >= 0 — row2 alone — and NOT the classic
    // [-1, 1] form row3 + row2. The far plane is clip.z <= clip.w.
    const math::Vec4 row0 = matrixRow(viewProjection, 0);
    const math::Vec4 row1 = matrixRow(viewProjection, 1);
    const math::Vec4 row2 = matrixRow(viewProjection, 2);
    const math::Vec4 row3 = matrixRow(viewProjection, 3);

    m_planes[kLeft] = normalizePlane(row3 + row0);
    m_planes[kRight] = normalizePlane(row3 - row0);
    m_planes[kBottom] = normalizePlane(row3 + row1);
    m_planes[kTop] = normalizePlane(row3 - row1);
    m_planes[kNear] = normalizePlane(row2);
    m_planes[kFar] = normalizePlane(row3 - row2);
}

bool Frustum::intersectsAabb(const math::Vec3& center, const math::Vec3& halfExtent) const {
    for (int i = 0; i < kPlaneCount; ++i) {
        const math::Vec4& planeVector = m_planes[i];
        const math::Vec3 normal(planeVector);
        // Positive vertex: the box corner furthest along the plane normal. If
        // even that corner is outside, the whole box is outside.
        const math::Vec3 positiveVertex(
            center.x + (normal.x >= 0.0f ? halfExtent.x : -halfExtent.x),
            center.y + (normal.y >= 0.0f ? halfExtent.y : -halfExtent.y),
            center.z + (normal.z >= 0.0f ? halfExtent.z : -halfExtent.z));
        if (glm::dot(normal, positiveVertex) + planeVector.w < 0.0f) {
            return false;
        }
    }
    return true;
}

}  // namespace nova::scene

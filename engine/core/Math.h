#pragma once

// Nova Engine — math types.
//
// GLM is the underlying implementation (SIMD-friendly, header-only); the
// nova::math namespace provides the engine-wide spelling of common helpers
// plus Vulkan-specific conventions:
//   * radians everywhere,
//   * depth range [0, 1] (VK_FORMAT depth buffers),
//   * Y is flipped in the projection matrix so classic "Y-up" world content
//     renders upright with a positive-height viewport.
//
// Hot paths (skinners, culling) additionally use NEON intrinsics behind
// these types; see milestone M2+.

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace nova::math {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 2.0f * kPi;
inline constexpr float kHalfPi = 0.5f * kPi;

constexpr float toRadians(float degrees) { return degrees * (kPi / 180.0f); }
constexpr float toDegrees(float radians) { return radians * (180.0f / kPi); }

inline Mat4 identity() { return Mat4(1.0f); }

/// Rotation about X/Y/Z (radians).
inline Mat4 rotationX(float radians) { return glm::rotate(Mat4(1.0f), radians, Vec3(1, 0, 0)); }
inline Mat4 rotationY(float radians) { return glm::rotate(Mat4(1.0f), radians, Vec3(0, 1, 0)); }
inline Mat4 rotationZ(float radians) { return glm::rotate(Mat4(1.0f), radians, Vec3(0, 0, 1)); }

/// Right-handed perspective projection with Vulkan depth range [0, 1] and
/// the Y axis flipped (GLM produces Y-up clip space; Vulkan renders Y-down).
inline Mat4 perspectiveVk(float fovYRadians, float aspect, float zNear, float zFar) {
    Mat4 proj = glm::perspective(fovYRadians, aspect, zNear, zFar);
    proj[1][1] = -proj[1][1];
    return proj;
}

/// Right-handed orthographic projection with Vulkan depth range [0, 1] and the
/// Y axis flipped, matching perspectiveVk.
inline Mat4 orthographicVk(float left, float right, float bottom, float top, float zNear,
                          float zFar) {
    Mat4 proj = glm::ortho(left, right, bottom, top, zNear, zFar);
    proj[1][1] = -proj[1][1];
    return proj;
}

/// Right-handed look-at view matrix (Y-up world).
inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    return glm::lookAt(eye, center, up);
}

/// Inverse of a world matrix — the view matrix of a camera placed by it.
inline Mat4 inverse(const Mat4& matrix) { return glm::inverse(matrix); }

/// Uniform scale.
inline Mat4 scale(const Vec3& s) { return glm::scale(Mat4(1.0f), s); }

/// Pure translation.
inline Mat4 translation(const Vec3& t) { return glm::translate(Mat4(1.0f), t); }

/// Quaternion helpers (blend trees in milestone M6 build on these).
inline Quat quatRotationX(float radians) { return glm::angleAxis(radians, Vec3(1, 0, 0)); }
inline Quat quatRotationY(float radians) { return glm::angleAxis(radians, Vec3(0, 1, 0)); }
inline Quat quatRotationZ(float radians) { return glm::angleAxis(radians, Vec3(0, 0, 1)); }
inline Mat4 quatToMat4(const Quat& q) { return glm::mat4_cast(q); }

/// Translation * rotation * scale — the standard local-transform composition
/// used by nova::scene::Transform::localMatrix().
inline Mat4 composeTRS(const Vec3& t, const Quat& r, const Vec3& s) {
    return translation(t) * quatToMat4(r) * scale(s);
}

}  // namespace nova::math

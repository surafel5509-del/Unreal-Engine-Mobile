// Unit tests — nova::math (GLM wrapper + Vulkan conventions).
#include <doctest.h>

#include "core/Math.h"

#include <cmath>

using namespace nova::math;

namespace {

bool near(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a - b) <= epsilon;
}

/// Transform a point by a 4x4 matrix (GLM requires vec4).
Vec3 xf(const Mat4& matrix, const Vec3& point) {
    const Vec4 clip = matrix * Vec4(point, 1.0f);
    return Vec3(clip.x, clip.y, clip.z);
}

}  // namespace

TEST_CASE("rotationZ maps +X to +Y at 90 degrees") {
    const Mat4 rotation = rotationZ(toRadians(90.0f));
    const Vec3 rotated = xf(rotation, Vec3(1.0f, 0.0f, 0.0f));
    CHECK(near(rotated.x, 0.0f));
    CHECK(near(rotated.y, 1.0f));
    CHECK(near(rotated.z, 0.0f));
}

TEST_CASE("rotationY maps +Z to +X at 90 degrees") {
    const Mat4 rotation = rotationY(toRadians(90.0f));
    const Vec3 rotated = xf(rotation, Vec3(0.0f, 0.0f, 1.0f));
    CHECK(near(rotated.x, 1.0f));
    CHECK(near(rotated.y, 0.0f));
    CHECK(near(rotated.z, 0.0f));
}

TEST_CASE("rotationX maps +Y to +Z at 90 degrees") {
    const Mat4 rotation = rotationX(toRadians(90.0f));
    const Vec3 rotated = xf(rotation, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(near(rotated.x, 0.0f));
    CHECK(near(rotated.y, 0.0f));
    CHECK(near(rotated.z, 1.0f));
}

TEST_CASE("perspectiveVk flips Y for Vulkan clip space and keeps depth 0..1") {
    const Mat4 projection = perspectiveVk(toRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    // GLM produces GL conventions; nova::math flips the Y row.
    CHECK(projection[1][1] < 0.0f);

    // A point straight ahead of the eye must project to the clip centre
    // (x=0, y=0) with positive W (visible).
    const Vec4 clip = projection * Vec4(0.0f, 0.0f, -3.0f, 1.0f);
    CHECK(clip.w > 0.0f);
    CHECK(near(clip.x, 0.0f));
    CHECK(near(clip.y, 0.0f));

    // Depth range: near plane maps to 0, far plane to 1.
    const Vec4 nearClip = projection * Vec4(0.0f, 0.0f, -0.1f, 1.0f);
    const Vec4 farClip = projection * Vec4(0.0f, 0.0f, -100.0f, 1.0f);
    CHECK(near(nearClip.z / nearClip.w, 0.0f, 1e-3f));
    CHECK(near(farClip.z / farClip.w, 1.0f, 1e-3f));
}

TEST_CASE("lookAt places the target in front of the camera") {
    const Mat4 view = lookAt(Vec3(0.0f, 0.0f, 3.0f), Vec3(0.0f, 0.0f, 0.0f),
                             Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 originInView = view * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(near(originInView.x, 0.0f));
    CHECK(near(originInView.y, 0.0f));
    CHECK(near(originInView.z, -3.0f));  // 3 units ahead of a RH camera
}

TEST_CASE("quaternion helpers match matrix rotations") {
    const Quat quarterTurn = quatRotationZ(toRadians(90.0f));
    const Vec3 viaQuat = xf(quatToMat4(quarterTurn), Vec3(1.0f, 0.0f, 0.0f));
    CHECK(near(viaQuat.x, 0.0f));
    CHECK(near(viaQuat.y, 1.0f));
}

TEST_CASE("angle conversions round-trip") {
    CHECK(near(toDegrees(toRadians(123.0f)), 123.0f));
    CHECK(near(toRadians(toDegrees(1.5f)), 1.5f));
    CHECK(near(kPi, 3.14159265f));
}

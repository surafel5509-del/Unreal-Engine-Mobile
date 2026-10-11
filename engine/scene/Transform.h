#pragma once

// Nova Engine — transform components (M2).
//
// Three components describe where an entity is:
//   Transform       local translation/rotation/scale, authored by gameplay
//   Parent          link to the parent entity (null handle == root)
//   WorldTransform  cached world-space matrix, written by SceneHierarchy
//
// All three are trivially copyable POD, so they can be uploaded to the GPU or
// memcpy'd into a skinning buffer without per-element handling.
//
// Thread-safety: plain data; the owning World and SceneHierarchy are
// single-threaded (see scene/World.h).

#include "core/Math.h"
#include "core/Types.h"
#include "scene/Entity.h"

#include <type_traits>

namespace nova::scene {

/// Local transform: translation, rotation and (non-uniform) scale.
///
/// Composition order is T * R * S, matching glTF and UE: a vertex is scaled,
/// then rotated, then translated into the parent's space.
struct Transform {
    math::Vec3 position{0.0f, 0.0f, 0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    math::Vec3 scale{1.0f, 1.0f, 1.0f};

    /// Matrix mapping local space into the parent's space.
    [[nodiscard]] math::Mat4 localMatrix() const {
        return math::composeTRS(position, rotation, scale);
    }

    /// Transform placed at `position`, no rotation, unit scale.
    [[nodiscard]] static Transform fromPosition(const math::Vec3& p) {
        Transform out{};
        out.position = p;
        return out;
    }

    /// Transform with a rotation about an arbitrary axis (radians).
    [[nodiscard]] static Transform fromAxisAngle(const math::Vec3& axis, float radians) {
        Transform out{};
        out.rotation = glm::angleAxis(radians, axis);
        return out;
    }
};

/// Parent link. A null handle marks the entity as a root of the hierarchy.
///
/// Set through SceneHierarchy::setParent so cycles are rejected; writing this
/// component directly is allowed but bypasses that check (SceneHierarchy::update
/// then reports the cycle instead of hanging).
struct Parent {
    Entity entity = kNullEntity;
};

/// World-space matrix cache produced by SceneHierarchy::update().
struct WorldTransform {
    math::Mat4 matrix = math::identity();

    /// Convenience accessor for the translation column.
    [[nodiscard]] math::Vec3 position() const { return math::Vec3(matrix[3]); }
};

static_assert(std::is_trivially_copyable_v<Transform>,
              "Transform must stay trivially copyable for GPU upload");
static_assert(std::is_trivially_copyable_v<Parent>,
              "Parent must stay trivially copyable for GPU upload");
static_assert(std::is_trivially_copyable_v<WorldTransform>,
              "WorldTransform must stay trivially copyable for GPU upload");

}  // namespace nova::scene

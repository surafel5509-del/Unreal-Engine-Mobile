// Nova Engine — frustum culling and draw-list ordering (M2).

#include "scene/Culling.h"

#include "core/Log.h"
#include "scene/Transform.h"
#include "scene/View.h"

#include <algorithm>
#include <tuple>

namespace nova::scene {

using core::usize;

bool worldBounds(World& world, Entity entity, math::Vec3& outCenter, math::Vec3& outHalfExtent) {
    const WorldTransform* worldTransform = world.get<WorldTransform>(entity);
    const Bounds* bounds = world.get<Bounds>(entity);
    if (worldTransform == nullptr || bounds == nullptr) {
        return false;
    }

    const math::Mat4& matrix = worldTransform->matrix;
    outCenter = math::Vec3(matrix * math::Vec4(bounds->center, 1.0f));

    // Expand the local half extents by the absolute basis vectors: exact for
    // rotation and scale, and cheap enough to do per frame (M2). A tighter OBB
    // test lands with the frame graph if profiling asks for it.
    const math::Vec3 axisX(matrix[0]);
    const math::Vec3 axisY(matrix[1]);
    const math::Vec3 axisZ(matrix[2]);
    outHalfExtent = glm::abs(axisX) * bounds->halfExtent.x +
                    glm::abs(axisY) * bounds->halfExtent.y +
                    glm::abs(axisZ) * bounds->halfExtent.z;
    return true;
}

CullStats cullFrustum(World& world, const Frustum& frustum, std::vector<Entity>& outVisible,
                      Entity exclude) {
    outVisible.clear();
    CullStats stats{};

    for (auto entry : world.view<WorldTransform>()) {
        const Entity entity = std::get<Entity>(entry);
        if (entity == exclude) {
            continue;
        }
        ++stats.candidates;

        math::Vec3 center(0.0f);
        math::Vec3 halfExtent(0.0f);
        if (!worldBounds(world, entity, center, halfExtent)) {
            // No extent known: keep it, and say so in the diagnostics.
            ++stats.unbounded;
            ++stats.visible;
            outVisible.push_back(entity);
            continue;
        }

        if (frustum.intersectsAabb(center, halfExtent)) {
            ++stats.visible;
            outVisible.push_back(entity);
        } else {
            ++stats.culled;
        }
    }

    return stats;
}

bool cullForCamera(World& world, Entity cameraEntity, std::vector<Entity>& outVisible,
                   CullStats& outStats) {
    outVisible.clear();
    outStats = CullStats{};

    const Camera* camera = world.get<Camera>(cameraEntity);
    const WorldTransform* cameraTransform = world.get<WorldTransform>(cameraEntity);
    if (camera == nullptr || cameraTransform == nullptr) {
        NOVA_LOG_ERROR("scene: cullForCamera on entity %u without a Camera/WorldTransform",
                       cameraEntity.index);
        return false;
    }

    const Frustum frustum(viewProjectionMatrix(*camera, *cameraTransform));
    outStats = cullFrustum(world, frustum, outVisible, cameraEntity);
    return true;
}

void sortFrontToBack(World& world, const math::Vec3& origin, std::vector<Entity>& entities) {
    // Squared distance keeps the sort allocation-free (std::sort, no temporary
    // buffer unlike std::stable_sort) and monotonic with distance.
    std::sort(entities.begin(), entities.end(),
              [&world, &origin](const Entity& lhs, const Entity& rhs) {
                  const WorldTransform* left = world.get<WorldTransform>(lhs);
                  const WorldTransform* right = world.get<WorldTransform>(rhs);
                  if (left == nullptr) {
                      return false;  // entities without a transform sort last
                  }
                  if (right == nullptr) {
                      return true;
                  }
                  const math::Vec3 leftDelta = left->position() - origin;
                  const math::Vec3 rightDelta = right->position() - origin;
                  return glm::dot(leftDelta, leftDelta) < glm::dot(rightDelta, rightDelta);
              });
}

}  // namespace nova::scene

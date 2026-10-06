#include "core/ScenePicking.hpp"

#include <algorithm>
#include <cmath>

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "core/Mesh.hpp"
#include "core/SceneSpatialIndex.hpp"

namespace engine::core {

bool rayIntersectsAabb(glm::vec3 origin, glm::vec3 end, glm::vec3 boundsMin, glm::vec3 boundsMax, float& outT) {
    glm::vec3 dir = end - origin;
    float tmin = 0.0f;
    float tmax = 1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dir[axis]) < 1e-8f) {
            if (origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis]) return false;
            continue;
        }
        float invDir = 1.0f / dir[axis];
        float t1 = (boundsMin[axis] - origin[axis]) * invDir;
        float t2 = (boundsMax[axis] - origin[axis]) * invDir;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    outT = tmin;
    return true;
}

ScenePickResult pickEntity(ECS& ecs, MeshLibrary& meshLibrary, glm::vec3 origin, glm::vec3 direction,
                            float maxDistance, EntityId excludeEntity) {
    ScenePickResult result;
    float dirLen = glm::length(direction);
    if (dirLen < 1e-6f || maxDistance <= 0.0f) return result;

    glm::vec3 unitDir = direction / dirLen;
    glm::vec3 worldEnd = origin + unitDir * maxDistance;

    float closestT = 1.0f; // t in [0,1] along origin->worldEnd; 1.0 = "nothing closer than maxDistance yet"
    auto testEntity = [&](EntityId entity, const glm::mat4& world) {
        const Renderable* renderable = ecs.tryGetComponent<Renderable>(entity);
        if (renderable == nullptr || !renderable->visible) return;
        const Mesh* mesh = meshLibrary.get(renderable->meshHandle);
        if (mesh == nullptr) return;

        glm::mat4 invModel = glm::inverse(world);
        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(origin, 1.0f));
        glm::vec3 localEnd = glm::vec3(invModel * glm::vec4(worldEnd, 1.0f));

        float t = 0.0f;
        if (rayIntersectsAabb(localOrigin, localEnd, mesh->localBoundsMin(), mesh->localBoundsMax(), t) && t < closestT) {
            closestT = t;
            result.hit = true;
            result.entity = entity;
            result.distance = t * maxDistance;
            result.point = origin + unitDir * result.distance;
        }
    };

    if (auto* index = ecs.raw().ctx().find<SceneSpatialIndex>()) {
        index->sync(ecs, meshLibrary);
        index->raycast(origin, unitDir, maxDistance, [&](EntityId entity, float) {
            if (entity != excludeEntity) {
                if (const glm::mat4* world = index->worldMatrix(entity)) testEntity(entity, *world);
            }
            return closestT * maxDistance;
        });
        return result;
    }

    auto view = ecs.view<Transform, Renderable>();
    for (auto entity : view) {
        if (entity == excludeEntity) continue;
        testEntity(entity, hierarchy::computeWorldMatrix(ecs, entity));
    }
    return result;
}

} // namespace engine::core

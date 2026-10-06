#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <glm/glm.hpp>

#include "core/Bvh.hpp"
#include "core/ECS.hpp"

namespace engine::core {

class MeshLibrary;

struct SpatialIndexStats {
    size_t objects = 0;
    size_t moved = 0;       // leaves reinserted by the last sync
    size_t inserted = 0;
    size_t removed = 0;
    size_t visible = 0;     // results of the last cull
    size_t culled = 0;
    int treeHeight = 0;
};

// BVH over every visible Transform + Renderable entity's world-space bounds.
// sync() is incremental: an unparented entity whose transform, mesh and
// mesh bounds haven't changed costs a few compares, and a moved one only
// touches the tree when it leaves its fat box. The world matrices computed
// by sync() are kept, so the renderer doesn't compute them twice.
//
// Studio and the player keep one in the registry context
// (`ecs.raw().ctx()`), which is how core::pickEntity finds it.
class SceneSpatialIndex {
public:
    void sync(ECS& ecs, const MeshLibrary& meshes);
    // Same, with mesh bounds from `bounds(meshHandle, min, max)`; false skips the entity.
    using MeshBoundsFn = std::function<bool(uint32_t meshHandle, glm::vec3& min, glm::vec3& max)>;
    void sync(ECS& ecs, const MeshBoundsFn& bounds);

    // Entities whose bounds touch the frustum of `viewProj`, appended to `out`.
    void cull(const glm::mat4& viewProj, std::vector<EntityId>& out);
    [[nodiscard]] bool wasVisible(EntityId entity) const;

    // visit(entity, tEnterBounds) -> float new max distance, or < 0 to stop.
    template <typename Visit>
    void raycast(const glm::vec3& origin, const glm::vec3& unitDirection, float maxDistance, Visit&& visit) const {
        tree_.raycast(origin, unitDirection, maxDistance, [&](int32_t proxy, float enter) {
            return visit(records_[tree_.userData(proxy)].entity, enter);
        });
    }

    template <typename Visit>
    void queryAabb(const Aabb& box, Visit&& visit) const {
        tree_.queryAabb(box, [&](int32_t proxy) {
            const Record& record = records_[tree_.userData(proxy)];
            return record.bounds.overlaps(box) ? visit(record.entity) : true;
        });
    }

    [[nodiscard]] bool contains(EntityId entity) const { return find(entity) != nullptr; }
    [[nodiscard]] const glm::mat4* worldMatrix(EntityId entity) const;
    [[nodiscard]] const Aabb* worldBounds(EntityId entity) const;
    [[nodiscard]] const SpatialIndexStats& stats() const { return stats_; }
    [[nodiscard]] const DynamicAabbTree& tree() const { return tree_; }
    void clear();

private:
    struct Record {
        EntityId entity = kNullEntity;
        int32_t proxy = DynamicAabbTree::kNull;
        uint64_t seen = 0;
        uint64_t visibleStamp = 0;
        uint32_t meshHandle = ~0u;
        bool parented = false;
        glm::vec3 position{0.0f};
        glm::vec4 rotation{0.0f};
        glm::vec3 scale{0.0f};
        glm::vec3 localMin{0.0f};
        glm::vec3 localMax{0.0f};
        glm::mat4 world{1.0f};
        Aabb bounds;
    };

    [[nodiscard]] const Record* find(EntityId entity) const;
    void drop(Record& record);

    DynamicAabbTree tree_{0.05f};
    std::vector<Record> records_; // indexed by entity slot
    uint64_t syncStamp_ = 0;
    uint64_t cullStamp_ = 0;
    SpatialIndexStats stats_;
};

} // namespace engine::core

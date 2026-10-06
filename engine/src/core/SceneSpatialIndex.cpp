#include "core/SceneSpatialIndex.hpp"

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "core/Mesh.hpp"

namespace engine::core {

namespace {
size_t slotOf(EntityId entity) { return static_cast<size_t>(entt::to_entity(entity)); }
} // namespace

const SceneSpatialIndex::Record* SceneSpatialIndex::find(EntityId entity) const {
    const size_t slot = slotOf(entity);
    if (slot >= records_.size()) return nullptr;
    const Record& record = records_[slot];
    return record.entity == entity && record.proxy != DynamicAabbTree::kNull ? &record : nullptr;
}

const glm::mat4* SceneSpatialIndex::worldMatrix(EntityId entity) const {
    const Record* record = find(entity);
    return record ? &record->world : nullptr;
}

const Aabb* SceneSpatialIndex::worldBounds(EntityId entity) const {
    const Record* record = find(entity);
    return record ? &record->bounds : nullptr;
}

bool SceneSpatialIndex::wasVisible(EntityId entity) const {
    const Record* record = find(entity);
    return record != nullptr && record->visibleStamp == cullStamp_;
}

void SceneSpatialIndex::drop(Record& record) {
    if (record.proxy != DynamicAabbTree::kNull) {
        tree_.remove(record.proxy);
        ++stats_.removed;
    }
    record = Record{};
}

void SceneSpatialIndex::clear() {
    tree_.clear();
    records_.clear();
    stats_ = {};
}

void SceneSpatialIndex::sync(ECS& ecs, const MeshLibrary& meshes) {
    sync(ecs, [&meshes](uint32_t handle, glm::vec3& min, glm::vec3& max) {
        const Mesh* mesh = meshes.get(handle);
        if (mesh == nullptr) return false;
        min = mesh->localBoundsMin();
        max = mesh->localBoundsMax();
        return true;
    });
}

void SceneSpatialIndex::sync(ECS& ecs, const MeshBoundsFn& bounds) {
    ++syncStamp_;
    stats_.moved = stats_.inserted = stats_.removed = 0;

    auto view = ecs.view<Transform, Renderable>();
    for (auto entity : view) {
        const Renderable& renderable = view.get<Renderable>(entity);
        if (!renderable.visible) continue;
        glm::vec3 localMin;
        glm::vec3 localMax;
        if (!bounds(renderable.meshHandle, localMin, localMax)) continue;

        const size_t slot = slotOf(entity);
        if (slot >= records_.size()) records_.resize(slot + 1);
        Record& record = records_[slot];
        if (record.entity != entity && record.proxy != DynamicAabbTree::kNull) drop(record);

        const Transform& transform = view.get<Transform>(entity);
        const auto* links = ecs.tryGetComponent<Hierarchy>(entity);
        const bool parented = links != nullptr && links->parent != kNullEntity;
        const glm::vec4 rotation(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w);

        record.seen = syncStamp_;
        const bool unchanged = record.proxy != DynamicAabbTree::kNull && !parented && !record.parented &&
                               record.meshHandle == renderable.meshHandle && record.position == transform.position &&
                               record.rotation == rotation && record.scale == transform.scale &&
                               record.localMin == localMin && record.localMax == localMax;
        if (unchanged) continue;

        record.entity = entity;
        record.meshHandle = renderable.meshHandle;
        record.parented = parented;
        record.position = transform.position;
        record.rotation = rotation;
        record.scale = transform.scale;
        record.localMin = localMin;
        record.localMax = localMax;
        record.world = parented ? hierarchy::computeWorldMatrix(ecs, entity) : transform.matrix();
        record.bounds = Aabb::transformed({localMin, localMax}, record.world);

        if (record.proxy == DynamicAabbTree::kNull) {
            record.proxy = tree_.insert(record.bounds, static_cast<uint32_t>(slot));
            ++stats_.inserted;
        } else if (tree_.update(record.proxy, record.bounds)) {
            ++stats_.moved;
        }
    }

    for (Record& record : records_) {
        if (record.proxy != DynamicAabbTree::kNull && record.seen != syncStamp_) drop(record);
    }
    stats_.objects = tree_.proxyCount();
    stats_.treeHeight = tree_.height();
}

void SceneSpatialIndex::cull(const glm::mat4& viewProj, std::vector<EntityId>& out) {
    ++cullStamp_;
    const Frustum frustum = Frustum::fromViewProjection(viewProj);
    size_t visible = 0;
    tree_.queryFrustum(frustum, [&](int32_t proxy) {
        Record& record = records_[tree_.userData(proxy)];
        if (!frustum.intersects(record.bounds)) return;
        record.visibleStamp = cullStamp_;
        out.push_back(record.entity);
        ++visible;
    });
    stats_.visible = visible;
    stats_.culled = tree_.proxyCount() - visible;
}

} // namespace engine::core

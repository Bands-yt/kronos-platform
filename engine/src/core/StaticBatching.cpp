#include "core/StaticBatching.hpp"

#include <algorithm>
#include <limits>
#include <cstring>

namespace engine::core {

namespace {

size_t slotOf(EntityId entity) { return static_cast<size_t>(entt::to_entity(entity)); }

struct Hasher {
    uint64_t value = 14695981039346656037ull;
    void bytes(const void* data, size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) value = (value ^ p[i]) * 1099511628211ull;
    }
    void f(float v) {
        if (v == 0.0f) v = 0.0f; // -0 and +0 match
        bytes(&v, sizeof(v));
    }
    void u(uint64_t v) { bytes(&v, sizeof(v)); }
    void v3(const glm::vec3& v) { f(v.x), f(v.y), f(v.z); }
    void v4(const glm::vec4& v) { f(v.x), f(v.y), f(v.z), f(v.w); }
};

uint64_t mix(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return x;
}

} // namespace

uint64_t StaticBatchSet::materialKey(const Renderable& r) {
    Hasher h;
    h.v4(r.baseColor);
    h.f(r.metallic);
    h.f(r.roughness);
    h.f(r.normalIntensity);
    h.u(r.materialHandle);
    h.u(r.useTriplanarProjection);
    h.u(r.castsShadow);
    h.v3(r.emissiveColor);
    h.f(r.emissiveIntensity);
    for (uint32_t texture : {r.albedoTexture, r.normalTexture, r.metallicTexture, r.roughnessTexture, r.aoTexture}) h.u(texture);
    const MaterialLayers& l = r.layers;
    h.f(l.clearcoat);
    h.f(l.clearcoatRoughness);
    h.f(l.anisotropy);
    h.f(l.anisotropyRotation);
    h.v3(l.sheenColor);
    h.f(l.sheenRoughness);
    h.f(l.specular);
    h.f(l.waterWaves);
    h.f(l.waterFoam);
    h.f(l.gridSize);
    return h.value | 1u;
}

bool StaticBatchSet::eligible(const Renderable& r) {
    return r.visible && !r.instanced && r.transmission <= 0.0f && !r.unlitSilhouette &&
           r.surfacePipeline == Renderable::kInvalidHandle && r.meshHandle != Renderable::kInvalidHandle;
}

bool StaticBatchSet::isBatched(EntityId entity) const {
    const size_t slot = slotOf(entity);
    return slot < tracked_.size() && tracked_[slot].entity == entity && tracked_[slot].batch >= 0;
}

uint64_t StaticBatchSet::groupKey(const Tracked& tracked, const glm::vec3& worldCenter) const {
    const glm::ivec3 cell = glm::ivec3(glm::floor(worldCenter / std::max(settings_.cellSize, 0.001f)));
    uint64_t key = tracked.material;
    key = mix(key ^ static_cast<uint64_t>(static_cast<uint32_t>(cell.x)));
    key = mix(key ^ (static_cast<uint64_t>(static_cast<uint32_t>(cell.y)) << 21));
    key = mix(key ^ (static_cast<uint64_t>(static_cast<uint32_t>(cell.z)) << 42));
    return key | 1u;
}

bool StaticBatchSet::hostMeshReady(uint32_t meshHandle, uint64_t version) const {
    auto it = meshCache_.find(meshHandle);
    return it != meshCache_.end() && it->second.version == version;
}

std::shared_ptr<const HostMesh> StaticBatchSet::hostMesh(uint32_t meshHandle, uint64_t version) {
    auto it = meshCache_.find(meshHandle);
    if (it != meshCache_.end() && it->second.version == version) return it->second.mesh;
    ++fetchesThisUpdate_;
    std::shared_ptr<const HostMesh> mesh = backend_.fetch ? backend_.fetch(meshHandle) : nullptr;
    meshCache_[meshHandle] = {version, mesh};
    return mesh;
}

void StaticBatchSet::retire(uint32_t meshHandle) {
    if (meshHandle == Renderable::kInvalidHandle) return;
    retired_.push_back({meshHandle, updates_ + settings_.releaseDelayUpdates});
}

void StaticBatchSet::evict(Tracked& tracked, bool moved) {
    if (tracked.group == 0) return;
    auto it = groups_.find(tracked.group);
    if (it != groups_.end()) {
        Group& group = it->second;
        const uint32_t slot = static_cast<uint32_t>(&tracked - tracked_.data());
        group.slots.erase(std::remove(group.slots.begin(), group.slots.end(), slot), group.slots.end());
        group.dirty = true;
        if (group.batch >= 0) {
            batches_[static_cast<size_t>(group.batch)].active = false;
            for (uint32_t member : group.slots) tracked_[member].batch = -1;
        }
    }
    if (tracked.batch >= 0) {
        ++stats_.evictions;
        if (moved && ++tracked.evictions >= settings_.maxEvictions) tracked.blocked = true;
    }
    tracked.batch = -1;
    tracked.group = 0;
}

void StaticBatchSet::update(ECS& ecs, uint64_t frame) {
    if (frame == lastFrame_ || !hasBackend()) return;
    lastFrame_ = frame;
    ++updates_;
    fetchesThisUpdate_ = 0;

    for (auto it = retired_.begin(); it != retired_.end();) {
        if (it->releaseAt <= updates_) {
            if (backend_.release) backend_.release(it->meshHandle);
            it = retired_.erase(it);
        } else {
            ++it;
        }
    }

    auto view = ecs.view<Transform, Renderable>();
    for (auto entity : view) {
        const size_t slot = slotOf(entity);
        if (slot >= tracked_.size()) tracked_.resize(slot + 1);
        Tracked& t = tracked_[slot];
        if (t.entity != entity) {
            evict(t);
            t = Tracked{};
            t.entity = entity;
        }
        t.seen = updates_;

        const Renderable& renderable = view.get<Renderable>(entity);
        const Transform& transform = view.get<Transform>(entity);
        const auto* links = ecs.tryGetComponent<Hierarchy>(entity);
        const auto* body = ecs.tryGetComponent<RigidBody>(entity);
        const uint64_t version = eligible(renderable) && backend_.meshVersion ? backend_.meshVersion(renderable.meshHandle) : 0;
        const bool canBatch = version != 0 && (links == nullptr || links->parent == kNullEntity) &&
                              (body == nullptr || body->motionType == RigidBodyMotionType::Static) &&
                              !ecs.hasComponent<SkinnedRenderable>(entity);

        const glm::vec4 rotation(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w);
        const uint64_t material = materialKey(renderable);
        const bool moved = t.position != transform.position || t.rotation != rotation || t.scale != transform.scale;
        if (moved || t.meshHandle != renderable.meshHandle || t.meshVersion != version || t.material != material) {
            evict(t, moved);
            t.meshHandle = renderable.meshHandle;
            t.meshVersion = version;
            t.material = material;
            t.position = transform.position;
            t.rotation = rotation;
            t.scale = transform.scale;
            t.stableFor = 0;
        } else if (t.stableFor < ~0u) {
            ++t.stableFor;
        }

        if (!canBatch) {
            evict(t);
            continue;
        }
        if (t.blocked || t.group != 0) continue;
        const uint32_t backoff = std::min<uint32_t>(t.evictions, 4);
        if (t.stableFor < (settings_.stableUpdates << (2 * backoff))) continue;

        if (!hostMeshReady(renderable.meshHandle, version) && fetchesThisUpdate_ >= settings_.fetchesPerUpdate) continue;
        std::shared_ptr<const HostMesh> mesh = hostMesh(renderable.meshHandle, version);
        if (!mesh || mesh->vertices.empty() || mesh->vertices.size() > settings_.maxSourceVertices) {
            t.blocked = true;
            continue;
        }
        glm::vec3 localMin(std::numeric_limits<float>::max());
        glm::vec3 localMax(std::numeric_limits<float>::lowest());
        for (const Vertex& v : mesh->vertices) {
            localMin = glm::min(localMin, v.position);
            localMax = glm::max(localMax, v.position);
        }
        const glm::vec3 center = Aabb::transformed({localMin, localMax}, transform.matrix()).center();
        t.group = groupKey(t, center);
        Group& group = groups_[t.group];
        if (group.slots.empty()) group.material = renderable;
        group.slots.push_back(static_cast<uint32_t>(slot));
        group.dirty = true;
    }

    for (Tracked& t : tracked_) {
        if (t.entity != kNullEntity && t.seen != updates_) {
            evict(t);
            t = Tracked{};
        }
    }

    // Groups whose batch was hidden by an eviction go first.
    std::vector<uint64_t> dirty;
    for (auto& [key, group] : groups_) {
        if (group.dirty) dirty.push_back(key);
    }
    std::stable_partition(dirty.begin(), dirty.end(), [&](uint64_t key) {
        const Group& group = groups_[key];
        return group.batch >= 0 && !batches_[static_cast<size_t>(group.batch)].active;
    });
    size_t rebuilt = 0;
    for (uint64_t key : dirty) {
        if (rebuilt >= settings_.rebuildsPerUpdate) break;
        auto it = groups_.find(key);
        rebuildGroup(ecs, key, it->second);
        if (it->second.slots.empty() && it->second.batch < 0) groups_.erase(it);
        ++rebuilt;
    }

    stats_.batches = 0;
    stats_.batchedObjects = 0;
    for (const Batch& batch : batches_) {
        if (batch.active) ++stats_.batches;
    }
    stats_.trackedObjects = 0;
    for (const Tracked& t : tracked_) {
        if (t.entity == kNullEntity) continue;
        ++stats_.trackedObjects;
        if (t.batch >= 0) ++stats_.batchedObjects;
    }
    stats_.pendingRebuilds = dirty.size() - rebuilt;
}

void StaticBatchSet::rebuildGroup(ECS& ecs, uint64_t groupKey, Group& group) {
    group.dirty = false;
    auto dropBatch = [&] {
        if (group.batch < 0) return;
        Batch& batch = batches_[static_cast<size_t>(group.batch)];
        retire(batch.meshHandle);
        batch = Batch{};
        freeBatches_.push_back(group.batch);
        group.batch = -1;
    };
    for (uint32_t slot : group.slots) tracked_[slot].batch = -1;

    HostMesh merged;
    std::vector<EntityId> members;
    std::vector<uint32_t> included;
    for (uint32_t slot : group.slots) {
        Tracked& t = tracked_[slot];
        auto* transform = ecs.tryGetComponent<Transform>(t.entity);
        if (transform == nullptr) continue;
        std::shared_ptr<const HostMesh> mesh = hostMesh(t.meshHandle, t.meshVersion);
        if (!mesh) continue;
        if (merged.vertices.size() + mesh->vertices.size() > settings_.maxBatchVertices) break;

        const glm::mat4 world = transform->matrix();
        const glm::mat3 linear(world);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
        const uint32_t base = static_cast<uint32_t>(merged.vertices.size());
        for (Vertex v : mesh->vertices) {
            v.position = glm::vec3(world * glm::vec4(v.position, 1.0f));
            const glm::vec3 n = normalMatrix * v.normal;
            v.normal = glm::length(n) > 0.0f ? glm::normalize(n) : v.normal;
            const glm::vec3 tangent = linear * glm::vec3(v.tangent);
            if (glm::length(tangent) > 0.0f) v.tangent = glm::vec4(glm::normalize(tangent), v.tangent.w);
            merged.vertices.push_back(v);
        }
        const bool mirrored = glm::determinant(linear) < 0.0f;
        for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
            merged.indices.push_back(base + mesh->indices[i]);
            merged.indices.push_back(base + mesh->indices[mirrored ? i + 2 : i + 1]);
            merged.indices.push_back(base + mesh->indices[mirrored ? i + 1 : i + 2]);
        }
        members.push_back(t.entity);
        included.push_back(slot);
    }

    if (members.size() < 2) {
        dropBatch();
        return;
    }
    const uint32_t handle = backend_.upload(merged);
    if (handle == Renderable::kInvalidHandle) {
        dropBatch();
        return;
    }

    int32_t index = group.batch;
    if (index >= 0) {
        retire(batches_[static_cast<size_t>(index)].meshHandle);
    } else if (!freeBatches_.empty()) {
        index = freeBatches_.back();
        freeBatches_.pop_back();
    } else {
        index = static_cast<int32_t>(batches_.size());
        batches_.emplace_back();
    }
    group.batch = index;

    Batch& batch = batches_[static_cast<size_t>(index)];
    batch.meshHandle = handle;
    batch.material = group.material;
    batch.material.meshHandle = handle;
    batch.vertexCount = static_cast<uint32_t>(merged.vertices.size());
    batch.indexCount = static_cast<uint32_t>(merged.indices.size());
    batch.members = std::move(members);
    batch.key = groupKey;
    batch.active = true;
    batch.bounds = {glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
    for (const Vertex& v : merged.vertices) {
        batch.bounds.min = glm::min(batch.bounds.min, v.position);
        batch.bounds.max = glm::max(batch.bounds.max, v.position);
    }
    for (uint32_t slot : included) tracked_[slot].batch = index;
    ++stats_.rebuilds;
}

void StaticBatchSet::clear() {
    for (Batch& batch : batches_) retire(batch.meshHandle);
    batches_.clear();
    freeBatches_.clear();
    groups_.clear();
    tracked_.clear();
    meshCache_.clear();
    stats_ = {};
}

} // namespace engine::core

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Bvh.hpp"
#include "core/Components.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"

namespace engine::core {

struct HostMesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

// How batches reach the GPU. Tests plug in fakes.
struct StaticBatchBackend {
    // Changes whenever the mesh behind a handle is replaced; 0 = not loaded yet.
    std::function<uint64_t(uint32_t meshHandle)> meshVersion;
    std::function<std::shared_ptr<const HostMesh>(uint32_t meshHandle)> fetch;
    // Returns a mesh handle, or Renderable::kInvalidHandle on failure.
    std::function<uint32_t(const HostMesh& merged)> upload;
    std::function<void(uint32_t meshHandle)> release;
    const void* owner = nullptr; // what the callbacks talk to, so callers can tell when to rebind
};

struct StaticBatchSettings {
    uint32_t stableUpdates = 60;       // unchanged this many updates before an object is batched
    float cellSize = 32.0f;            // batches never span more than one cell, so they still cull well
    uint32_t maxSourceVertices = 20000; // bigger meshes are already one efficient draw
    uint32_t maxBatchVertices = 1u << 20;
    uint32_t rebuildsPerUpdate = 4;
    uint32_t fetchesPerUpdate = 32;    // source meshes downloaded per update; the rest wait their turn
    uint32_t releaseDelayUpdates = 4;  // frames in flight may still read a replaced batch
    uint32_t maxEvictions = 3;         // objects that keep moving are left out for good
    // Each move out of a batch multiplies the wait before rejoining by 4.
};

struct StaticBatchStats {
    size_t batches = 0;
    size_t batchedObjects = 0;
    size_t trackedObjects = 0;
    size_t evictions = 0;     // total
    size_t rebuilds = 0;      // total
    size_t pendingRebuilds = 0;
};

// Automatic static batching. Objects that have not moved or changed their
// material for `stableUpdates` updates are merged, in world space, with
// other objects that share their material and spatial cell, into one mesh
// drawn with one call. An object that moves, changes material or is
// destroyed leaves its batch on the next update and the batch is rebuilt
// without it; an object that keeps doing that stops being batched.
//
// Lives in the registry context (`ecs.raw().ctx()`), next to the
// SceneSpatialIndex; the renderer updates it once per frame.
class StaticBatchSet {
public:
    struct Batch {
        uint32_t meshHandle = Renderable::kInvalidHandle;
        Renderable material;
        Aabb bounds;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        std::vector<EntityId> members;
        uint64_t key = 0;
        bool active = false; // false while waiting for a rebuild; members draw on their own then
    };

    // Destruction never calls the backend: whoever owns the meshes frees them.
    StaticBatchSet() = default;
    ~StaticBatchSet() = default;
    StaticBatchSet(const StaticBatchSet&) = delete;
    StaticBatchSet& operator=(const StaticBatchSet&) = delete;
    StaticBatchSet(StaticBatchSet&&) = default;
    StaticBatchSet& operator=(StaticBatchSet&&) = default;

    void setBackend(StaticBatchBackend backend) { backend_ = std::move(backend); }
    [[nodiscard]] bool hasBackend() const { return static_cast<bool>(backend_.upload); }
    [[nodiscard]] const StaticBatchBackend& backend() const { return backend_; }
    void setSettings(const StaticBatchSettings& settings) { settings_ = settings; }
    [[nodiscard]] const StaticBatchSettings& settings() const { return settings_; }

    // Runs at most once per `frame` value.
    void update(ECS& ecs, uint64_t frame);

    [[nodiscard]] bool isBatched(EntityId entity) const;
    [[nodiscard]] const std::vector<Batch>& batches() const { return batches_; }
    [[nodiscard]] const StaticBatchStats& stats() const { return stats_; }

    // Releases every batch through the backend; all objects draw on their own again.
    void clear();

    // Equal for renderables that can share one draw.
    [[nodiscard]] static uint64_t materialKey(const Renderable& renderable);
    [[nodiscard]] static bool eligible(const Renderable& renderable);

private:
    struct Tracked {
        EntityId entity = kNullEntity;
        uint64_t seen = 0;
        uint32_t stableFor = 0;
        uint32_t evictions = 0;
        uint32_t meshHandle = Renderable::kInvalidHandle;
        uint64_t meshVersion = 0;
        uint64_t material = 0;
        glm::vec3 position{0.0f};
        glm::vec4 rotation{0.0f};
        glm::vec3 scale{0.0f};
        int32_t batch = -1;
        uint64_t group = 0;
        bool blocked = false;
    };

    struct Group {
        std::vector<uint32_t> slots; // tracked objects wanting to be in this group's batch
        int32_t batch = -1;
        bool dirty = false;
        Renderable material;
    };

    void evict(Tracked& tracked, bool moved = false);
    void rebuildGroup(ECS& ecs, uint64_t groupKey, Group& group);
    void retire(uint32_t meshHandle);
    std::shared_ptr<const HostMesh> hostMesh(uint32_t meshHandle, uint64_t version);
    [[nodiscard]] bool hostMeshReady(uint32_t meshHandle, uint64_t version) const;
    [[nodiscard]] uint64_t groupKey(const Tracked& tracked, const glm::vec3& worldCenter) const;

    StaticBatchBackend backend_;
    StaticBatchSettings settings_;
    StaticBatchStats stats_;
    std::vector<Tracked> tracked_; // indexed by entity slot
    std::unordered_map<uint64_t, Group> groups_;
    std::vector<Batch> batches_;
    std::vector<int32_t> freeBatches_;
    struct CachedMesh {
        uint64_t version = 0;
        std::shared_ptr<const HostMesh> mesh;
    };
    std::unordered_map<uint32_t, CachedMesh> meshCache_;
    struct Retired {
        uint32_t meshHandle;
        uint64_t releaseAt;
    };
    std::vector<Retired> retired_;
    uint64_t lastFrame_ = ~0ull;
    uint64_t updates_ = 0;
    uint32_t fetchesThisUpdate_ = 0;
};

} // namespace engine::core

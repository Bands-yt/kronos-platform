#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/Bvh.hpp"
#include "core/ECS.hpp"
#include "core/ResourceManager.hpp"
#include "core/SceneFile.hpp"
#include "core/SceneManager.hpp"

namespace engine::core {

struct WorldCellCoord {
    int32_t x = 0;
    int32_t z = 0;
    friend bool operator==(const WorldCellCoord&, const WorldCellCoord&) = default;
};

// The cell layout of a streamed world: `<scene>.world/world.kworld`, next to
// cell files that are ordinary scene files.
struct WorldManifest {
    float cellSize = 64.0f;
    float loadRadius = 160.0f;   // a cell loads when a source comes this close to it
    float unloadRadius = 224.0f; // and unloads once every source is further than this
    std::string cellExtension = ".scene";

    struct Cell {
        WorldCellCoord coord;
        std::string file; // relative to the manifest
        uint32_t entities = 0;
        Aabb bounds;      // content bounds; may reach past the cell square
        bool hasBounds = false;
    };
    std::vector<Cell> cells;

    [[nodiscard]] static std::string directoryFor(const std::string& scenePath);
    [[nodiscard]] static std::string pathFor(const std::string& scenePath);
    [[nodiscard]] static WorldCellCoord cellOf(const glm::vec3& position, float cellSize);
    [[nodiscard]] static std::string cellFileName(WorldCellCoord coord, const std::string& extension);

    [[nodiscard]] int find(WorldCellCoord coord) const;
    // XZ distance from `position` to the cell square merged with its content bounds.
    [[nodiscard]] float distanceTo(const Cell& cell, const glm::vec3& position) const;

    [[nodiscard]] bool parse(const std::string& text, std::string& error);
    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] bool load(const std::string& path, std::string& error);
    [[nodiscard]] bool save(const std::string& path) const;
};

enum class WorldCellState : uint8_t { Unloaded, Loading, Loaded, Failed };

struct WorldStreamingStats {
    size_t cells = 0;
    size_t loaded = 0;
    size_t loading = 0;
    size_t failed = 0;
    size_t entities = 0;
    size_t editedCells = 0; // unloaded cells holding unsaved edits
    uint64_t loads = 0;
    uint64_t unloads = 0;
};

// Streams a world's cells in and out around streaming sources, through the
// resource layer: each cell is a ResourceKind::Scene resource, decoded on a
// worker and instantiated on the main thread, released (and its entities
// destroyed) once no source is near and the resource unload delay passes.
// Cell entities carry StreamedCell, so main scene saves leave them out.
//
// Editing: with setEditing(true), a cell unloaded with unsaved changes
// keeps them in memory until saveCells() writes them out.
class WorldStreamer {
public:
    WorldStreamer();
    ~WorldStreamer();
    WorldStreamer(const WorldStreamer&) = delete;
    WorldStreamer& operator=(const WorldStreamer&) = delete;

    // Opens `scenePath`'s world, if it has one. Returns false when it doesn't.
    bool open(const std::string& scenePath, ECS& ecs, ResourceManager& resources, const SceneBuildContext& context,
              std::string* error = nullptr);
    // Starts an empty world for `scenePath` with `settings`' layout.
    bool create(const std::string& scenePath, const WorldManifest& settings, ECS& ecs, ResourceManager& resources,
                const SceneBuildContext& context, std::string* error = nullptr);
    // Destroys every cell entity now.
    void close();
    [[nodiscard]] bool isOpen() const { return ecs_ != nullptr; }
    [[nodiscard]] const std::string& scenePath() const { return scenePath_; }

    void update(const std::vector<glm::vec3>& sources, const std::vector<float>& radiusScales = {});
    // Keeps every cell loaded regardless of sources.
    void setLoadAll(bool loadAll) { loadAll_ = loadAll; }
    [[nodiscard]] bool loadAll() const { return loadAll_; }
    void setEditing(bool editing) { editing_ = editing; }
    void setRadii(float loadRadius, float unloadRadius);

    // Moves every eligible entity without StreamedCell into the cell under
    // it. Eligible: a named root with no script, no moving body, and
    // bounds smaller than two cells (big ground planes stay in the scene).
    size_t adopt(ECS& ecs);
    [[nodiscard]] static bool adoptable(ECS& ecs, EntityId entity, float cellSize);

    // Points the world at `newScenePath`, copying the cell folder there.
    bool moveTo(const std::string& newScenePath, std::string* error = nullptr);
    // Appends every cell's entities to `file`, loaded or not, for exports
    // that need the whole world in one scene.
    void appendAllCells(SceneFile& file) const;
    // Writes changed cells and the manifest.
    bool saveCells(std::string* error = nullptr);
    [[nodiscard]] bool hasUnsavedCells() const;

    [[nodiscard]] const WorldManifest& manifest() const { return manifest_; }
    [[nodiscard]] WorldCellState cellState(size_t index) const;
    [[nodiscard]] std::vector<uint32_t> entityCounts() const; // live entities per cell
    [[nodiscard]] WorldStreamingStats stats() const;

    // Called for each cell entity right before it is destroyed.
    std::function<void(ECS&, EntityId)> beforeDestroy;
    // Called before close() frees meshes a frame in flight may still use.
    std::function<void()> waitForGpu;
    // Frees a mesh once frames in flight are done with it. Without one,
    // meshes are freed four update() calls after their last cell unloads.
    std::function<void(std::function<void()>)> deferDestroy;

    // Decoded cell, as the resource layer hands it over.
    struct DecodedCell final : ResourcePayload {
        std::string path;
        SceneFile file;
        uint64_t hash = 0;
        bool edited = false; // came from unsaved edits, not the file
    };
    [[nodiscard]] static uint64_t contentHash(const SceneFile& file);

private:
    struct Slot {
        bool live = false;
        int cell = -1;
        std::vector<std::string> meshKeys;
        uint64_t hash = 0;
        bool unsaved = false;
    };
    struct SharedMesh {
        uint32_t handle = Renderable::kInvalidHandle;
        uint32_t references = 0;
    };
    struct PendingDestroy {
        uint32_t handle;
        uint64_t at;
    };

    bool start(const std::string& scenePath, const WorldManifest& manifest, ECS& ecs, ResourceManager& resources,
               const SceneBuildContext& context);
    void installLoader();
    int addCell(WorldCellCoord coord);
    void detach(EntityId entity, int cell);
    void destroyEntity(EntityId entity);
    [[nodiscard]] uint32_t handleFor(size_t slot) const { return epoch_ << 16 | static_cast<uint32_t>(slot); }
    [[nodiscard]] Slot* slotFor(uint32_t handle);
    bool commit(DecodedCell& decoded, uint32_t handle, bool reloading, std::string& error);
    void release(uint32_t handle);
    void destroySlotEntities(Slot& slot);
    void instantiate(Slot& slot, const SceneFile& file);
    [[nodiscard]] std::string cellPath(size_t index) const;
    [[nodiscard]] int cellForPath(const std::string& normalizedPath) const;
    [[nodiscard]] SceneFile captureSlot(const Slot& slot) const;
    [[nodiscard]] std::vector<EntityId> cellEntities(int cell) const;
    [[nodiscard]] Aabb entityBounds(EntityId entity);
    void refreshCellInfo(size_t cell, const SceneFile& file);
    void collectGarbage();
    [[nodiscard]] uint32_t registerMesh(Mesh mesh);

    ECS* ecs_ = nullptr;
    ResourceManager* resources_ = nullptr;
    SceneBuildContext context_;
    std::string scenePath_;
    std::string directory_;
    WorldManifest manifest_;
    std::vector<ResourceHandle> handles_; // per cell; valid while wanted
    std::vector<Slot> slots_;
    std::vector<int> cellSlot_;           // per cell, -1 when not instantiated
    std::unordered_map<std::string, int> cellByPath_;
    std::unordered_map<std::string, uint32_t> sharedHandles_;
    std::unordered_map<std::string, SharedMesh> sharedMeshes_;
    std::vector<uint32_t> freeMeshHandles_;
    std::vector<PendingDestroy> pendingDestroy_;
    uint32_t epoch_ = 0;
    uint64_t updates_ = 0;
    bool loadAll_ = false;
    bool editing_ = false;
    bool manifestDirty_ = false;
    WorldStreamingStats counters_;

    // Unsaved cells by normalized path. Shared with decodes running on workers.
    struct EditStore {
        std::mutex mutex;
        std::unordered_map<std::string, SceneFile> files;
    };
    std::shared_ptr<EditStore> edits_;
};

} // namespace engine::core

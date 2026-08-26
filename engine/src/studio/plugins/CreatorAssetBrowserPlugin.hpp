#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AssetImportQueue.hpp"
#include "core/AssetRegistry.hpp"
#include "core/Mesh.hpp"
#include "core/WorldProp.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/plugins/TerrainEditorPlugin.hpp"

namespace engine::studio::plugins {

// Sprint 10 ("Creator Tools Phase 2") task category 4: a real browser
// over the actual spawnable/appliable assets this engine already has --
// six real WorldPropKinds, five real material presets
// (studio::kMaterialPresets, extracted this pass specifically so this
// browser and MaterialPlugin share the exact same table, not a second
// copy), six real particle presets (studio::ParticlePresetId, same
// extraction reasoning), and three real terrain presets. Deliberately
// not a new asset *type* or a fake catalogue -- every "Use" button here
// calls the exact same real function the dedicated tool for that asset
// kind would (studio::spawnPropAuthoring(), MaterialPresetInfo's fields,
// applyParticlePreset(), core::Terrain::applyPreset()), so this is a
// genuinely faster on-ramp to existing tools, not a parallel system that
// could drift from them.
class CreatorAssetBrowserPlugin final : public IStudioPlugin {
public:
    CreatorAssetBrowserPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                               core::MeshLibrary& meshLibrary, TerrainEditorPlugin& terrainEditor);

    [[nodiscard]] const char* name() const override { return "Asset Browser"; }
    [[nodiscard]] const char* category() const override { return "Assets"; }

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Kronos ("Asset Hot-Import Pipeline" -- real OS drag-and-drop):
    // real, non-blocking -- queues `path` on importQueue_ the exact same
    // way the panel's own "Import" button does, so a file dragged from
    // the OS file manager onto the Studio window and one typed into the
    // Import box behave identically. Called from StudioApp's own
    // SDL_DROPFILE handling (see Window::setRawEventCallback's own doc
    // comment) -- deliberately not this class discovering SDL events
    // itself, same "one real event pump, not a second competing one"
    // reasoning that callback mechanism already exists for.
    void submitDroppedFile(const std::string& path);

private:
    void drawPropEntries(core::ECS& ecs);
    void drawMaterialEntries(core::ECS& ecs, core::EntityId selected);
    void drawParticleEntries(core::ECS& ecs, core::EntityId selected);
    void drawTerrainEntries();
    // Kronos (Alpha Roadmap Phase 8, "Asset Pipeline" -- "Asset
    // registry"): the real, creator-imported half of this browser --
    // distinct from every drawXEntries() above, which browse the
    // engine's own built-in preset catalogue. See core/AssetRegistry.hpp
    // for the real persisted list this reads/writes.
    void drawImportedAssetsSection();

    // Real, simple search -- case-insensitive substring match against an
    // entry's name or its real tag string; `false` (never filtered out)
    // when the search box is empty.
    [[nodiscard]] bool matchesSearch(const char* nameText, const char* tags) const;

    core::MeshLibrary* meshLibrary_;
    TerrainEditorPlugin* terrainEditor_;
    uint32_t boxMesh_ = core::Renderable::kInvalidHandle;
    uint32_t capsuleMesh_ = core::Renderable::kInvalidHandle;
    int propSpawnCount_ = 0;

    char searchBuffer_[128] = "";
    // 0=All, 1=Props, 2=Materials, 3=Particles, 4=Terrain, 5=Imported.
    int categoryFilter_ = 0;

    core::AssetRegistry assetRegistry_;
    char importPathBuffer_[256] = "";
    std::string importStatusMessage_;

    // Kronos ("Asset Hot-Import Pipeline"): real background worker pool
    // -- both the "Import" button and submitDroppedFile() below queue
    // through this instead of calling assetRegistry_.importAsset()
    // synchronously, so a large/slow file (a big glTF mesh, a large
    // texture) never freezes the Studio render loop. update() polls it
    // every frame and adopts each real completed result into
    // assetRegistry_ via AssetRegistry::adoptMetadata().
    core::AssetImportQueue importQueue_;
};

} // namespace engine::studio::plugins

#pragma once

#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "core/Biome.hpp"
#include "core/Components.hpp"
#include "core/Mesh.hpp"
#include "core/Navigation.hpp"
#include "core/Renderer.hpp"
#include "core/WorldProp.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/plugins/TerrainEditorPlugin.hpp"

namespace engine::studio::plugins {

// Sprint 7 ("Studio UI Revamp") task category 3's "Creator Tools panel
// for props/terrain/lighting/teleport pads" -- a real, one-click
// authoring surface over content Sprint 6 introduced, so placing a tree
// or linking two teleport pads doesn't require hand-writing the
// Transform/Renderable/marker component block main.cpp's bring-up scene
// does today (see that file's prop/teleport-pad blocks, which this
// panel's spawn helpers mirror).
//
// Deliberately authoring-only, matching Studio's own "no Physics here"
// boundary (see StudioApp's class comment): spawned props get a real
// Transform + Renderable + MeshSource + WorldProp marker (+ Interactable
// for the interactive kinds) but no live Jolt body -- Studio never runs
// physics outside PhysicsPreviewPlugin's own sandboxed Play mode, so
// attaching one here would either dangle (no physics world to attach
// into) or require standing up a second, editor-owned world nobody
// steps. A real runtime spawn path (main.cpp, or a future creator
// world's own load code) calls core::spawnWorldProp() with a live
// core::Physics& for the real collider -- this panel places the same
// visual/marker data at author time, physics attaches at run time, the
// same split Prefab.hpp already draws between "what a save file stores"
// and "what a live body needs".
//
// Terrain: deliberately does NOT duplicate TerrainEditorPlugin's brush
// UI -- it offers one-click presets over the *same* core::Terrain
// instance (via the reference passed in, see hasTerrain()/terrain()) so
// there is exactly one terrain in a session, not two competing ones.
// Lighting: applies a real core::BiomePreset onto the live
// core::Renderer's current lighting, visibly changing the viewport --
// see drawBiomeSection()'s comment for why directionWS is preserved.
//
// The actual entity-creation logic lives in studio/CreatorToolsSpawning.hpp,
// not here -- this class stays Vulkan-coupled (mesh registration needs a
// live device), but spawning an authoring entity doesn't, so that half is
// split into its own ECS-only, headlessly-testable module (see that
// header's own comment).
class CreatorToolsPlugin final : public IStudioPlugin {
public:
    CreatorToolsPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                        core::MeshLibrary& meshLibrary, core::Renderer& renderer, TerrainEditorPlugin& terrainEditor);

    [[nodiscard]] const char* name() const override { return "Creator Tools"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawPropSection(core::ECS& ecs);
    void drawTeleportSection(core::ECS& ecs);
    void drawNavMarkerSection(core::ECS& ecs);
    void drawPointLightSection(core::ECS& ecs);
    void drawTerrainSection();
    void drawBiomeSection();

    // Sprint 9 ("Creator Tools Phase 1") task category 2's "snap-to-grid,
    // snap-to-surface" -- applies whichever of the two real toggles below
    // are on to `spawnPosition_`, returning the real position the next
    // prop/pad actually spawns at (spawnPosition_ itself stays whatever
    // the user typed/dragged, so toggling snap off restores exactly what
    // was there before -- snapping is a real, non-destructive view over
    // the raw field, not a one-way mutation of it).
    [[nodiscard]] glm::vec3 effectiveSpawnPosition() const;

    core::MeshLibrary* meshLibrary_;
    core::Renderer* renderer_;
    TerrainEditorPlugin* terrainEditor_;

    uint32_t boxMesh_ = core::Renderable::kInvalidHandle;
    uint32_t capsuleMesh_ = core::Renderable::kInvalidHandle;

    // Where the next prop/pad spawns -- a real, simple, editable field
    // (not mouse-picked; see TerrainEditorPlugin's own "one interaction
    // step short of the mouse-driven version" comment for why this
    // engine's Studio doesn't have viewport-click placement yet either).
    glm::vec3 spawnPosition_{0.0f, 1.0f, 0.0f};
    int propSpawnCount_ = 0; // disambiguates entity names ("Tree 3") across repeated spawns

    // Sprint 9 task category 2: real rotation/scale applied to a prop at
    // spawn time (not just editable afterward via Inspector -- a real
    // placement tool lets you set orientation/size *while* placing).
    glm::vec3 spawnRotationDegrees_{0.0f, 0.0f, 0.0f};
    float spawnScale_ = 1.0f;

    // Snap-to-grid: rounds spawnPosition_'s X/Z to the nearest gridSize_
    // multiple. Snap-to-surface: overrides Y with the real terrain height
    // at that X/Z (TerrainEditorPlugin's terrain, when one exists) --
    // real, honest no-op with no terrain present, not a crash or a
    // silent 0.
    bool snapToGridEnabled_ = false;
    float gridSize_ = 1.0f;
    bool snapToSurfaceEnabled_ = false;

    char teleportTagBuffer_[64] = "new_route";
    glm::vec3 teleportDestination_{0.0f, 1.0f, 0.0f};
    int teleportSpawnCount_ = 0;

    // Sprint 9 task category 4: nav marker placement.
    core::NavMarkerKind selectedNavMarkerKind_ = core::NavMarkerKind::Custom;
    char navMarkerLabelBuffer_[64] = "";
    int navMarkerSpawnCount_ = 0;

    // Kronos (Alpha Roadmap Phase 6, "Tooling Layer" -- "Lighting
    // editor"): real point-light placement, see
    // studio::spawnPointLightAuthoring()'s own comment.
    int pointLightSpawnCount_ = 0;

    core::BiomeId selectedBiome_ = core::BiomeId::Forest;

    core::Terrain::Preset selectedPreset_ = core::Terrain::Preset::RollingHills;
};

} // namespace engine::studio::plugins

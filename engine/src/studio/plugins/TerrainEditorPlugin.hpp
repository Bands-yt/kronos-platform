#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Mesh.hpp"
#include "core/Terrain.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/UndoStack.hpp"

namespace engine::studio::plugins {

// Terrain sculpting -- raise/lower/smooth/flatten/paint/noise brushes
// over core::Terrain (see that class's header for the chunked-heightmap
// design and why "paint" is per-chunk color, not per-vertex texture
// splatting).
//
// Interaction model, stated plainly: brushes apply at the *primary
// selection's* world position, not a live "click in the viewport to
// paint" flow -- that needs mouse-ray-to-terrain raycasting infrastructure
// this pass doesn't build. Move any entity to where you want to sculpt
// (the existing ImGuizmo translate gizmo already does this), select it,
// and click a brush button here. Real, usable, just one interaction step
// short of the mouse-driven version a shipped terrain tool would want.
//
// Sprint 9 ("Creator Tools Phase 1") task category 1 additions: a real
// Flatten brush, a real falloff-shape slider (core::Terrain::brushFalloff()),
// and real undo/redo -- each "Apply Brush" click pushes one
// UndoStack::Command capturing a full before/after core::Terrain
// heightmap snapshot (core::Terrain::heightSnapshot()/restoreHeightSnapshot()),
// the same "capture before, commit after" shape InspectorPanel's
// Transform edits already established, just triggered by a button click
// instead of a drag gesture's activate/deactivate pair.
class TerrainEditorPlugin final : public IStudioPlugin {
public:
    TerrainEditorPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                         core::MeshLibrary& meshLibrary, UndoStack& undoStack);

    [[nodiscard]] const char* name() const override { return "Terrain Editor"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Sprint 7 ("Studio UI Revamp"): lets CreatorToolsPlugin offer one-
    // click terrain presets without owning a second, competing
    // core::Terrain instance -- there is exactly one terrain in a Studio
    // session, this plugin owns it, everything else that wants to touch
    // it goes through here.
    [[nodiscard]] bool hasTerrain() const { return terrain_.isValid(); }
    [[nodiscard]] core::Terrain& terrain() { return terrain_; }

private:
    enum class BrushMode { Raise, Lower, Smooth, Flatten, Paint, Noise };

    void drawCreationUi(core::ECS& ecs);
    void drawBrushUi(core::ECS& ecs, core::EntityId selected);
    void applyBrush(float worldX, float worldZ, core::ECS& ecs);

    core::Terrain terrain_;

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    UndoStack* undoStack_;

    uint32_t gridResolution_ = 65;
    uint32_t chunkCount_ = 8;
    float cellSize_ = 1.0f;

    BrushMode brushMode_ = BrushMode::Raise;
    float brushRadius_ = 5.0f;
    float brushStrength_ = 0.5f;
    float noiseFrequency_ = 0.15f;
    // 1.0 = the real original linear falloff every brush always had;
    // see core::Terrain::brushFalloff()'s own comment for the curve.
    float falloffPower_ = 1.0f;
    float paintColor_[4] = {0.3f, 0.55f, 0.25f, 1.0f};
};

} // namespace engine::studio::plugins

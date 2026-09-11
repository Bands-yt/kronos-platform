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
// Interaction model: brushes can still apply at the *primary selection's*
// world position via "Apply Brush" (move an entity there with the gizmo,
// select it, click). "Live Sculpt (Viewport)" below is the real mouse-
// driven alternative -- ViewportPanel::draw()'s optional terrainEditor
// overlay raycasts the mouse against core::Terrain::raycast() every frame
// while this is enabled and drives beginLiveStroke()/applyLiveBrush()/
// endLiveStroke() from the drag gesture, landing one UndoStack::Command
// per drag instead of per click.
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

    // Real mouse-driven sculpting for ViewportPanel's optional overlay --
    // see the class comment above and Terrain::raycast()'s own header
    // comment for why this is a pure analytic heightfield raycast rather
    // than a core::Physics one (Studio's terrain has no live collider).
    [[nodiscard]] bool liveSculptEnabled() const { return liveSculptEnabled_; }
    [[nodiscard]] float brushRadius() const { return brushRadius_; }
    [[nodiscard]] bool raycastFromMouse(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
                                         glm::vec3& outHitPoint) const;

    // One UndoStack::Command per drag gesture, not per per-frame brush
    // application -- beginLiveStroke() snapshots the heightmap once,
    // applyLiveBrush() can then be called every frame the mouse stays
    // down, and endLiveStroke() pushes a single before/after pair. Paint
    // mode still isn't undo-tracked (see applyBrush()'s own comment).
    //
    // applyLiveBrush() is throttled to kMaxLiveApplyRateHz (see .cpp) and
    // scales strength by real elapsed time rather than applying
    // brushStrength_ once per rendered frame -- a held mouse button can
    // otherwise call this 60-300+ times/sec, each one a full-strength
    // mutation *and* a regenerateChunksInRadius() (mesh rebuild + GPU
    // buffer replace, see Terrain::regenerateChunk()), which at an
    // uncapped frame rate saturates Smooth/Flatten in a couple of frames
    // and turns a "brief click" into a multi-frame pile of full-strength
    // Raise/Noise applications before the user's finger is even fully
    // down. deltaTime is the real per-frame delta (ViewportPanel::draw()'s
    // own deltaTime), not wall-clock time snapshotted here.
    void beginLiveStroke();
    void applyLiveBrush(float worldX, float worldZ, core::ECS& ecs, float deltaTime);
    void endLiveStroke();

private:
    enum class BrushMode { Raise, Lower, Smooth, Flatten, Paint, Noise };

    void drawCreationUi(core::ECS& ecs);
    void drawBrushUi(core::ECS& ecs, core::EntityId selected);
    void applyBrush(float worldX, float worldZ, core::ECS& ecs);
    // The actual brush mutation (no snapshot/undo bookkeeping) -- shared
    // by applyBrush()'s single-click undo pair (passes brushStrength_
    // unscaled) and applyLiveBrush() (passes a time-scaled strength for
    // every mode except Paint, which ignores `strength` entirely).
    void applyBrushCore(float worldX, float worldZ, core::ECS& ecs, float strength);

    core::Terrain terrain_;

    bool liveSculptEnabled_ = false;
    bool strokeActive_ = false;
    std::vector<float> strokeBeforeSnapshot_;
    // Real elapsed time since the last applyLiveBrush() mutation actually
    // ran -- see that method's own throttling comment.
    float liveApplyAccumTime_ = 0.0f;

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

#include "studio/plugins/TerrainEditorPlugin.hpp"

#include <algorithm>

#include <imgui.h>

#include "core/Components.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

TerrainEditorPlugin::TerrainEditorPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                          core::MeshLibrary& meshLibrary, UndoStack& undoStack)
    : allocator_(allocator), device_(device), cmdPool_(cmdPool), queue_(queue), meshLibrary_(&meshLibrary),
      undoStack_(&undoStack) {}

void TerrainEditorPlugin::drawPanel(core::ECS& ecs, core::EntityId selected,
                                     const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Terrain Editor");
    drawPluginHeader("Terrain Editor");

    if (!terrain_.isValid()) {
        drawCreationUi(ecs);
    } else {
        drawBrushUi(ecs, selected);
    }

    drawPluginFooter(terrain_.isValid() ? "" : "No terrain yet -- create one above.");
    ImGui::End();
}

void TerrainEditorPlugin::drawCreationUi(core::ECS& ecs) {
    ImGui::TextDisabled("No terrain yet.");
    int gridResolution = static_cast<int>(gridResolution_);
    int chunkCount = static_cast<int>(chunkCount_);
    ImGui::InputInt("Grid Resolution", &gridResolution);
    ImGui::InputInt("Chunk Count", &chunkCount);
    ImGui::DragFloat("Cell Size", &cellSize_, 0.05f, 0.1f, 10.0f);
    ImGui::TextDisabled("(Grid Resolution - 1) should divide evenly by Chunk Count.");

    gridResolution_ = static_cast<uint32_t>(std::max(3, gridResolution));
    chunkCount_ = static_cast<uint32_t>(std::max(1, chunkCount));

    if (ImGui::Button("Create Terrain")) {
        core::Terrain::CreateInfo info;
        info.gridResolution = gridResolution_;
        info.chunkCount = chunkCount_;
        info.cellSize = cellSize_;
        info.origin = {-static_cast<float>(gridResolution_ - 1) * cellSize_ * 0.5f, 0.0f,
                        -static_cast<float>(gridResolution_ - 1) * cellSize_ * 0.5f};
        (void)terrain_.create(info, ecs, *meshLibrary_, allocator_, device_, cmdPool_, queue_);
    }
}

void TerrainEditorPlugin::drawBrushUi(core::ECS& ecs, core::EntityId selected) {
    ImGui::TextUnformatted("Brush");
    int mode = static_cast<int>(brushMode_);
    ImGui::RadioButton("Raise", &mode, static_cast<int>(BrushMode::Raise));
    ImGui::SameLine();
    ImGui::RadioButton("Lower", &mode, static_cast<int>(BrushMode::Lower));
    ImGui::SameLine();
    ImGui::RadioButton("Smooth", &mode, static_cast<int>(BrushMode::Smooth));
    ImGui::SameLine();
    ImGui::RadioButton("Flatten", &mode, static_cast<int>(BrushMode::Flatten));
    ImGui::RadioButton("Paint", &mode, static_cast<int>(BrushMode::Paint));
    ImGui::SameLine();
    ImGui::RadioButton("Noise", &mode, static_cast<int>(BrushMode::Noise));
    brushMode_ = static_cast<BrushMode>(mode);

    ImGui::DragFloat("Radius", &brushRadius_, 0.1f, 0.5f, 50.0f);
    if (brushMode_ == BrushMode::Smooth || brushMode_ == BrushMode::Flatten) {
        ImGui::SliderFloat("Strength (blend)", &brushStrength_, 0.0f, 1.0f);
    } else {
        ImGui::DragFloat("Strength", &brushStrength_, 0.05f, -10.0f, 10.0f);
    }
    if (brushMode_ != BrushMode::Paint) {
        // Sprint 9 ("Creator Tools Phase 1") task category 1's "falloff
        // controls" -- 1.0 is the real original linear falloff every
        // brush always had; see core::Terrain::brushFalloff()'s comment.
        ImGui::SliderFloat("Falloff Shape", &falloffPower_, 0.25f, 4.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("(1 = linear, >1 = concentrated, <1 = spread out)");
    }
    if (brushMode_ == BrushMode::Noise) {
        ImGui::DragFloat("Frequency", &noiseFrequency_, 0.01f, 0.01f, 2.0f);
    }
    if (brushMode_ == BrushMode::Paint) {
        ImGui::ColorEdit4("Color", paintColor_);
    }

    ImGui::Separator();
    ImGui::Checkbox("Live Sculpt (Viewport)", &liveSculptEnabled_);
    if (liveSculptEnabled_) {
        ImGui::TextDisabled("Click and drag on the terrain in the viewport to sculpt.");
    } else {
        ImGui::TextDisabled("Brush applies at the primary selection's position --");
        ImGui::TextDisabled("move something there with the gizmo, then click Apply.");
    }

    bool canApply = selected != core::kNullEntity;
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Apply Brush")) {
        if (auto* transform = ecs.tryGetComponent<core::Transform>(selected)) {
            applyBrush(transform->position.x, transform->position.z, ecs);
        }
    }
    ImGui::EndDisabled();
    if (!canApply) {
        ImGui::TextDisabled("Select an entity to use as the brush cursor.");
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!undoStack_->canUndo());
    if (ImGui::Button("Undo Brush")) undoStack_->undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!undoStack_->canRedo());
    if (ImGui::Button("Redo Brush")) undoStack_->redo();
    ImGui::EndDisabled();
}

void TerrainEditorPlugin::applyBrushCore(float worldX, float worldZ, core::ECS& ecs, float strength) {
    switch (brushMode_) {
        case BrushMode::Raise:
            terrain_.raise(worldX, worldZ, brushRadius_, strength, falloffPower_);
            break;
        case BrushMode::Lower:
            terrain_.lower(worldX, worldZ, brushRadius_, strength, falloffPower_);
            break;
        case BrushMode::Smooth:
            terrain_.smooth(worldX, worldZ, brushRadius_, strength, falloffPower_);
            break;
        case BrushMode::Flatten:
            terrain_.flatten(worldX, worldZ, brushRadius_, strength, falloffPower_);
            break;
        case BrushMode::Noise:
            terrain_.addNoise(worldX, worldZ, brushRadius_, strength, noiseFrequency_, falloffPower_);
            break;
        case BrushMode::Paint:
            terrain_.paint(worldX, worldZ, brushRadius_,
                            glm::vec4(paintColor_[0], paintColor_[1], paintColor_[2], paintColor_[3]), ecs);
            break;
    }
}

void TerrainEditorPlugin::applyBrush(float worldX, float worldZ, core::ECS& ecs) {
    // Sprint 9 ("Creator Tools Phase 1") task category 1's "undo/redo
    // support" -- real, for the five height-mutating brushes (Raise/
    // Lower/Smooth/Flatten/Noise): a full heightmap snapshot before the
    // brush applies, another after, captured by value in the real
    // UndoStack::Command this pushes. Paint mutates per-chunk
    // Renderable::baseColor, not the heightmap, so it's deliberately not
    // covered by this snapshot -- see README's Known Issues.
    if (brushMode_ == BrushMode::Paint) {
        applyBrushCore(worldX, worldZ, ecs, brushStrength_);
        return;
    }

    std::vector<float> before = terrain_.heightSnapshot();
    applyBrushCore(worldX, worldZ, ecs, brushStrength_);
    std::vector<float> after = terrain_.heightSnapshot();

    core::Terrain* terrainPtr = &terrain_;
    undoStack_->push({"Terrain Brush",
                       [terrainPtr, before]() { terrainPtr->restoreHeightSnapshot(before); },
                       [terrainPtr, after]() { terrainPtr->restoreHeightSnapshot(after); }});
}

bool TerrainEditorPlugin::raycastFromMouse(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
                                            glm::vec3& outHitPoint) const {
    if (!terrain_.isValid()) return false;
    constexpr float kMaxSculptPickDistance = 2000.0f; // generous -- matches the scale free-fly cameras roam at, not the terrain's own size
    return terrain_.raycast(rayOrigin, rayDirection, kMaxSculptPickDistance, outHitPoint);
}

void TerrainEditorPlugin::beginLiveStroke() {
    if (strokeActive_) return; // real, honest no-op against a duplicate mouse-down (e.g. a stray event) -- never double-snapshots
    strokeActive_ = true;
    liveApplyAccumTime_ = 0.0f;
    strokeBeforeSnapshot_ = terrain_.heightSnapshot();
}

void TerrainEditorPlugin::applyLiveBrush(float worldX, float worldZ, core::ECS& ecs, float deltaTime) {
    if (!strokeActive_) return;

    // Paint sets an absolute color, so repeated per-frame application is
    // idempotent -- no strength to scale, just let it re-paint every frame
    // the mouse moves (drawTerrainSculpt() already only calls this while
    // the mouse is down and over terrain).
    if (brushMode_ == BrushMode::Paint) {
        applyBrushCore(worldX, worldZ, ecs, brushStrength_);
        return;
    }

    // Throttle to at most kMaxLiveApplyRateHz actual mutations/sec --
    // each one is a full heightmap-region rewrite plus
    // regenerateChunksInRadius() (CPU mesh rebuild + GPU buffer replace),
    // so applying once per rendered frame at an uncapped frame rate is
    // needless GPU churn. Strength is scaled by the *real* elapsed time
    // since the last applied mutation (not a fixed per-tick amount), so
    // holding the button for one second moves the surface by
    // approximately brushStrength_ total regardless of frame rate or
    // throttle interval -- the same total effect as one old single-click
    // application, just spread continuously over the hold instead of
    // jumping there in one frame.
    constexpr float kMaxLiveApplyRateHz = 60.0f;
    constexpr float kMinLiveApplyInterval = 1.0f / kMaxLiveApplyRateHz;
    constexpr float kLiveSculptRatePerSecond = 1.0f;

    liveApplyAccumTime_ += deltaTime;
    if (liveApplyAccumTime_ < kMinLiveApplyInterval) return;

    float elapsed = liveApplyAccumTime_;
    liveApplyAccumTime_ = 0.0f;

    float scaledStrength = brushStrength_ * elapsed * kLiveSculptRatePerSecond;
    applyBrushCore(worldX, worldZ, ecs, scaledStrength);
}

void TerrainEditorPlugin::endLiveStroke() {
    if (!strokeActive_) return;
    strokeActive_ = false;

    if (brushMode_ == BrushMode::Paint) return; // Paint stays outside undo tracking, same as applyBrush()'s single-click path

    std::vector<float> before = std::move(strokeBeforeSnapshot_);
    std::vector<float> after = terrain_.heightSnapshot();
    if (before == after) return; // real no-op stroke (e.g. clicked off-terrain the whole drag) shouldn't clutter undo history

    core::Terrain* terrainPtr = &terrain_;
    undoStack_->push({"Terrain Sculpt",
                       [terrainPtr, before]() { terrainPtr->restoreHeightSnapshot(before); },
                       [terrainPtr, after]() { terrainPtr->restoreHeightSnapshot(after); }});
}

} // namespace engine::studio::plugins

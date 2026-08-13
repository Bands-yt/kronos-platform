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
    ImGui::TextDisabled("Brush applies at the primary selection's position --");
    ImGui::TextDisabled("move something there with the gizmo, then click Apply.");

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

void TerrainEditorPlugin::applyBrush(float worldX, float worldZ, core::ECS& ecs) {
    // Sprint 9 ("Creator Tools Phase 1") task category 1's "undo/redo
    // support" -- real, for the five height-mutating brushes (Raise/
    // Lower/Smooth/Flatten/Noise): a full heightmap snapshot before the
    // brush applies, another after, captured by value in the real
    // UndoStack::Command this pushes. Paint mutates per-chunk
    // Renderable::baseColor, not the heightmap, so it's deliberately not
    // covered by this snapshot -- see README's Known Issues.
    if (brushMode_ == BrushMode::Paint) {
        terrain_.paint(worldX, worldZ, brushRadius_,
                        glm::vec4(paintColor_[0], paintColor_[1], paintColor_[2], paintColor_[3]), ecs);
        return;
    }

    std::vector<float> before = terrain_.heightSnapshot();
    switch (brushMode_) {
        case BrushMode::Raise:
            terrain_.raise(worldX, worldZ, brushRadius_, brushStrength_, falloffPower_);
            break;
        case BrushMode::Lower:
            terrain_.lower(worldX, worldZ, brushRadius_, brushStrength_, falloffPower_);
            break;
        case BrushMode::Smooth:
            terrain_.smooth(worldX, worldZ, brushRadius_, brushStrength_, falloffPower_);
            break;
        case BrushMode::Flatten:
            terrain_.flatten(worldX, worldZ, brushRadius_, brushStrength_, falloffPower_);
            break;
        case BrushMode::Noise:
            terrain_.addNoise(worldX, worldZ, brushRadius_, brushStrength_, noiseFrequency_, falloffPower_);
            break;
        case BrushMode::Paint:
            break; // handled above
    }
    std::vector<float> after = terrain_.heightSnapshot();

    core::Terrain* terrainPtr = &terrain_;
    undoStack_->push({"Terrain Brush",
                       [terrainPtr, before]() { terrainPtr->restoreHeightSnapshot(before); },
                       [terrainPtr, after]() { terrainPtr->restoreHeightSnapshot(after); }});
}

} // namespace engine::studio::plugins

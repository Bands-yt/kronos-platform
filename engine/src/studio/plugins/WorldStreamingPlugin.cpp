#include "studio/plugins/WorldStreamingPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {

ImU32 cellColor(core::WorldCellState state) {
    switch (state) {
        case core::WorldCellState::Loaded: return IM_COL32(96, 200, 120, 200);
        case core::WorldCellState::Loading: return IM_COL32(235, 190, 80, 200);
        case core::WorldCellState::Failed: return IM_COL32(230, 90, 90, 220);
        case core::WorldCellState::Unloaded: break;
    }
    return IM_COL32(110, 110, 120, 140);
}

} // namespace

void WorldStreamingPlugin::drawPanel(core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    drawPluginHeader("World Streaming");

    if (!streamer_->isOpen()) {
        ImGui::TextWrapped("This scene loads all at once. Splitting it into cells lets big worlds load only what is near "
                           "the player.");
        ImGui::DragFloat("Cell size (m)", &settings_.cellSize, 1.0f, 8.0f, 1024.0f, "%.0f");
        ImGui::DragFloat("Load radius (m)", &settings_.loadRadius, 1.0f, 0.0f, 4096.0f, "%.0f");
        ImGui::DragFloat("Unload radius (m)", &settings_.unloadRadius, 1.0f, 0.0f, 4096.0f, "%.0f");
        settings_.unloadRadius = std::max(settings_.unloadRadius, settings_.loadRadius);
        int candidates = 0;
        for (auto entity : ecs.view<core::Transform, core::Name>()) {
            if (core::WorldStreamer::adoptable(ecs, entity, settings_.cellSize)) ++candidates;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%d objects can move into cells. Scripted, moving and very large objects stay in the scene.",
                           candidates);
        ImGui::PopStyleColor();
        if (ImGui::Button("Split Scene Into Cells")) {
            std::string error;
            if (createWorld_(settings_, error)) {
                const size_t moved = streamer_->adopt(ecs);
                if (markDirty_) markDirty_();
                status_ = "Moved " + std::to_string(moved) + " objects into " +
                          std::to_string(streamer_->manifest().cells.size()) + " cells. Save the scene to write them.";
            } else {
                status_ = error;
            }
        }
        drawPluginFooter(status_.c_str());
        return;
    }

    const core::WorldStreamingStats stats = streamer_->stats();
    ImGui::Text("%zu cells: %zu loaded, %zu loading, %zu failed", stats.cells, stats.loaded, stats.loading, stats.failed);
    ImGui::Text("%zu streamed objects in the scene", stats.entities);
    if (stats.editedCells > 0) ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "%zu unloaded cells have unsaved edits", stats.editedCells);

    float load = streamer_->manifest().loadRadius;
    float unload = streamer_->manifest().unloadRadius;
    bool radiiChanged = ImGui::DragFloat("Load radius (m)", &load, 1.0f, 0.0f, 4096.0f, "%.0f");
    radiiChanged |= ImGui::DragFloat("Unload radius (m)", &unload, 1.0f, 0.0f, 4096.0f, "%.0f");
    if (radiiChanged) {
        streamer_->setRadii(load, unload);
        if (markDirty_) markDirty_();
    }
    bool loadAll = streamer_->loadAll();
    if (ImGui::Checkbox("Load every cell", &loadAll)) streamer_->setLoadAll(loadAll);
    ImGui::SameLine();
    ImGui::TextDisabled("(cell size %.0f m)", streamer_->manifest().cellSize);

    if (ImGui::Button("Move New Objects Into Cells")) {
        const size_t moved = streamer_->adopt(ecs);
        if (moved > 0 && markDirty_) markDirty_();
        status_ = "Moved " + std::to_string(moved) + " objects into cells.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Cells")) {
        std::string error;
        status_ = streamer_->saveCells(&error) ? "Cells saved." : "Save failed: " + error;
    }

    ImGui::SliderFloat("Map zoom", &mapZoom_, 0.25f, 4.0f, "%.2fx");
    drawMap();
    drawPluginFooter(status_.c_str());
}

void WorldStreamingPlugin::drawMap() {
    const core::WorldManifest& manifest = streamer_->manifest();
    const float width = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    const float height = std::min(width, 320.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("world_map", ImVec2(width, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(20, 22, 26, 255));
    draw->PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);

    const glm::vec3 focus = camera_->position;
    const float metersAcross = std::max(manifest.unloadRadius * 2.5f, manifest.cellSize * 4.0f) / mapZoom_;
    const float scale = width / metersAcross;
    const ImVec2 center(origin.x + width * 0.5f, origin.y + height * 0.5f);
    auto toScreen = [&](float x, float z) { return ImVec2(center.x + (x - focus.x) * scale, center.y + (z - focus.z) * scale); };

    for (size_t i = 0; i < manifest.cells.size(); ++i) {
        const auto& cell = manifest.cells[i];
        const float x = static_cast<float>(cell.coord.x) * manifest.cellSize;
        const float z = static_cast<float>(cell.coord.z) * manifest.cellSize;
        const ImVec2 a = toScreen(x, z);
        const ImVec2 b = toScreen(x + manifest.cellSize, z + manifest.cellSize);
        draw->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), cellColor(streamer_->cellState(i)));
        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            if (mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y) {
                ImGui::SetTooltip("Cell %d, %d\n%u objects\n%s", cell.coord.x, cell.coord.z, cell.entities, cell.file.c_str());
            }
        }
    }
    draw->AddCircle(center, manifest.loadRadius * scale, IM_COL32(120, 220, 140, 200), 64, 1.5f);
    draw->AddCircle(center, manifest.unloadRadius * scale, IM_COL32(220, 160, 90, 160), 64, 1.0f);
    draw->AddCircleFilled(center, 4.0f, IM_COL32(255, 255, 255, 255));
    draw->PopClipRect();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Green: loaded. Yellow: loading. Grey: unloaded. Circles: load and unload radius around the camera.");
    ImGui::PopStyleColor();
}

} // namespace engine::studio::plugins

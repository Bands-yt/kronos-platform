#include "studio/plugins/AlignPlugin.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "core/Components.hpp"

namespace engine::studio::plugins {

namespace {
float axisValue(const glm::vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }
void setAxisValue(glm::vec3& v, int axis, float value) {
    if (axis == 0) v.x = value;
    else if (axis == 1) v.y = value;
    else v.z = value;
}
} // namespace

void AlignPlugin::alignAxis(core::ECS& ecs, const std::vector<core::EntityId>& selection, core::EntityId primary, int axis) {
    auto* primaryTransform = ecs.tryGetComponent<core::Transform>(primary);
    if (primaryTransform == nullptr) return;
    float target = axisValue(primaryTransform->position, axis);

    for (core::EntityId entity : selection) {
        if (entity == primary) continue;
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            setAxisValue(transform->position, axis, target);
        }
    }
}

void AlignPlugin::distributeAxis(core::ECS& ecs, const std::vector<core::EntityId>& selection, int axis) {
    std::vector<core::EntityId> sorted = selection;
    std::sort(sorted.begin(), sorted.end(), [&](core::EntityId a, core::EntityId b) {
        auto* ta = ecs.tryGetComponent<core::Transform>(a);
        auto* tb = ecs.tryGetComponent<core::Transform>(b);
        float va = ta != nullptr ? axisValue(ta->position, axis) : 0.0f;
        float vb = tb != nullptr ? axisValue(tb->position, axis) : 0.0f;
        return va < vb;
    });
    if (sorted.size() < 3) return;

    auto* firstTransform = ecs.tryGetComponent<core::Transform>(sorted.front());
    auto* lastTransform = ecs.tryGetComponent<core::Transform>(sorted.back());
    if (firstTransform == nullptr || lastTransform == nullptr) return;

    float lo = axisValue(firstTransform->position, axis);
    float hi = axisValue(lastTransform->position, axis);
    size_t n = sorted.size();
    for (size_t i = 1; i + 1 < n; ++i) {
        if (auto* transform = ecs.tryGetComponent<core::Transform>(sorted[i])) {
            float f = static_cast<float>(i) / static_cast<float>(n - 1);
            setAxisValue(transform->position, axis, lo + (hi - lo) * f);
        }
    }
}

void AlignPlugin::snapToGrid(core::ECS& ecs, const std::vector<core::EntityId>& selection) const {
    float grid = std::max(gridSize_, 0.001f);
    for (core::EntityId entity : selection) {
        if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
            transform->position.x = std::round(transform->position.x / grid) * grid;
            transform->position.y = std::round(transform->position.y / grid) * grid;
            transform->position.z = std::round(transform->position.z / grid) * grid;
        }
    }
}

void AlignPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) {
    ImGui::Begin("Align & Distribute");

    ImGui::Text("Selected: %zu %s", selectedEntities.size(), selectedEntities.size() == 1 ? "entity" : "entities");
    ImGui::TextDisabled("Explorer: ctrl-click to add to selection, shift-click to range-select.");

    ImGui::Separator();
    ImGui::TextUnformatted("Align To Primary Selection");
    bool canAlign = selectedEntities.size() >= 2 && selected != core::kNullEntity;
    ImGui::BeginDisabled(!canAlign);
    if (ImGui::Button("Align X")) alignAxis(ecs, selectedEntities, selected, 0);
    ImGui::SameLine();
    if (ImGui::Button("Align Y")) alignAxis(ecs, selectedEntities, selected, 1);
    ImGui::SameLine();
    if (ImGui::Button("Align Z")) alignAxis(ecs, selectedEntities, selected, 2);
    ImGui::EndDisabled();
    if (!canAlign) ImGui::TextDisabled("Select 2+ entities (primary = last clicked).");

    ImGui::Separator();
    ImGui::TextUnformatted("Distribute Evenly");
    bool canDistribute = selectedEntities.size() >= 3;
    ImGui::BeginDisabled(!canDistribute);
    if (ImGui::Button("Distribute X")) distributeAxis(ecs, selectedEntities, 0);
    ImGui::SameLine();
    if (ImGui::Button("Distribute Y")) distributeAxis(ecs, selectedEntities, 1);
    ImGui::SameLine();
    if (ImGui::Button("Distribute Z")) distributeAxis(ecs, selectedEntities, 2);
    ImGui::EndDisabled();
    if (!canDistribute) ImGui::TextDisabled("Select 3+ entities -- the two extremes stay fixed, the rest space out between them.");

    ImGui::Separator();
    ImGui::TextUnformatted("Snap To Grid");
    ImGui::DragFloat("Grid Size", &gridSize_, 0.05f, 0.05f, 100.0f);
    ImGui::BeginDisabled(selectedEntities.empty());
    if (ImGui::Button("Snap Selected")) snapToGrid(ecs, selectedEntities);
    ImGui::EndDisabled();

    ImGui::End();
}

} // namespace engine::studio::plugins

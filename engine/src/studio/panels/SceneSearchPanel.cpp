#include "studio/panels/SceneSearchPanel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/Components.hpp"
#include "core/ParticleSystem.hpp"

namespace engine::studio::panels {

namespace {
std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
} // namespace

core::EntityId SceneSearchPanel::draw(core::ECS& ecs, core::EntityId currentSelection) {
    ImGui::Begin("Scene Search");

    // Result count lives right under the box that produced it (not after
    // scrolling past the whole list) -- the number a user actually wants
    // to see the moment they finish typing, not after hunting for it.
    float clearButtonWidth = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##filter", "Search by name...", filterText_, sizeof(filterText_));
    ImGui::SameLine();
    ImGui::BeginDisabled(filterText_[0] == '\0');
    if (ImGui::Button("Clear")) filterText_[0] = '\0';
    ImGui::EndDisabled();

    ImGui::Checkbox("Has Renderable", &requireRenderable_);
    ImGui::SameLine();
    ImGui::Checkbox("Has RigidBody", &requireRigidBody_);
    ImGui::SameLine();
    ImGui::Checkbox("Has ParticleEmitter", &requireParticleEmitter_);

    core::EntityId clicked = core::kNullEntity;
    std::string filterLower = toLower(filterText_);
    size_t matchCount = 0;
    size_t totalCount = 0;

    struct Match {
        core::EntityId entity;
        std::string name;
        bool hasRenderable;
        bool hasRigidBody;
        bool hasParticleEmitter;
    };
    std::vector<Match> matches;

    for (auto entity : ecs.view<core::Transform>()) {
        ++totalCount;
        bool hasRenderable = ecs.hasComponent<core::Renderable>(entity);
        bool hasRigidBody = ecs.hasComponent<core::RigidBody>(entity);
        bool hasParticleEmitter = ecs.hasComponent<core::ParticleEmitter>(entity);
        if (requireRenderable_ && !hasRenderable) continue;
        if (requireRigidBody_ && !hasRigidBody) continue;
        if (requireParticleEmitter_ && !hasParticleEmitter) continue;

        std::string name;
        if (const auto* n = ecs.tryGetComponent<core::Name>(entity); n != nullptr && !n->value.empty()) {
            name = n->value;
        } else {
            name = "Entity " + std::to_string(static_cast<uint32_t>(entity));
        }

        if (!filterLower.empty() && toLower(name).find(filterLower) == std::string::npos) continue;
        matches.push_back({entity, std::move(name), hasRenderable, hasRigidBody, hasParticleEmitter});
    }
    matchCount = matches.size();

    ImGui::TextDisabled("%zu of %zu entit%s", matchCount, totalCount, totalCount == 1 ? "y" : "ies");
    ImGui::Separator();

    ImGui::BeginChild("results");
    if (matches.empty() && totalCount > 0) {
        if (filterLower.empty()) {
            ImGui::TextDisabled("No entities match the selected component filters.");
        } else {
            ImGui::TextDisabled("No entities match \"%s\".", filterText_);
        }
    }
    for (const Match& match : matches) {
        bool isSelected = match.entity == currentSelection;
        char label[224];
        // A compact component badge (R/B/P for Renderable/RigidBody/
        // ParticleEmitter) so a result carries more information than its
        // name alone at a glance, without a full icon per row.
        std::snprintf(label, sizeof(label), "%s  %s%s%s##%u", match.name.c_str(), match.hasRenderable ? "[R]" : "",
                      match.hasRigidBody ? "[B]" : "", match.hasParticleEmitter ? "[P]" : "",
                      static_cast<uint32_t>(match.entity));
        if (ImGui::Selectable(label, isSelected)) {
            clicked = match.entity;
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return clicked;
}

} // namespace engine::studio::panels

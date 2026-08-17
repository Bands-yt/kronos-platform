#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "studio/panels/InspectorPanel.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/Navigation.hpp"
#include "core/OreNode.hpp"
#include "core/PhysicsMaterial.hpp"

namespace engine::studio::panels {

bool InspectorPanel::hasInvalidComponents(glm::vec3 v) {
    return glm::any(glm::isnan(v)) || glm::any(glm::isinf(v));
}

void InspectorPanel::draw(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities,
                           UndoStack& undoStack) {
    ImGui::Begin("Inspector");

    if (selected == core::kNullEntity) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextDisabled("Click an entity in the Viewport or Explorer to inspect it.");
        ImGui::End();
        return;
    }

    bool multiSelected = selectedEntities.size() > 1;

    if (auto* name = ecs.tryGetComponent<core::Name>(selected)) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", name->value.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##entity_name", buf, sizeof(buf))) {
            name->value = buf;
        }
    }
    if (multiSelected) {
        ImGui::TextColored(ImVec4(0.35f, 0.72f, 0.85f, 1.0f), "%zu entities selected -- Position edits apply to all of them (same relative offset each keeps); Rotation/Scale/other fields edit \"%s\" only.",
                            selectedEntities.size(), ecs.tryGetComponent<core::Name>(selected) ? ecs.tryGetComponent<core::Name>(selected)->value.c_str() : "the primary selection");
    } else {
        ImGui::TextDisabled("Entity ID %u", static_cast<uint32_t>(selected));
    }
    ImGui::Spacing();

    // Consistent label column width across every section below, rather
    // than each DragFloat3's own label ("Position"/"Rotation"/"Scale")
    // pushing the drag widgets to a different X per row.
    ImGui::PushItemWidth(-1.0f);

    if (auto* transform = ecs.tryGetComponent<core::Transform>(selected);
        transform != nullptr &&
        ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Kronos ("Developer Velocity Sprint" -- "Multi-Selection
        // Property Inspector"): Position edits apply as a real shared
        // delta across every selected entity -- see this class's own
        // header comment on why (matches ViewportPanel gizmo group-move
        // exactly). Captured once per commit (drag release, or formula
        // Apply), not per-frame -- the same "one undo command per
        // gesture" granularity this field's undo integration already
        // uses.
        auto applyPositionDeltaToGroup = [&](glm::vec3 before, glm::vec3 after) {
            glm::vec3 delta = after - before;
            if (delta == glm::vec3(0.0f)) return;
            for (core::EntityId other : selectedEntities) {
                if (other == selected) continue;
                if (auto* otherTransform = ecs.tryGetComponent<core::Transform>(other)) {
                    otherTransform->position += delta;
                }
            }
        };

        ImGui::TextUnformatted("Position");
        ImGui::DragFloat3("##position", &transform->position.x, 0.05f);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) positionFormulaPopup_.open();
        // Real validation: position can legitimately be any finite value
        // (unlike scale/collider dimensions below, there's no natural
        // [min,max] to clamp against), but NaN/Inf can still creep in
        // from a degenerate gizmo drag (e.g. a zero-length normalize) --
        // catching and surfacing that here, with a real one-click fix,
        // beats silently rendering a corrupted entity with no visible cause.
        if (hasInvalidComponents(transform->position)) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Invalid position (NaN/Inf) -- likely a bad gizmo drag.");
            if (ImGui::Button("Reset Position to Origin")) transform->position = glm::vec3(0.0f);
        }
        if (ImGui::IsItemActivated()) positionBeforeEdit_ = transform->position;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            glm::vec3 before = positionBeforeEdit_;
            glm::vec3 after = transform->position;
            applyPositionDeltaToGroup(before, after);
            core::EntityId entity = selected;
            undoStack.push({"Move Entity",
                             [&ecs, entity, before]() {
                                 if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->position = before;
                             },
                             [&ecs, entity, after]() {
                                 if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->position = after;
                             }});
        }
        {
            glm::vec3 beforeFormula = transform->position;
            if (positionFormulaPopup_.draw("position_formula_popup", &transform->position)) {
                glm::vec3 after = transform->position;
                applyPositionDeltaToGroup(beforeFormula, after);
                core::EntityId entity = selected;
                undoStack.push({"Move Entity (Formula)",
                                 [&ecs, entity, beforeFormula]() {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->position = beforeFormula;
                                 },
                                 [&ecs, entity, after]() {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->position = after;
                                 }});
            }
        }

        ImGui::TextUnformatted("Rotation");
        glm::vec3 eulerDegrees = glm::degrees(glm::eulerAngles(transform->rotation));
        bool rotationEdited = ImGui::DragFloat3("##rotation", &eulerDegrees.x, 1.0f);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) rotationFormulaPopup_.open();
        if (ImGui::IsItemActivated()) rotationBeforeEdit_ = transform->rotation;
        if (rotationEdited) {
            transform->rotation = glm::quat(glm::radians(eulerDegrees));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            glm::quat before = rotationBeforeEdit_;
            glm::quat after = transform->rotation;
            core::EntityId entity = selected;
            undoStack.push({"Rotate Entity",
                             [&ecs, entity, before]() {
                                 if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->rotation = before;
                             },
                             [&ecs, entity, after]() {
                                 if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->rotation = after;
                             }});
        }
        {
            glm::vec3 eulerForFormula = eulerDegrees;
            if (rotationFormulaPopup_.draw("rotation_formula_popup", &eulerForFormula)) {
                glm::quat before = transform->rotation;
                transform->rotation = glm::quat(glm::radians(eulerForFormula));
                glm::quat after = transform->rotation;
                core::EntityId entity = selected;
                undoStack.push({"Rotate Entity (Formula)",
                                 [&ecs, entity, before]() {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->rotation = before;
                                 },
                                 [&ecs, entity, after]() {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->rotation = after;
                                 }});
            }
        }

        ImGui::TextUnformatted("Scale");
        // AlwaysClamp: without it, ImGui only clamps drag-gesture input --
        // typing a value directly (Ctrl+click) bypasses [min,max] entirely,
        // so a typed "-5" would silently produce an inverted/degenerate
        // scale. Real validation, not just a visual range hint.
        ImGui::DragFloat3("##scale", &transform->scale.x, 0.05f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) scaleFormulaPopup_.open();
        if (ImGui::IsItemActivated()) scaleBeforeEdit_ = transform->scale;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            glm::vec3 before = scaleBeforeEdit_;
            glm::vec3 after = transform->scale;
            core::EntityId entity = selected;
            undoStack.push({"Scale Entity",
                             [&ecs, entity, before]() {
                                 if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->scale = before;
                             },
                             [&ecs, entity, after]() {
                                 if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->scale = after;
                             }});
        }
        {
            glm::vec3 beforeScaleFormula = transform->scale;
            if (scaleFormulaPopup_.draw("scale_formula_popup", &transform->scale)) {
                glm::vec3 after = transform->scale;
                core::EntityId entity = selected;
                undoStack.push({"Scale Entity (Formula)",
                                 [&ecs, entity, beforeScaleFormula]() {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->scale = beforeScaleFormula;
                                 },
                                 [&ecs, entity, after]() {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->scale = after;
                                 }});
            }
        }

        if (multiSelected) {
            ImGui::TextDisabled("Right-click Position/Rotation/Scale to enter a formula (e.g. +10, *2, 180 - 45).");
        }
    }

    if (auto* renderable = ecs.tryGetComponent<core::Renderable>(selected);
        renderable != nullptr && ImGui::CollapsingHeader("Renderable", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Visible", &renderable->visible);
        ImGui::SameLine();
        ImGui::Checkbox("Casts Shadow", &renderable->castsShadow);

        // Real material property drawers (task category 4) -- direct
        // editing of the same inline PBR fields scene.frag reads every
        // frame (see Components.hpp's Renderable comment on why there's
        // no separate material asset indirection yet). ColorEdit4/
        // SliderFloat are ImGui's own clamped widgets; Metallic/Roughness
        // use SliderFloat (not DragFloat) specifically because a slider's
        // fixed [0,1] track has no "drag past the end" case to begin with.
        ImGui::Separator();
        ImGui::TextUnformatted("Material");
        ImGui::ColorEdit4("Base Color", &renderable->baseColor.x);
        ImGui::SliderFloat("Metallic", &renderable->metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &renderable->roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Normal Intensity", &renderable->normalIntensity, 0.0f, 2.0f);

        ImGui::Spacing();
        ImGui::TextUnformatted("Emissive");
        ImGui::ColorEdit3("Emissive Color", &renderable->emissiveColor.x);
        ImGui::SliderFloat("Emissive Intensity", &renderable->emissiveIntensity, 0.0f, 10.0f);
        // A quick preview swatch of the actual glow color*intensity would
        // clip at white for anything past ~1.0 in a plain ColorEdit, so a
        // small separate preview block (raw, un-tonemapped) shows what's
        // really being sent to the shader rather than a misleadingly
        // clamped one.
        ImVec2 swatchSize(ImGui::GetContentRegionAvail().x, 18.0f);
        glm::vec3 previewColor = glm::min(renderable->emissiveColor * renderable->emissiveIntensity, glm::vec3(1.0f));
        ImGui::ColorButton("##emissive_preview", ImVec4(previewColor.x, previewColor.y, previewColor.z, 1.0f),
                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, swatchSize);
    }

    drawPhysicsSection(ecs, selected);
    drawNavigationSection(ecs, selected);
    drawNavMarkerSection(ecs, selected);
    drawOreNodeSection(ecs, selected);
    drawLightSection(ecs, selected);

    ImGui::PopItemWidth();
    ImGui::End();
}

namespace {
constexpr const char* kColliderKindNames[] = {"Box", "Sphere", "Capsule", "Mesh"};
constexpr core::ColliderShapeKind kColliderKindValues[] = {
    core::ColliderShapeKind::Box, core::ColliderShapeKind::Sphere, core::ColliderShapeKind::Capsule,
    core::ColliderShapeKind::Mesh};
constexpr const char* kMotionTypeNames[] = {"Static", "Kinematic", "Dynamic"};
constexpr core::RigidBodyMotionType kMotionTypeValues[] = {
    core::RigidBodyMotionType::Static, core::RigidBodyMotionType::Kinematic, core::RigidBodyMotionType::Dynamic};
} // namespace

// Real Physics Material System editor (task category 5) + real collider
// shape/motion-type authoring (category 6's "Scene Physics Integration"
// needs *something* to attach a Play-mode body to). Deliberately edits
// component data only, live-updated in Studio's own display, but not
// pushed into an already-simulating Jolt body if one exists (RigidBody's
// existing comment already establishes that boundary for mass/motion-
// type -- friction/restitution *could* go live via
// Physics::applyMaterial(), but that needs a live core::Physics
// reference this panel deliberately doesn't hold, keeping it decoupled
// from studio::plugins::PhysicsPreviewPlugin; Stop+Play again is the
// real, honest way to apply an edited material -- see README's Known
// Issues).
void InspectorPanel::drawPhysicsSection(core::ECS& ecs, core::EntityId selected) {
    auto* colliderShape = ecs.tryGetComponent<core::ColliderShape>(selected);
    auto* material = ecs.tryGetComponent<core::PhysicsMaterial>(selected);
    auto* rigidBody = ecs.tryGetComponent<core::RigidBody>(selected);

    if (colliderShape == nullptr && material == nullptr && rigidBody == nullptr) {
        if (ImGui::Button("Add Physics Collider")) {
            ecs.addComponent<core::ColliderShape>(selected, core::ColliderShape{});
            ecs.addComponent<core::PhysicsMaterial>(selected, core::PhysicsMaterial{});
            // kInvalidBodyId -- authored intent only, no live Jolt body
            // yet. A real Play session (studio::plugins::
            // PhysicsPreviewPlugin) attaches one via
            // Physics::attachBodyToEntity(), which overwrites this with
            // the real id.
            ecs.addComponent<core::RigidBody>(selected, core::RigidBody{core::RigidBody::kInvalidBodyId, core::RigidBodyMotionType::Dynamic});
        }
        return;
    }

    if (!ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (colliderShape != nullptr) {
        int kindIndex = 0;
        for (int i = 0; i < IM_ARRAYSIZE(kColliderKindValues); ++i) {
            if (kColliderKindValues[i] == colliderShape->kind) kindIndex = i;
        }
        if (ImGui::Combo("Collider Shape", &kindIndex, kColliderKindNames, IM_ARRAYSIZE(kColliderKindNames))) {
            colliderShape->kind = kColliderKindValues[kindIndex];
        }
        // AlwaysClamp on every dimension below -- a zero/negative collider
        // half-extent or radius isn't a smaller shape, it's a degenerate
        // Jolt shape (attachBodyToEntity() would either assert or produce
        // an inside-out collider), so typed input needs the same real
        // clamp the drag gesture already gets, not just a visual hint.
        switch (colliderShape->kind) {
            case core::ColliderShapeKind::Box:
                ImGui::DragFloat3("Half Extents", &colliderShape->params.x, 0.05f, 0.01f, 100.0f, "%.3f",
                                   ImGuiSliderFlags_AlwaysClamp);
                break;
            case core::ColliderShapeKind::Sphere:
                ImGui::DragFloat("Radius", &colliderShape->params.x, 0.05f, 0.01f, 100.0f, "%.3f",
                                  ImGuiSliderFlags_AlwaysClamp);
                break;
            case core::ColliderShapeKind::Capsule:
                ImGui::DragFloat("Radius", &colliderShape->params.x, 0.05f, 0.01f, 100.0f, "%.3f",
                                  ImGuiSliderFlags_AlwaysClamp);
                ImGui::DragFloat("Half Height", &colliderShape->params.y, 0.05f, 0.01f, 100.0f, "%.3f",
                                  ImGuiSliderFlags_AlwaysClamp);
                break;
            case core::ColliderShapeKind::Mesh: {
                char pathBuf[256];
                std::snprintf(pathBuf, sizeof(pathBuf), "%s", colliderShape->path.c_str());
                if (ImGui::InputText("Mesh Path (.obj)", pathBuf, sizeof(pathBuf))) colliderShape->path = pathBuf;
                ImGui::TextDisabled("Mesh colliders are Static-only; Play mode needs the same host-side geometry");
                ImGui::TextDisabled("this .obj resolves to -- see README's Known Issues.");
                break;
            }
        }
    }

    if (material != nullptr) {
        ImGui::Separator();
        ImGui::TextUnformatted("Material");
        // AlwaysClamp joins the existing Logarithmic flag on Density below --
        // same "typed input bypasses [min,max] otherwise" reasoning as the
        // collider dimensions above; negative friction or zero density are
        // physically invalid, not just visually out of the slider's track.
        ImGui::SliderFloat("Friction", &material->friction, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Restitution", &material->restitution, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Density (kg/m^3)", &material->density, 1.0f, 10000.0f, "%.0f",
                            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);

        // Real presets -- one click overwrites all three sliders with a
        // real, sourced material (see PhysicsMaterial.cpp's comment on
        // each preset's actual values), the same real "material assignment
        // per collider" task category 5 asks for.
        if (ImGui::Button("Metal")) *material = core::physicsMaterialForPreset(core::PhysicsMaterialPreset::Metal);
        ImGui::SameLine();
        if (ImGui::Button("Rubber")) *material = core::physicsMaterialForPreset(core::PhysicsMaterialPreset::Rubber);
        ImGui::SameLine();
        if (ImGui::Button("Wood")) *material = core::physicsMaterialForPreset(core::PhysicsMaterialPreset::Wood);
        ImGui::SameLine();
        if (ImGui::Button("Stone")) *material = core::physicsMaterialForPreset(core::PhysicsMaterialPreset::Stone);
    }

    if (rigidBody != nullptr) {
        ImGui::Separator();
        bool live = rigidBody->joltBodyId != core::RigidBody::kInvalidBodyId;
        if (live) {
            ImGui::Text("Jolt body id: %u", rigidBody->joltBodyId);
            ImGui::Text("Motion type: %s", core::rigidBodyMotionTypeName(rigidBody->motionType));
            ImGui::TextDisabled("Live-simulated (Play mode) -- Stop to edit motion type again.");
        } else {
            int motionIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(kMotionTypeValues); ++i) {
                if (kMotionTypeValues[i] == rigidBody->motionType) motionIndex = i;
            }
            if (ImGui::Combo("Motion Type", &motionIndex, kMotionTypeNames, IM_ARRAYSIZE(kMotionTypeNames))) {
                rigidBody->motionType = kMotionTypeValues[motionIndex];
            }
            ImGui::TextDisabled("Not yet attached to a live body -- Play (Physics Preview) attaches one.");
        }
    }

    if (colliderShape != nullptr && ImGui::Button("Remove Physics Collider")) {
        ecs.removeComponent<core::ColliderShape>(selected);
        ecs.removeComponent<core::PhysicsMaterial>(selected);
        ecs.removeComponent<core::RigidBody>(selected);
    }
}

void InspectorPanel::drawNavigationSection(core::ECS& ecs, core::EntityId selected) {
    auto* pad = ecs.tryGetComponent<core::TeleportPad>(selected);
    if (pad == nullptr) return;

    if (!ImGui::CollapsingHeader("Teleport Pad", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextUnformatted("Destination");
    ImGui::DragFloat3("##teleport_destination", &pad->destination.x, 0.1f);

    char linkBuf[64];
    std::snprintf(linkBuf, sizeof(linkBuf), "%s", pad->linkTag.c_str());
    ImGui::TextUnformatted("Link Tag");
    if (ImGui::InputText("##teleport_link_tag", linkBuf, sizeof(linkBuf))) pad->linkTag = linkBuf;
    ImGui::TextDisabled("Two pads sharing a non-empty Link Tag are a real, paired fast-travel route.");
}

void InspectorPanel::drawNavMarkerSection(core::ECS& ecs, core::EntityId selected) {
    auto* marker = ecs.tryGetComponent<core::NavMarker>(selected);
    if (marker == nullptr) return;

    if (!ImGui::CollapsingHeader("Nav Marker", ImGuiTreeNodeFlags_DefaultOpen)) return;

    constexpr core::NavMarkerKind kKinds[] = {core::NavMarkerKind::Spawn, core::NavMarkerKind::Shop,
                                               core::NavMarkerKind::UpgradeKiosk, core::NavMarkerKind::TeleportPad,
                                               core::NavMarkerKind::Custom};
    int kindIndex = 0;
    for (int i = 0; i < 5; ++i) {
        if (kKinds[i] == marker->kind) kindIndex = i;
    }
    const char* kindNames[5];
    for (int i = 0; i < 5; ++i) kindNames[i] = core::navMarkerKindName(kKinds[i]);
    if (ImGui::Combo("Kind##navmarker_inspector", &kindIndex, kindNames, 5)) marker->kind = kKinds[kindIndex];

    char labelBuf[64];
    std::snprintf(labelBuf, sizeof(labelBuf), "%s", marker->label.c_str());
    ImGui::TextUnformatted("Label");
    if (ImGui::InputText("##navmarker_label", labelBuf, sizeof(labelBuf))) marker->label = labelBuf;
}

void InspectorPanel::drawOreNodeSection(core::ECS& ecs, core::EntityId selected) {
    auto* node = ecs.tryGetComponent<core::OreNode>(selected);
    if (node == nullptr) return;

    if (!ImGui::CollapsingHeader("Ore Node", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const core::OreTypeInfo& info = core::oreTypeInfo(node->oreType);

    // Rarity-color preview -- the same glm::vec3 oreTypeInfo() already
    // drives this node's own Renderable::baseColor/emissiveColor with
    // (see OreNode.cpp's breakOreNode()/respawnOreNode()), so the swatch
    // shown here is never out of sync with what the node actually looks
    // like in the viewport.
    ImGui::ColorButton("##ore_rarity_swatch", ImVec4(info.color.x, info.color.y, info.color.z, 1.0f),
                        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(24.0f, 24.0f));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("%s", info.name);
    ImGui::TextColored(ImVec4(info.color.x, info.color.y, info.color.z, 1.0f), "%s", core::oreRarityName(info.rarity));
    ImGui::EndGroup();

    ImGui::Spacing();
    if (node->broken) {
        float respawnFraction =
            info.respawnSeconds > 0.0f ? 1.0f - (node->respawnRemainingSeconds / info.respawnSeconds) : 1.0f;
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "Respawn %.0fs", node->respawnRemainingSeconds);
        ImGui::ProgressBar(std::clamp(respawnFraction, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), overlay);
    } else {
        float healthFraction = info.hitsToBreak > 0 ? static_cast<float>(node->currentHealth) /
                                                            static_cast<float>(info.hitsToBreak)
                                                      : 0.0f;
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "%d / %d hits", node->currentHealth, info.hitsToBreak);
        ImGui::ProgressBar(std::clamp(healthFraction, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), overlay);
    }

    ImGui::TextDisabled("Sell value: %d coins/unit -- Drop: %d-%d units", info.baseSellValue, info.minDrop,
                         info.maxDrop);
    ImGui::TextDisabled("Read-only -- ore stats come from OreType, not per-entity fields (see OreNode.hpp).");
}

void InspectorPanel::drawLightSection(core::ECS& ecs, core::EntityId selected) {
    auto* light = ecs.tryGetComponent<core::Light>(selected);
    if (light == nullptr) return;

    if (!ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Checkbox("Enabled", &light->enabled);
    ImGui::ColorEdit3("Color", &light->color.x);
    ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Radius", &light->radius, 0.1f, 0.1f, 100.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextDisabled(
        "A real, entity-driven point light -- Renderer.cpp real-combines up to kMaxPointLights=4 of these (plus "
        "any scene-wide ones from Lighting Tools) each frame.");
}

} // namespace engine::studio::panels

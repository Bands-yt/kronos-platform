#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "studio/panels/InspectorPanel.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/Audio.hpp"
#include "core/Navigation.hpp"
#include "core/OreNode.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/ResourceManager.hpp"
#include "core/SceneTypes.hpp"
#include "core/UIWidgets.hpp"
#include "studio/FileBrowse.hpp"
#include "studio/StudioIcons.hpp"

namespace engine::studio::panels {

bool InspectorPanel::hasInvalidComponents(glm::vec3 v) {
    return glm::any(glm::isnan(v)) || glm::any(glm::isinf(v));
}

void InspectorPanel::draw(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities,
                           UndoStack& undoStack) {
    ImGui::Begin("Inspector");

    if (selected == core::kNullEntity) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 start = ImGui::GetCursorPos();
        const float iconSize = 36.0f;
        ImGui::SetCursorPos(ImVec2(start.x + (avail.x - iconSize) * 0.5f, start.y + std::max(12.0f, avail.y * 0.3f)));
        const ImVec2 iconMin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(iconSize, iconSize));
        const ImVec4 accent = ui::accent();
        ImGui::GetWindowDrawList()->AddRectFilled(iconMin, ImVec2(iconMin.x + iconSize, iconMin.y + iconSize),
                                                  ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.14f)), 9.0f);
        drawIcon(ImGui::GetWindowDrawList(), Icon::Prop, ImVec2(iconMin.x + iconSize * 0.5f, iconMin.y + iconSize * 0.5f), 20.0f,
                 ImGui::GetColorU32(accent));
        const char* heading = "Nothing selected";
        ImGui::SetCursorPosX(start.x + std::max(0.0f, (avail.x - ImGui::CalcTextSize(heading).x) * 0.5f));
        ImGui::TextUnformatted(heading);
        ImGui::PushTextWrapPos(start.x + avail.x - 12.0f);
        ImGui::SetCursorPosX(start.x + 12.0f);
        ImGui::TextDisabled("Pick an entity in the Viewport or Explorer to edit its properties.");
        ImGui::PopTextWrapPos();
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
        if (ImGui::IsItemActivated()) {
            positionBeforeEdit_ = transform->position;
            positionGroupBeforeEdit_.clear();
            for (core::EntityId other : selectedEntities) {
                if (auto* otherTransform = ecs.tryGetComponent<core::Transform>(other)) {
                    positionGroupBeforeEdit_.emplace_back(other, otherTransform->position);
                }
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            glm::vec3 before = positionBeforeEdit_;
            glm::vec3 after = transform->position;
            applyPositionDeltaToGroup(before, after);
            // Snapshot every group member's *after* position too (not just
            // delta + before), so undo/redo restores the whole group exactly
            // -- not just the primary selection -- matching what
            // applyPositionDeltaToGroup() actually just did live.
            std::vector<std::pair<core::EntityId, glm::vec3>> groupAfter;
            for (const auto& [entity, beforePos] : positionGroupBeforeEdit_) {
                if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) groupAfter.emplace_back(entity, t->position);
            }
            std::vector<std::pair<core::EntityId, glm::vec3>> groupBefore = positionGroupBeforeEdit_;
            undoStack.push({"Move Entity",
                             [&ecs, groupBefore]() {
                                 for (const auto& [entity, pos] : groupBefore) {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->position = pos;
                                 }
                             },
                             [&ecs, groupAfter]() {
                                 for (const auto& [entity, pos] : groupAfter) {
                                     if (auto* t = ecs.tryGetComponent<core::Transform>(entity)) t->position = pos;
                                 }
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
        // Same "snapshot on activate, push on deactivate" undo integration
        // as the Transform fields above -- these had none at all before,
        // so a material edit couldn't be undone (Ctrl+Z would silently do
        // nothing, or undo an unrelated earlier transform edit instead).
        core::EntityId materialEntity = selected;
        ImGui::ColorEdit4("Base Color", &renderable->baseColor.x);
        if (ImGui::IsItemActivated()) baseColorBeforeEdit_ = renderable->baseColor;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            glm::vec4 before = baseColorBeforeEdit_;
            glm::vec4 after = renderable->baseColor;
            undoStack.push({"Edit Base Color",
                             [&ecs, materialEntity, before]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->baseColor = before;
                             },
                             [&ecs, materialEntity, after]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->baseColor = after;
                             }});
        }
        ImGui::SliderFloat("Metallic", &renderable->metallic, 0.0f, 1.0f);
        if (ImGui::IsItemActivated()) metallicBeforeEdit_ = renderable->metallic;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            float before = metallicBeforeEdit_;
            float after = renderable->metallic;
            undoStack.push({"Edit Metallic",
                             [&ecs, materialEntity, before]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->metallic = before;
                             },
                             [&ecs, materialEntity, after]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->metallic = after;
                             }});
        }
        ImGui::SliderFloat("Roughness", &renderable->roughness, 0.0f, 1.0f);
        if (ImGui::IsItemActivated()) roughnessBeforeEdit_ = renderable->roughness;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            float before = roughnessBeforeEdit_;
            float after = renderable->roughness;
            undoStack.push({"Edit Roughness",
                             [&ecs, materialEntity, before]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->roughness = before;
                             },
                             [&ecs, materialEntity, after]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->roughness = after;
                             }});
        }
        ImGui::SliderFloat("Normal Intensity", &renderable->normalIntensity, 0.0f, 2.0f);
        if (ImGui::IsItemActivated()) normalIntensityBeforeEdit_ = renderable->normalIntensity;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            float before = normalIntensityBeforeEdit_;
            float after = renderable->normalIntensity;
            undoStack.push({"Edit Normal Intensity",
                             [&ecs, materialEntity, before]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->normalIntensity = before;
                             },
                             [&ecs, materialEntity, after]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->normalIntensity = after;
                             }});
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Emissive");
        ImGui::ColorEdit3("Emissive Color", &renderable->emissiveColor.x);
        if (ImGui::IsItemActivated()) emissiveColorBeforeEdit_ = renderable->emissiveColor;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            glm::vec3 before = emissiveColorBeforeEdit_;
            glm::vec3 after = renderable->emissiveColor;
            undoStack.push({"Edit Emissive Color",
                             [&ecs, materialEntity, before]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->emissiveColor = before;
                             },
                             [&ecs, materialEntity, after]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->emissiveColor = after;
                             }});
        }
        ImGui::SliderFloat("Emissive Intensity", &renderable->emissiveIntensity, 0.0f, 10.0f);
        if (ImGui::IsItemActivated()) emissiveIntensityBeforeEdit_ = renderable->emissiveIntensity;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            float before = emissiveIntensityBeforeEdit_;
            float after = renderable->emissiveIntensity;
            undoStack.push({"Edit Emissive Intensity",
                             [&ecs, materialEntity, before]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->emissiveIntensity = before;
                             },
                             [&ecs, materialEntity, after]() {
                                 if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->emissiveIntensity = after;
                             }});
        }
        // A quick preview swatch of the actual glow color*intensity would
        // clip at white for anything past ~1.0 in a plain ColorEdit, so a
        // small separate preview block (raw, un-tonemapped) shows what's
        // really being sent to the shader rather than a misleadingly
        // clamped one.
        ImVec2 swatchSize(ImGui::GetContentRegionAvail().x, 18.0f);
        glm::vec3 previewColor = glm::min(renderable->emissiveColor * renderable->emissiveIntensity, glm::vec3(1.0f));
        ImGui::ColorButton("##emissive_preview", ImVec4(previewColor.x, previewColor.y, previewColor.z, 1.0f),
                            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, swatchSize);

        ImGui::Spacing();
        if (ImGui::TreeNode("Material Layers")) {
            core::MaterialLayers& m = renderable->layers;
            auto trackUndo = [&](const char* label) {
                if (ImGui::IsItemActivated()) layersBeforeEdit_ = m;
                if (!ImGui::IsItemDeactivatedAfterEdit()) return;
                core::MaterialLayers before = layersBeforeEdit_;
                core::MaterialLayers after = m;
                undoStack.push({label,
                                 [&ecs, materialEntity, before]() {
                                     if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->layers = before;
                                 },
                                 [&ecs, materialEntity, after]() {
                                     if (auto* r = ecs.tryGetComponent<core::Renderable>(materialEntity)) r->layers = after;
                                 }});
            };
            ImGui::SliderFloat("Specular", &m.specular, 0.0f, 1.0f);
            trackUndo("Edit Specular");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dielectric reflectance: F0 = 0.16 * specular^2 (0.5 = 4%%).");
            ImGui::SliderFloat("Clearcoat", &m.clearcoat, 0.0f, 1.0f);
            trackUndo("Edit Clearcoat");
            ImGui::SliderFloat("Clearcoat Roughness", &m.clearcoatRoughness, 0.0f, 1.0f);
            trackUndo("Edit Clearcoat Roughness");
            ImGui::ColorEdit3("Sheen Color", &m.sheenColor.x);
            trackUndo("Edit Sheen Color");
            ImGui::SliderFloat("Sheen Roughness", &m.sheenRoughness, 0.0f, 1.0f);
            trackUndo("Edit Sheen Roughness");
            ImGui::SliderFloat("Anisotropy", &m.anisotropy, -1.0f, 1.0f);
            trackUndo("Edit Anisotropy");
            ImGui::SliderAngle("Anisotropy Rotation", &m.anisotropyRotation, -180.0f, 180.0f);
            trackUndo("Edit Anisotropy Rotation");
            ImGui::TreePop();
        }
    }

    drawPhysicsSection(ecs, selected);
    drawNavigationSection(ecs, selected);
    drawNavMarkerSection(ecs, selected);
    drawOreNodeSection(ecs, selected);
    drawLightSection(ecs, selected);
    drawSoundSection(ecs, selected, undoStack);

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
    ImGui::TextDisabled("Two pads with the same Link Tag form a fast-travel route.");
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
    ImGui::TextDisabled("Read-only -- ore stats are shared per ore type, not editable per-entity.");
}

void InspectorPanel::drawLightSection(core::ECS& ecs, core::EntityId selected) {
    auto* light = ecs.tryGetComponent<core::Light>(selected);
    if (light == nullptr) return;

    if (!ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Checkbox("Enabled", &light->enabled);
    ImGui::ColorEdit3("Color", &light->color.x);
    ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Radius", &light->radius, 0.1f, 0.1f, 100.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    int type = static_cast<int>(light->type);
    const char* types[] = {"Point", "Spot"};
    if (ImGui::Combo("Type", &type, types, IM_ARRAYSIZE(types))) light->type = static_cast<core::LightType>(type);
    if (light->type == core::LightType::Spot) {
        ImGui::DragFloat("Inner Cone", &light->innerConeDegrees, 0.25f, 0.0f, 89.0f, "%.1f deg",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat("Outer Cone", &light->outerConeDegrees, 0.25f, 0.0f, 89.0f, "%.1f deg",
                         ImGuiSliderFlags_AlwaysClamp);
        light->innerConeDegrees = std::min(light->innerConeDegrees, light->outerConeDegrees);
        ImGui::Checkbox("Cast Shadows", &light->castsShadow);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Up to %u shadowed spots are rendered per view, nearest visible first.",
                              core::kMaxShadowedSpotLights);
        }
    }
    ImGui::TextDisabled(
        "Shaded through the clustered light list, so there is no per-scene light cap. Spots aim down the entity's "
        "-Z axis.");
}

namespace {

void loadSoundFile(core::ECS& ecs, core::EntityId entity, core::ResourceManager* resources) {
    auto* sound = ecs.tryGetComponent<core::AudioSource>(entity);
    if (sound == nullptr) return;
    auto* refs = ecs.tryGetComponent<core::ResourceRefs>(entity);
    if (refs != nullptr) {
        std::erase_if(refs->handles, [&](const core::ResourceHandle& handle) {
            return handle.kind() == core::ResourceKind::Audio && handle.get() == sound->soundHandle;
        });
    }
    sound->soundHandle = core::AudioSource::kInvalidHandle;
    if (sound->path.empty() || resources == nullptr || !resources->hasLoader(core::ResourceKind::Audio)) return;
    core::ResourceHandle handle = resources->acquire(core::ResourceKind::Audio, sound->path);
    sound->soundHandle = handle.get();
    ecs.raw().get_or_emplace<core::ResourceRefs>(entity).handles.push_back(std::move(handle));
}

const core::ResourceHandle* soundResource(core::ECS& ecs, core::EntityId entity, uint32_t soundHandle) {
    auto* refs = ecs.tryGetComponent<core::ResourceRefs>(entity);
    if (refs == nullptr) return nullptr;
    for (const core::ResourceHandle& handle : refs->handles) {
        if (handle.kind() == core::ResourceKind::Audio && handle.get() == soundHandle) return &handle;
    }
    return nullptr;
}

} // namespace

void InspectorPanel::drawSoundSection(core::ECS& ecs, core::EntityId selected, UndoStack& undoStack) {
    auto* sound = ecs.tryGetComponent<core::AudioSource>(selected);
    core::ResourceManager* resources = resources_;
    auto setSound = [&ecs, selected, resources](const core::AudioSource& value) {
        auto* target = ecs.tryGetComponent<core::AudioSource>(selected);
        if (target == nullptr) return;
        const bool reload = target->path != value.path;
        const uint32_t handle = target->soundHandle;
        *target = value;
        target->soundHandle = handle;
        target->playing = false;
        if (reload) loadSoundFile(ecs, selected, resources);
    };

    if (sound == nullptr) {
        ImGui::Spacing();
        if (ImGui::Button("Add Sound")) {
            ecs.addComponent<core::AudioSource>(selected);
            undoStack.push({"Add Sound", [&ecs, selected]() { ecs.removeComponent<core::AudioSource>(selected); },
                            [&ecs, selected]() { ecs.addComponent<core::AudioSource>(selected); }});
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lets this object play a sound file.");
        return;
    }
    if (!ImGui::CollapsingHeader("Sound", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto track = [&](const char* label) {
        if (ImGui::IsItemActivated()) soundBeforeEdit_ = *sound;
        if (!ImGui::IsItemDeactivatedAfterEdit()) return;
        const core::AudioSource before = soundBeforeEdit_;
        const core::AudioSource after = *sound;
        undoStack.push({label, [setSound, before]() { setSound(before); }, [setSound, after]() { setSound(after); }});
    };
    auto commit = [&](const char* label, const core::AudioSource& before) {
        const core::AudioSource after = *sound;
        undoStack.push({label, [setSound, before]() { setSound(before); }, [setSound, after]() { setSound(after); }});
    };

    if (soundPathEntity_ != selected || (!ImGui::IsAnyItemActive() && sound->path != soundPath_)) {
        std::snprintf(soundPath_, sizeof(soundPath_), "%s", sound->path.c_str());
        soundPathEntity_ = selected;
    }
    auto labelAbove = [](const char* text) {
        ImGui::TextUnformatted(text);
        ImGui::SetNextItemWidth(-FLT_MIN);
    };
    ImGui::TextUnformatted("File");
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Browse...").x -
                                                ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputText("##soundfile", soundPath_, sizeof(soundPath_));
    bool pathChanged = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    pathChanged |= browseButton("sound", soundPath_, sizeof(soundPath_),
                                {"Choose a Sound", {"*.wav", "*.mp3", "*.ogg", "*.flac"}, "Sound files"});
    if (pathChanged && sound->path != soundPath_) {
        const core::AudioSource before = *sound;
        sound->path = soundPath_;
        loadSoundFile(ecs, selected, resources_);
        sound = ecs.tryGetComponent<core::AudioSource>(selected);
        commit("Change Sound File", before);
    }
    if (sound->path.empty()) {
        ImGui::TextDisabled("Pick a .wav, .mp3, .ogg or .flac file.");
    } else if (resources_ == nullptr || audio_ == nullptr || !audio_->isInitialized()) {
        ImGui::TextDisabled("No audio device, so sounds can't be heard here.");
    } else if (const core::ResourceHandle* handle = soundResource(ecs, selected, sound->soundHandle)) {
        if (handle->state() == core::ResourceState::Failed) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Couldn't load: %s", handle->error().c_str());
        } else if (handle->state() == core::ResourceState::Loading) {
            ImGui::TextDisabled("Loading...");
        }
    } else if (sound->soundHandle == core::AudioSource::kInvalidHandle) {
        loadSoundFile(ecs, selected, resources_);
        sound = ecs.tryGetComponent<core::AudioSource>(selected);
    }

    labelAbove("Volume");
    ImGui::SliderFloat("##soundvolume", &sound->volume, 0.0f, 2.0f, "%.2f");
    track("Edit Sound Volume");
    labelAbove("Pitch");
    ImGui::SliderFloat("##soundpitch", &sound->pitch, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    track("Edit Sound Pitch");
    ImGui::Checkbox("Loop", &sound->looping);
    track("Toggle Sound Loop");
    ImGui::Checkbox("Play when the game starts", &sound->playOnStart);
    track("Toggle Play On Start");
    ImGui::Checkbox("3D (quieter further away)", &sound->spatial);
    track("Toggle 3D Sound");
    if (sound->spatial) {
        labelAbove("Full volume within");
        ImGui::DragFloat("##soundmin", &sound->minDistance, 0.1f, 0.1f, 500.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
        track("Edit Sound Distance");
        labelAbove("Fades out by");
        ImGui::DragFloat("##soundmax", &sound->maxDistance, 0.5f, sound->minDistance, 5000.0f, "%.1f m",
                         ImGuiSliderFlags_AlwaysClamp);
        track("Edit Sound Distance");
    }

    std::vector<std::string> buses;
    if (audio_ != nullptr) {
        for (const core::MixerBus& bus : audio_->mixer().config().buses) buses.push_back(bus.name);
    }
    const std::string shown = !sound->bus.empty() ? sound->bus : sound->category == core::AudioCategory::Music ? "Music" : "SFX";
    labelAbove("Mixer bus");
    if (ImGui::BeginCombo("##soundbus", shown.c_str())) {
        for (const std::string& name : buses) {
            if (ImGui::Selectable(name.c_str(), name == shown) && name != shown) {
                const core::AudioSource before = *sound;
                sound->bus = name;
                sound->category = name == "Music" ? core::AudioCategory::Music : core::AudioCategory::SFX;
                commit("Change Sound Bus", before);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which group of the mixer this sound goes through (Audio > Mixer).");

    const bool canPreview = audio_ != nullptr && audio_->isInitialized() && sound->soundHandle != core::AudioSource::kInvalidHandle;
    ImGui::BeginDisabled(!canPreview);
    if (ImGui::Button("Preview")) {
        audio_->setSoundSpatialized(sound->soundHandle, false);
        audio_->setSoundVolume(sound->soundHandle, sound->volume);
        audio_->setSoundPitch(sound->soundHandle, sound->pitch);
        audio_->setSoundLooping(sound->soundHandle, false);
        audio_->setSoundBus(sound->soundHandle, shown);
        audio_->playFromOffset(sound->soundHandle, 0.0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) audio_->stopSound(sound->soundHandle);
    ImGui::EndDisabled();
    if (ImGui::Button("Remove Sound")) {
        const core::AudioSource before = *sound;
        if (canPreview) audio_->stopSound(sound->soundHandle);
        ecs.removeComponent<core::AudioSource>(selected);
        undoStack.push({"Remove Sound",
                        [&ecs, selected, before, resources]() {
                            ecs.addComponent<core::AudioSource>(selected, before);
                            loadSoundFile(ecs, selected, resources);
                        },
                        [&ecs, selected]() { ecs.removeComponent<core::AudioSource>(selected); }});
    }
}

} // namespace engine::studio::panels

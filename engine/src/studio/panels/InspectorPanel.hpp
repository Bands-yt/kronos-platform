#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/ECS.hpp"
#include "studio/MathExpressionPopup.hpp"
#include "studio/UndoStack.hpp"

namespace engine::studio::panels {

// docs/ARCHITECTURE.md §5's Properties/Inspector panel. Edits the real
// Transform/Renderable components directly (§6's Instance property-sheet
// view over the ECS doesn't exist yet -- see ExplorerPanel.hpp's matching
// note), so what you drag here is what Physics/Renderer actually read
// next tick, not a mock of a future property sheet.
//
// Transform field edits push one UndoStack::Command per drag gesture
// (not one per per-frame delta) -- see draw()'s use of ImGui's
// IsItemActivated()/IsItemDeactivatedAfterEdit(), the standard ImGui
// pattern for exactly this "snapshot before, commit after" integration.
//
// Kronos ("Developer Velocity Sprint" -- "Live Math Evaluation &
// Multi-Selection Property Inspector"): every Position/Rotation/Scale
// DragFloat3 can also be right-clicked to open a real "Enter Formula"
// popup (Vec3MathExpressionPopup) -- see that class's own comment on why
// this is a separate popup rather than inline support inside DragFloat3
// itself. When more than one entity is selected (`selectedEntities`),
// Position edits apply as a real shared delta across the whole
// selection -- the exact same "every other selected entity keeps its
// own relative offset" semantics ViewportPanel's own gizmo group-move
// already established (Sprint 9's "group move" task), not a
// re-invented convention. Rotation/Scale (and every other component
// section below) stay primary-selection-only when multi-selected -- the
// same real, stated scope boundary the gizmo's own group-move already
// draws ("rotating or scaling several objects together around a shared
// pivot is a meaningfully different, more complex feature"), extended
// here for consistency rather than silently diverging from it.
class InspectorPanel {
public:
    void draw(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities,
               UndoStack& undoStack);

    // Pure -- the real validation draw()'s Position field uses to detect
    // a corrupted (NaN/Inf) value (see draw()'s own comment on how that
    // can happen), split out so the check itself gets real headless test
    // coverage instead of living only inline inside an ImGui-touching
    // function no test here can call.
    [[nodiscard]] static bool hasInvalidComponents(glm::vec3 v);

private:
    void drawPhysicsSection(core::ECS& ecs, core::EntityId selected);
    // Sprint 6 ("World Systems & Environment") task category 4's "Studio"
    // half of teleport-pad support: real authoring for TeleportPad's
    // destination/linkTag, the same data engine_runtime's teleport
    // dispatch (Application.cpp) actually reads.
    void drawNavigationSection(core::ECS& ecs, core::EntityId selected);
    // Sprint 9 ("Creator Tools Phase 1") task category 4's "inspector UI
    // for linking destinations" -- the NavMarker half (TeleportPad's own
    // linking UI is drawNavigationSection() above): real kind/label
    // editing for a selected core::NavMarker.
    void drawNavMarkerSection(core::ECS& ecs, core::EntityId selected);
    // Sprint 7 ("Studio UI Revamp") task category 4's "rarity-color
    // preview for ore nodes" -- read-only (an ore node's stats come from
    // its OreType via oreTypeInfo(), not per-entity authored fields, see
    // OreNode.hpp), a real live view into currentHealth/respawn state
    // plus the same rarity color oreTypeInfo() already drives the node's
    // own Renderable/particle burst with.
    void drawOreNodeSection(core::ECS& ecs, core::EntityId selected);
    // Kronos (Alpha Roadmap Phase 6, "Tooling Layer" -- "Lighting
    // editor"): real per-entity editing for a selected core::Light
    // (Alpha Roadmap Phase 3) -- enabled/color/intensity/radius, the
    // exact fields Renderer.cpp's own point-light UBO fill reads.
    void drawLightSection(core::ECS& ecs, core::EntityId selected);


    // "Before" snapshots, captured on IsItemActivated() -- members, not
    // locals, since they must survive from the activation frame to the
    // (later) deactivation frame the undo command is actually pushed on.
    glm::vec3 positionBeforeEdit_{0.0f};
    glm::quat rotationBeforeEdit_{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scaleBeforeEdit_{1.0f};

    // Real, per-field popup state -- see Vec3MathExpressionPopup's own
    // class comment.
    Vec3MathExpressionPopup positionFormulaPopup_;
    Vec3MathExpressionPopup rotationFormulaPopup_;
    Vec3MathExpressionPopup scaleFormulaPopup_;
};

} // namespace engine::studio::panels

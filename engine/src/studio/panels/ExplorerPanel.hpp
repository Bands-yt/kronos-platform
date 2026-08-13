#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "core/ECS.hpp"
#include "studio/EntityClassification.hpp"

namespace engine::studio::panels {

// docs/ARCHITECTURE.md §5's Explorer panel: "Instance tree." Kronos Alpha
// Roadmap Phase 2 ("Scene system") added the first real Parent/Children
// relationship to the ECS (core::Hierarchy, see core/Hierarchy.hpp) --
// this panel now real-walks it: rows are grouped into the same
// EntityCategory "folders" as before, but only entities with no real
// parent (or whose parent lies outside this ECS's own Transform view) are
// drawn as a category's own root rows; every entity with a real parent is
// drawn nested under that parent's own row instead, wherever it lives --
// exactly the "data-source swap, not a rewrite of the panel's selection/
// drawing logic" this header used to promise once a real Parent concept
// existed. An entity with no Hierarchy component at all (the overwhelming
// majority, since parenting is opt-in) draws identically to before this
// change: a flat leaf row directly under its category header.
//
// Multi-select (ctrl-click to toggle, shift-click to range-select against
// the last click) is additive on top of the original single-select model:
// selectedEntity() keeps meaning exactly what it always did -- the primary/
// last-clicked entity -- so InspectorPanel and ViewportPanel's ImGuizmo
// integration (both single-entity, already verified working) needed zero
// changes. Plugins that operate over a whole selection (the Align plugin)
// read selectedEntities() instead.
//
// Real drag-and-drop reparenting: drag any row onto another row to
// core::hierarchy::setParent() the dragged entity under it (rejected
// silently -- no state change -- if that would self-parent or close a
// cycle, exactly as core::hierarchy::setParent() itself promises). Each
// row's right-click context menu offers "Unparent" real-wired to
// core::hierarchy::unparent(), disabled when the entity is already a
// root.
class ExplorerPanel {
public:
    void draw(core::ECS& ecs);

    // Pure -- one step of the real per-category collapse-fade convergence
    // draw() uses (see its own implementation comment), split out so the
    // animation math itself gets real headless test coverage instead of
    // living only inline inside an ImGui-touching function no test here
    // can call. `current`/`target` are both in [0,1]; `rate` matches
    // draw()'s own kAnimationRate semantics (higher = snappier).
    [[nodiscard]] static float animateOpenAmount(float current, float target, float dt, float rate);

    [[nodiscard]] core::EntityId selectedEntity() const { return selected_; }
    [[nodiscard]] const std::vector<core::EntityId>& selectedEntities() const { return selection_; }

    // Sets the primary selection to exactly `entity` (single-entity,
    // replacing any multi-select) -- for other panels that want to "jump
    // to" an entity (see panels/SceneSearchPanel.hpp) without duplicating
    // Explorer's selection state. entity == kNullEntity clears selection.
    void setSelected(core::EntityId entity) {
        selected_ = entity;
        selection_.clear();
        if (entity != core::kNullEntity) selection_.push_back(entity);
    }

    // Ctrl-click semantics for a selection source outside Explorer's own
    // tree (see studio/panels/ViewportPanel.hpp's click-to-select):  adds
    // `entity` to the multi-select if absent, removes it if present. The
    // just-added/still-present entity becomes the new primary selection;
    // removing the primary selection falls back to whatever's now last in
    // the remaining set (or kNullEntity if the set is now empty).
    void toggleSelection(core::EntityId entity) {
        auto it = std::find(selection_.begin(), selection_.end(), entity);
        if (it != selection_.end()) {
            selection_.erase(it);
            selected_ = selection_.empty() ? core::kNullEntity : selection_.back();
        } else {
            selection_.push_back(entity);
            selected_ = entity;
        }
    }

    // Drag-select-box semantics: replaces the whole selection with
    // `entities`. Primary selection becomes entities.back() (or
    // kNullEntity if empty), the same "last selected wins" convention
    // setSelected() uses for a single entity.
    void setSelectedMultiple(std::vector<core::EntityId> entities) {
        selection_ = std::move(entities);
        selected_ = selection_.empty() ? core::kNullEntity : selection_.back();
    }

private:
    // Real recursive tree-row draw -- one call per visible entity node
    // (root or nested child). `rowCursor` is the shared, pre-order DFS
    // row index (see draw()'s own comment on why it must stay in lockstep
    // with `rows`' construction order for shift-click range-select to
    // stay correct even with per-node collapse); `rows` is the full
    // flattened DFS order of every real entity in the tree (built once,
    // upfront, before any drawing -- exactly like the pre-Hierarchy flat
    // list this replaced), used only for the shift-click range lookup.
    void drawEntityNode(core::ECS& ecs, core::EntityId entity, int& rowCursor, const std::vector<core::EntityId>& rows);

    core::EntityId selected_ = core::kNullEntity; // primary selection -- last entity clicked, any modifier
    std::vector<core::EntityId> selection_;        // full multi-select set, always contains selected_ when non-null
    int lastClickedRow_ = -1;                       // row index for shift-click range anchoring

    // Sprint 7 ("Studio UI Revamp") task category 3: real, per-category
    // collapse animation state -- one real, lerped "how open is this
    // group right now" float per EntityCategory (0 = fully collapsed,
    // 1 = fully open), ticked every draw() call toward whichever way
    // ImGui's own tree-node open/closed state currently points. See
    // draw()'s own implementation comment for exactly how this drives a
    // real fade rather than an instant show/hide.
    static constexpr size_t kCategoryCount = 6; // must match EntityCategory's real enumerator count
    std::array<float, kCategoryCount> categoryOpenAmount_{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace engine::studio::panels

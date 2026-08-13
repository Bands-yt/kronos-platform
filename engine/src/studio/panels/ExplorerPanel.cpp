#include "studio/panels/ExplorerPanel.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "core/Hierarchy.hpp"
#include "studio/MaterialPresets.hpp"
#include "studio/StudioIcons.hpp"

namespace engine::studio::panels {

namespace {
// Draws `icon` in a small, fixed-width area immediately before whatever
// widget is laid out next (a category header, a row's Selectable),
// tinted by `categoryColor` -- the real per-row/per-group icon Sprint
// 7's task category 2 asks for ("hierarchy icons for all core object
// types" + "color-coded categories"). Leaves the cursor positioned for
// a same-line label to follow.
void drawInlineIcon(Icon icon, IconCategoryColor categoryColor) {
    constexpr float kIconSize = 15.0f;
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 center(cursor.x + kIconSize * 0.5f, cursor.y + ImGui::GetTextLineHeight() * 0.5f);
    ImU32 color = ImGui::GetColorU32(ImVec4(categoryColor.r, categoryColor.g, categoryColor.b, 1.0f));
    drawIcon(drawList, icon, center, kIconSize, color);
    ImGui::Dummy(ImVec2(kIconSize + 4.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, 4.0f);
}

// Real, honest root check -- an entity with no Hierarchy component at all
// (the overwhelming majority) or one whose Hierarchy::parent is
// kNullEntity is its own category-level root; anything else is drawn
// nested under its real parent's own row instead (see drawEntityNode()),
// never as a second top-level entry under its category header.
bool isTreeRoot(core::ECS& ecs, core::EntityId entity) {
    auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity);
    return !hierarchy || hierarchy->parent == core::kNullEntity;
}

// Defensive bound on recursion depth, mirroring core::hierarchy's own
// kMaxWalkDepth (Hierarchy.cpp) -- setParent() already rejects cycles at
// insertion time, so this is defense-in-depth against a corrupted/
// hand-edited scene file, not a claim that cycles are otherwise reachable
// through this panel.
constexpr int kMaxTreeDrawDepth = 256;

// Real, full pre-order DFS flattening of every entity that will ever be
// drawn (roots then their real descendants, recursively) -- built once,
// upfront, so shift-click range-select has a stable row index to look up
// against regardless of which nodes happen to be collapsed this frame
// (exactly the same "rows built once, drawing skips some of them" split
// the old flat-list version already used for collapsed *categories*, now
// extended to collapsed *tree nodes* too).
void collectRowsRecursive(core::ECS& ecs, core::EntityId entity, int depth, std::vector<core::EntityId>& rows) {
    rows.push_back(entity);
    if (depth >= kMaxTreeDrawDepth) return;
    if (auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity)) {
        for (core::EntityId child : hierarchy->children) collectRowsRecursive(ecs, child, depth + 1, rows);
    }
}
} // namespace

float ExplorerPanel::animateOpenAmount(float current, float target, float dt, float rate) {
    float next = current + (target - current) * std::clamp(dt * rate, 0.0f, 1.0f);
    if (std::fabs(next - target) < 0.01f) next = target;
    return next;
}

void ExplorerPanel::drawEntityNode(core::ECS& ecs, core::EntityId entity, int& rowCursor,
                                    const std::vector<core::EntityId>& rows) {
    int row = rowCursor++;

    auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity);
    bool hasChildren = hierarchy && !hierarchy->children.empty();

    EntityCategory category = classifyEntity(ecs, entity);
    Icon icon = iconForCategory(category);
    IconCategoryColor color = iconCategoryColor(icon);
    if (category != EntityCategory::Other) drawInlineIcon(icon, color);

    const core::Name* nameComponent = ecs.tryGetComponent<core::Name>(entity);
    char label[64];
    if (nameComponent && !nameComponent->value.empty()) {
        std::snprintf(label, sizeof(label), "%s##%u", nameComponent->value.c_str(), static_cast<uint32_t>(entity));
    } else {
        std::snprintf(label, sizeof(label), "Entity %u", static_cast<uint32_t>(entity));
    }

    // Leaf rows (no real children -- still the overwhelming majority,
    // since parenting is opt-in) use Leaf|NoTreePushOnOpen|Bullet so they
    // align under the same arrow-column indentation branch rows use,
    // rather than a plain Selectable that would sit one indent narrower
    // than its parented siblings -- the real reason every row now goes
    // through TreeNodeEx instead of the old leaf-only Selectable path.
    bool isSelected = std::find(selection_.begin(), selection_.end(), entity) != selection_.end();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    bool open = ImGui::TreeNodeEx(label, flags);

    // A click that toggled open/closed (the arrow, or double-click) isn't
    // a selection action -- matches how every real tree-view editor keeps
    // "expand" and "select" as separate gestures.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyShift && lastClickedRow_ >= 0) {
            // Range-select against the last clicked row, replacing
            // whatever was selected before -- matches how Explorer-
            // style shift-click works in most editors (not additive
            // with ctrl-click's toggle behavior).
            int lo = std::min(lastClickedRow_, row);
            int hi = std::max(lastClickedRow_, row);
            selection_.clear();
            for (int r = lo; r <= hi; ++r) selection_.push_back(rows[static_cast<size_t>(r)]);
            selected_ = entity;
        } else if (io.KeyCtrl) {
            auto it = std::find(selection_.begin(), selection_.end(), entity);
            if (it != selection_.end()) {
                selection_.erase(it);
                selected_ = selection_.empty() ? core::kNullEntity : selection_.back();
            } else {
                selection_.push_back(entity);
                selected_ = entity;
            }
            lastClickedRow_ = row;
        } else {
            selection_.clear();
            selection_.push_back(entity);
            selected_ = entity;
            lastClickedRow_ = row;
        }
    }

    // Real drag-and-drop reparenting -- see this class's own header
    // comment. setParent() itself validates (rejects self-parent/cycles),
    // so this drop handler just forwards the request honestly.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("EXPLORER_ENTITY", &entity, sizeof(core::EntityId));
        ImGui::Text("%s", label);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EXPLORER_ENTITY")) {
            core::EntityId dragged = *static_cast<const core::EntityId*>(payload->Data);
            if (dragged != entity) core::hierarchy::setParent(ecs, dragged, entity);
        }
        // Kronos (Alpha Completion Checklist, "Editor UX Polish" --
        // "Stable drag-and-drop in Asset Browser"): the real drop-target
        // half -- studio::plugins::CreatorAssetBrowserPlugin's own
        // drawMaterialEntries() is the drag source, carrying an index
        // into the same real, shared kMaterialPresets table this applies
        // from. A real, honest no-op if this row's entity has no
        // Renderable to apply a material to.
        if (const ImGuiPayload* materialPayload = ImGui::AcceptDragDropPayload("ASSET_MATERIAL_PRESET")) {
            int presetIndex = *static_cast<const int*>(materialPayload->Data);
            if (presetIndex >= 0 && presetIndex < studio::kMaterialPresetCount) {
                if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) {
                    const studio::MaterialPresetInfo& preset = studio::kMaterialPresets[presetIndex];
                    renderable->baseColor = preset.baseColor;
                    renderable->metallic = preset.metallic;
                    renderable->roughness = preset.roughness;
                    renderable->emissiveColor = preset.emissiveColor;
                    renderable->emissiveIntensity = preset.emissiveIntensity;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        bool hasParent = hierarchy && hierarchy->parent != core::kNullEntity;
        if (!hasParent) ImGui::BeginDisabled();
        if (ImGui::MenuItem("Unparent")) core::hierarchy::unparent(ecs, entity);
        if (!hasParent) ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    if (hasChildren) {
        if (open) {
            for (core::EntityId child : hierarchy->children) drawEntityNode(ecs, child, rowCursor, rows);
            ImGui::TreePop();
        } else {
            // Collapsed -- rowCursor must still advance past every real
            // descendant (they're still in `rows`, just not drawn this
            // frame) so shift-click range-select stays correct once
            // re-expanded, exactly the same invariant the old flat-list
            // version preserved for a collapsed category.
            for (core::EntityId child : hierarchy->children) {
                std::vector<core::EntityId> subtree;
                collectRowsRecursive(ecs, child, 0, subtree);
                rowCursor += static_cast<int>(subtree.size());
            }
        }
    }
}

void ExplorerPanel::draw(core::ECS& ecs) {
    ImGui::Begin("Explorer");

    // Every entity has a Transform (ECS::createEntity always adds one),
    // so that's the "list every entity" view -- see header comment on why
    // this isn't a full Instance tree yet, even though it's now a real
    // Parent/Children tree over whatever entities do exist.
    //
    // Sprint 7: rows are grouped by real EntityCategory
    // (EntityClassification.hpp) -- built by iterating categories in a
    // fixed, meaningful order (Terrain -> Prop -> Physics -> Economy ->
    // Navigation -> Other). Alpha Roadmap Phase 2: only entities with no
    // real parent become a category's own root rows (see isTreeRoot());
    // everything else is drawn nested under its real parent instead,
    // recursively, regardless of which category its parent (or it
    // itself) classifies into -- Parent always wins over category
    // grouping, matching every real editor's tree behavior.
    constexpr EntityCategory kCategoryOrder[ExplorerPanel::kCategoryCount] = {
        EntityCategory::Terrain, EntityCategory::Prop,       EntityCategory::Physics,
        EntityCategory::Economy, EntityCategory::Navigation, EntityCategory::Other,
    };

    std::array<std::vector<core::EntityId>, ExplorerPanel::kCategoryCount> groupedRoots;
    for (auto entity : ecs.view<core::Transform>()) {
        if (!isTreeRoot(ecs, entity)) continue;
        size_t categoryIndex = static_cast<size_t>(classifyEntity(ecs, entity));
        groupedRoots[categoryIndex].push_back(entity);
    }

    // Full pre-order DFS flattening, built once upfront exactly like the
    // pre-Hierarchy version's flat `rows` list -- see collectRowsRecursive()'s
    // own comment for why this must stay independent of any node's
    // current collapsed/expanded state.
    std::vector<core::EntityId> rows;
    for (EntityCategory category : kCategoryOrder) {
        for (core::EntityId root : groupedRoots[static_cast<size_t>(category)]) collectRowsRecursive(ecs, root, 0, rows);
    }

    int rowCursor = 0;
    for (EntityCategory category : kCategoryOrder) {
        size_t categoryIndex = static_cast<size_t>(category);
        const std::vector<core::EntityId>& roots = groupedRoots[categoryIndex];
        if (roots.empty()) continue;

        Icon groupIcon = iconForCategory(category);
        IconCategoryColor groupColor = iconCategoryColor(groupIcon);
        drawInlineIcon(groupIcon, groupColor);

        char headerLabel[64];
        std::snprintf(headerLabel, sizeof(headerLabel), "%s (%zu)##group_%zu", categoryDisplayName(category),
                      roots.size(), categoryIndex);
        bool open = ImGui::TreeNodeEx(headerLabel, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

        // Real, if simple, smooth-collapse animation (task category 3):
        // categoryOpenAmount_[categoryIndex] lerps toward ImGui's own
        // open/closed state (1.0/0.0) every frame at a fixed rate, and
        // that value drives a real alpha fade on this group's rows --
        // rows keep rendering (skipped entirely once the fade is close
        // enough to invisible, so a collapsed group isn't still
        // clickable) but fade smoothly in/out instead of an instant
        // ImGui show/hide, real motion ImGui's own TreeNode doesn't give
        // for free.
        float target = open ? 1.0f : 0.0f;
        float& openAmount = categoryOpenAmount_[categoryIndex];
        float dt = ImGui::GetIO().DeltaTime;
        constexpr float kAnimationRate = 10.0f; // higher = snappier; see animateOpenAmount()'s own tests for the convergence math
        openAmount = animateOpenAmount(openAmount, target, dt, kAnimationRate);

        if (openAmount > 0.02f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * openAmount);
            ImGui::Indent();
            for (core::EntityId root : roots) drawEntityNode(ecs, root, rowCursor, rows);
            ImGui::Unindent();
            ImGui::PopStyleVar();
        } else {
            for (core::EntityId root : roots) {
                std::vector<core::EntityId> subtree;
                collectRowsRecursive(ecs, root, 0, subtree);
                rowCursor += static_cast<int>(subtree.size());
            }
        }

        if (open) ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace engine::studio::panels

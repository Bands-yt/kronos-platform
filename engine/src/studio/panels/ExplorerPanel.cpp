#include "studio/panels/ExplorerPanel.hpp"
#include "core/UnionGeometry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>

#include <imgui.h>

#include "core/Hierarchy.hpp"
#include "core/InstanceTree.hpp"
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
// Roblox-style class icon; nullopt falls back to the category icon.
std::optional<Icon> classIcon(core::ECS& ecs, core::EntityId entity) {
    namespace inst = core::instances;
    const std::string cls = inst::className(ecs, inst::refOf(ecs, entity));
    if (inst::classIsA(cls, "LuaSourceContainer")) return Icon::Script;
    if (inst::classIsA(cls, "BasePart")) return Icon::Part;
    if (cls == "Model") return Icon::Group;
    if (cls == "Folder" || (inst::findClass(cls) != nullptr && inst::findClass(cls)->service)) return Icon::Folder;
    if (inst::classIsA(cls, "Light")) return Icon::Lighting;
    if (inst::classIsA(cls, "GuiBase")) return Icon::Layout;
    if (cls == "WeldConstraint") return Icon::Anchor;
    return std::nullopt;
}

// Right-click > Insert Object; returns the new object or kNullEntity.
core::EntityId insertObjectMenu(core::ECS& ecs, core::InstanceRef parent) {
    namespace inst = core::instances;
    static const std::pair<const char*, std::vector<const char*>> kGroups[] = {
        {"Parts", {"Part", "SpawnLocation", "Seat", "WedgePart", "Model", "Folder"}},
        {"Scripts", {"Script", "LocalScript", "ModuleScript"}},
        {"Effects", {"PointLight", "SpotLight", "Sound"}},
        {"Joints", {"WeldConstraint"}},
        {"GUI", {"ScreenGui", "Frame", "TextLabel", "TextButton", "ImageLabel", "UIListLayout", "UICorner"}},
        {"Events and values",
         {"RemoteEvent", "RemoteFunction", "BindableEvent", "BoolValue", "IntValue", "NumberValue", "StringValue"}},
    };
    core::EntityId made = core::kNullEntity;
    if (!ImGui::BeginMenu("Insert Object")) return made;
    for (const auto& [group, classes] : kGroups) {
        ImGui::SeparatorText(group);
        for (const char* cls : classes) {
            const core::ClassDef* def = inst::findClass(cls);
            if (def == nullptr || !def->creatable || !ImGui::MenuItem(cls)) continue;
            std::string error;
            const core::InstanceRef ref = inst::create(ecs, cls, error);
            if (ref == core::kNoInstance) continue;
            inst::setName(ecs, ref, cls);
            if (inst::classIsA(cls, "BasePart")) {
                glm::vec3 at(0.0f, 4.0f, 0.0f);
                if (inst::classIsA(inst::className(ecs, parent), "BasePart")) {
                    core::InstanceValue p;
                    inst::getProperty(ecs, parent, *inst::findProperty("BasePart", "Position"), p);
                    at = p.vec + glm::vec3(0.0f, 3.0f, 0.0f);
                }
                inst::setProperty(ecs, ref, *inst::findProperty("BasePart", "Position"), core::InstanceValue::ofVector3(at));
                inst::setProperty(ecs, ref, *inst::findProperty("BasePart", "Anchored"), core::InstanceValue::ofBool(true));
            }
            if (!inst::setParent(ecs, ref, parent, error)) {
                inst::destroy(ecs, ref);
                continue;
            }
            made = inst::entityOf(ecs, ref);
        }
    }
    ImGui::EndMenu();
    return made;
}

// Roblox Studio's Explorer order for services.
int serviceRank(const std::string& cls) {
    static const char* const kOrder[] = {"Players", "Lighting", "MaterialService", "ReplicatedFirst",
                                         "ReplicatedStorage", "ServerScriptService", "ServerStorage", "StarterGui",
                                         "StarterPack", "StarterPlayer", "Teams", "SoundService", "Chat"};
    for (int i = 0; i < static_cast<int>(std::size(kOrder)); ++i) {
        if (cls == kOrder[i]) return i;
    }
    return static_cast<int>(std::size(kOrder));
}
} // namespace

namespace {
void makeLabel(core::ECS& ecs, core::EntityId entity, char* label, size_t size) {
    const core::Name* nameComponent = ecs.tryGetComponent<core::Name>(entity);
    if (nameComponent && !nameComponent->value.empty()) {
        std::snprintf(label, size, "%s##%u", nameComponent->value.c_str(), static_cast<uint32_t>(entity));
    } else {
        std::snprintf(label, size, "Entity %u", static_cast<uint32_t>(entity));
    }
}
} // namespace

int ExplorerPanel::countSubtree(core::ECS& ecs, core::EntityId entity, int depth) {
    int count = 1;
    if (depth < kMaxTreeDrawDepth) {
        if (auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity)) {
            for (core::EntityId child : hierarchy->children) count += countSubtree(ecs, child, depth + 1);
        }
    }
    subtreeSize_[entity] = count;
    return count;
}

// Rows `entity` takes on screen: itself plus the children of every node that is open.
int ExplorerPanel::visibleRows(core::ECS& ecs, core::EntityId entity) {
    auto* hierarchy = ecs.tryGetComponent<core::Hierarchy>(entity);
    if (hierarchy == nullptr || hierarchy->children.empty()) return 1;
    char label[64];
    makeLabel(ecs, entity, label, sizeof(label));
    if (ImGui::GetStateStorage()->GetInt(ImGui::GetID(label), 1) == 0) return 1;
    int rows = 1;
    ImGui::PushID(label);
    for (core::EntityId child : hierarchy->children) rows += visibleRows(ecs, child);
    ImGui::PopID();
    return rows;
}

void ExplorerPanel::flushSkippedRows() {
    if (skippedHeight_ <= 0.0f) return;
    ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, skippedHeight_ - ImGui::GetStyle().ItemSpacing.y)));
    skippedHeight_ = 0.0f;
}

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

    if (virtualize_) {
        const float y = ImGui::GetCursorPosY() + skippedHeight_;
        const float margin = rowHeight_ * 4.0f;
        const float height = static_cast<float>(visibleRows(ecs, entity)) * rowHeight_;
        if (y + height < viewTop_ - margin || y > viewBottom_ + margin) {
            skippedHeight_ += height;
            const auto size = subtreeSize_.find(entity);
            if (size != subtreeSize_.end()) rowCursor += size->second - 1;
            return;
        }
        flushSkippedRows();
    }

    EntityCategory category = classifyEntity(ecs, entity);
    if (const std::optional<Icon> icon = classIcon(ecs, entity)) {
        drawInlineIcon(*icon, iconCategoryColor(*icon));
    } else if (category != EntityCategory::Other) {
        drawInlineIcon(iconForCategory(category), iconCategoryColor(iconForCategory(category)));
    }

    char label[64];
    makeLabel(ecs, entity, label, sizeof(label));

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
    if (virtualize_) rowHeight_ = ImGui::GetItemRectSize().y + ImGui::GetStyle().ItemSpacing.y;

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
        if (const core::EntityId made = insertObjectMenu(ecs, core::instances::refOf(ecs, entity)); made != core::kNullEntity) {
            setSelected(made);
            inserted_ = made;
        }
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

void ExplorerPanel::drawRobloxTree(core::ECS& ecs) {
    namespace inst = core::instances;
    auto entitiesOf = [&](const std::vector<core::InstanceRef>& refs) {
        std::vector<core::EntityId> out;
        for (core::InstanceRef ref : refs) {
            const core::EntityId e = inst::entityOf(ecs, ref);
            if (e != core::kNullEntity && ecs.tryGetComponent<core::PlayerAvatarPart>(e) == nullptr) out.push_back(e);
        }
        return out;
    };
    const std::vector<core::EntityId> workspaceChildren = entitiesOf(inst::children(ecs, core::kWorkspaceInstance));
    std::vector<core::EntityId> services;
    for (core::InstanceRef ref : inst::children(ecs, core::kGameInstance)) {
        if (ref == core::kWorkspaceInstance || inst::children(ecs, ref).empty()) continue;
        for (core::EntityId e : entitiesOf({ref})) services.push_back(e);
    }
    std::stable_sort(services.begin(), services.end(), [&](core::EntityId a, core::EntityId b) {
        return serviceRank(inst::className(ecs, inst::refOf(ecs, a))) <
               serviceRank(inst::className(ecs, inst::refOf(ecs, b)));
    });

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##explorer_search", "Search objects", search_, sizeof(search_));
    if (search_[0] != '\0') {
        std::string needle(search_);
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return std::tolower(c); });
        int shown = 0;
        for (core::InstanceRef ref : inst::descendants(ecs, core::kGameInstance)) {
            const core::EntityId e = inst::entityOf(ecs, ref);
            if (e == core::kNullEntity) continue;
            std::string name = inst::name(ecs, ref);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name.find(needle) == std::string::npos) continue;
            const std::string path = inst::fullName(ecs, ref);
            const bool isSelected = std::find(selection_.begin(), selection_.end(), e) != selection_.end();
            if (const std::optional<Icon> icon = classIcon(ecs, e)) drawInlineIcon(*icon, iconCategoryColor(*icon));
            ImGui::PushID(static_cast<int>(entt::to_integral(e)));
            if (ImGui::Selectable(path.c_str(), isSelected)) setSelected(e);
            ImGui::PopID();
            if (++shown >= 500) break;
        }
        if (shown == 0) ImGui::TextDisabled("No objects named like that.");
        return;
    }

    std::vector<core::EntityId> rows;
    for (core::EntityId e : workspaceChildren) collectRowsRecursive(ecs, e, 0, rows);
    for (core::EntityId e : services) collectRowsRecursive(ecs, e, 0, rows);
    int rowCursor = 0;

    subtreeSize_.clear();
    for (core::EntityId e : workspaceChildren) countSubtree(ecs, e, 0);
    for (core::EntityId e : services) countSubtree(ecs, e, 0);
    virtualize_ = true;
    if (rowHeight_ <= 0.0f) rowHeight_ = ImGui::GetFrameHeightWithSpacing();
    viewTop_ = ImGui::GetScrollY();
    viewBottom_ = viewTop_ + ImGui::GetWindowHeight();
    skippedHeight_ = 0.0f;

    drawInlineIcon(Icon::Folder, iconCategoryColor(Icon::Folder));
    const bool workspaceOpen =
        ImGui::TreeNodeEx("Workspace##roblox_workspace", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
    if (ImGui::BeginPopupContextItem("workspace_menu")) {
        if (const core::EntityId made = insertObjectMenu(ecs, core::kWorkspaceInstance); made != core::kNullEntity) {
            setSelected(made);
            inserted_ = made;
        }
        ImGui::EndPopup();
    }
    if (workspaceOpen) {
        for (core::EntityId e : workspaceChildren) drawEntityNode(ecs, e, rowCursor, rows);
        ImGui::TreePop();
    } else {
        for (core::EntityId e : workspaceChildren) {
            std::vector<core::EntityId> subtree;
            collectRowsRecursive(ecs, e, 0, subtree);
            rowCursor += static_cast<int>(subtree.size());
        }
    }
    for (core::EntityId e : services) drawEntityNode(ecs, e, rowCursor, rows);
    flushSkippedRows();
    virtualize_ = false;
}

void ExplorerPanel::draw(core::ECS& ecs) {
    ImGui::Begin("Explorer");
    if (ImGui::RadioButton("Tree", robloxTree_)) robloxTree_ = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Groups", !robloxTree_)) robloxTree_ = false;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Group objects by kind (the older view).");
    ImGui::Separator();
    if (robloxTree_) {
        drawRobloxTree(ecs);
        ImGui::End();
        return;
    }

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
        if (!isTreeRoot(ecs, entity) || ecs.tryGetComponent<core::PlayerAvatarPart>(entity) != nullptr ||
            core::unions::isPiece(ecs, entity) ||
            core::instances::isDetached(ecs, entity)) {
            continue;
        }
        size_t categoryIndex = static_cast<size_t>(classifyEntity(ecs, entity));
        groupedRoots[categoryIndex].push_back(entity);
    }
    // Views run newest first; the tree reads oldest first like Roblox's.
    for (auto& group : groupedRoots) std::reverse(group.begin(), group.end());

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

#include "studio/StudioRibbon.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "core/Components.hpp"
#include "core/UITheme.hpp"
#include "core/UIWidgets.hpp"
#include "studio/MaterialPresets.hpp"
#include "studio/PluginManager.hpp"
#include "studio/UndoStack.hpp"
#include "studio/panels/ExplorerPanel.hpp"
#include "studio/plugins/ModelImporterPlugin.hpp"
#include "studio/plugins/PhysicsPreviewPlugin.hpp"

namespace engine::studio {

namespace {

constexpr float kTabStripHeight = 26.0f;
constexpr float kBodyHeight = 90.0f;
constexpr float kCaptionHeight = 17.0f;
constexpr float kBigButtonHeight = kBodyHeight - kCaptionHeight - 8.0f;
constexpr float kSmallRowHeight = 19.0f;
constexpr float kGroupPadding = 8.0f;

constexpr ImU32 kBlue = IM_COL32(86, 170, 255, 255);
constexpr ImU32 kGreen = IM_COL32(88, 200, 120, 255);
constexpr ImU32 kRed = IM_COL32(240, 96, 96, 255);
constexpr ImU32 kOrange = IM_COL32(245, 160, 70, 255);
constexpr ImU32 kYellow = IM_COL32(246, 206, 84, 255);
constexpr ImU32 kPurple = IM_COL32(176, 132, 255, 255);
constexpr ImU32 kGrey = IM_COL32(205, 208, 214, 255);

ImU32 withAlpha(ImU32 color, float alpha) {
    const ImU32 a = static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f));
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 accentU32(float alpha) {
    const ImVec4 a = ui::accent();
    return ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, alpha));
}

ImVec4 lighten(ImVec4 c, float amount) {
    return ImVec4(c.x + (1.0f - c.x) * amount, c.y + (1.0f - c.y) * amount, c.z + (1.0f - c.z) * amount, c.w);
}

} // namespace

bool StudioRibbon::bigButton(const char* id, Icon icon, const char* label, bool active, bool enabled, const char* tooltip,
                             ImU32 iconColor, bool dropdown) {
    const float labelWidth = ImGui::CalcTextSize(label).x;
    const float width = std::max(48.0f, labelWidth + 16.0f);
    const ImVec2 min(cursorX_, bodyMin_.y + 4.0f);
    const ImVec2 max(min.x + width, min.y + kBigButtonHeight);

    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##big", ImVec2(width, kBigButtonHeight));
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const ImGuiID itemId = ImGui::GetItemID();
    const float hover = ui::animate(itemId, hovered && enabled ? 1.0f : 0.0f);
    const float on = ui::animate(itemId + 1, active ? 1.0f : 0.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (on > 0.01f) {
        drawList->AddRectFilled(min, max, accentU32(0.20f * on), 4.0f);
        drawList->AddRect(min, max, accentU32(0.65f * on), 4.0f);
    }
    if (hover > 0.01f) drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, static_cast<int>(18.0f * hover)), 4.0f);

    const float alpha = enabled ? 1.0f : 0.35f;
    drawIcon(drawList, icon, ImVec2((min.x + max.x) * 0.5f, min.y + 22.0f), 26.0f, withAlpha(iconColor, alpha));
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text, enabled ? 0.92f : 0.4f);
    drawList->AddText(ImVec2((min.x + max.x - labelWidth) * 0.5f, min.y + 41.0f), textColor, label);
    if (dropdown) {
        const ImVec2 c((min.x + max.x) * 0.5f, min.y + 58.0f);
        drawList->AddTriangleFilled(ImVec2(c.x - 3.5f, c.y - 2.0f), ImVec2(c.x + 3.5f, c.y - 2.0f), ImVec2(c.x, c.y + 2.0f), textColor);
    }
    if (tooltip != nullptr && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    cursorX_ = max.x + 2.0f;
    return clicked && enabled;
}

bool StudioRibbon::beginDropdown(const char* popupId) {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 4.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(140.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
    const bool open = ImGui::BeginPopup(popupId);
    ImGui::PopStyleVar(2);
    if (open) ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
    return open;
}

void StudioRibbon::endDropdown() {
    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

bool StudioRibbon::smallButton(const char* id, Icon icon, const char* label, bool active, bool enabled, const char* tooltip,
                               ImU32 iconColor) {
    const float width = ImGui::CalcTextSize(label).x + 30.0f;
    const ImVec2 min(columnStartX_, bodyMin_.y + 5.0f + static_cast<float>(columnRow_) * (kSmallRowHeight + 2.0f));
    const ImVec2 max(min.x + width, min.y + kSmallRowHeight);

    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##small", ImVec2(width, kSmallRowHeight));
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (active) {
        drawList->AddRectFilled(min, max, accentU32(0.22f), 3.0f);
        drawList->AddRect(min, max, accentU32(0.6f), 3.0f);
    }
    if (hovered && enabled) drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 18), 3.0f);
    const float alpha = enabled ? 1.0f : 0.35f;
    drawIcon(drawList, icon, ImVec2(min.x + 11.0f, (min.y + max.y) * 0.5f), 14.0f, withAlpha(iconColor, alpha));
    drawList->AddText(ImVec2(min.x + 23.0f, min.y + (kSmallRowHeight - ImGui::GetFontSize()) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_Text, enabled ? 0.9f : 0.4f), label);
    if (tooltip != nullptr && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    columnWidth_ = std::max(columnWidth_, width);
    ++columnRow_;
    return clicked && enabled;
}

void StudioRibbon::beginGroup() {
    cursorX_ += kGroupPadding;
    groupStartX_ = cursorX_;
}

void StudioRibbon::endGroup(const char* caption) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float captionWidth = ImGui::CalcTextSize(caption).x;
    const float contentWidth = std::max(cursorX_ - groupStartX_, captionWidth + 8.0f);
    const float right = groupStartX_ + contentWidth;
    const float captionY = bodyMin_.y + bodyHeight_ - kCaptionHeight + 1.0f;
    drawList->AddText(ImVec2(groupStartX_ + (contentWidth - captionWidth) * 0.5f, captionY),
                      ImGui::GetColorU32(ImGuiCol_TextDisabled), caption);
    cursorX_ = right + kGroupPadding;
    drawList->AddLine(ImVec2(cursorX_, bodyMin_.y + 6.0f), ImVec2(cursorX_, bodyMin_.y + bodyHeight_ - 6.0f),
                      ImGui::GetColorU32(ImGuiCol_Separator, 0.8f), 1.0f);
    cursorX_ += 1.0f;
}

void StudioRibbon::beginColumn() {
    inColumn_ = true;
    columnStartX_ = cursorX_ + 2.0f;
    columnWidth_ = 0.0f;
    columnRow_ = 0;
}

void StudioRibbon::endColumn() {
    inColumn_ = false;
    cursorX_ = columnStartX_ + columnWidth_ + 4.0f;
}

void StudioRibbon::draw(const RibbonContext& context) {
    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec4 bodyBg = lighten(style.Colors[ImGuiCol_WindowBg], 0.035f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_MenuBarBg]);
    ImGui::BeginChild("##StudioRibbon", ImVec2(0.0f, kTabStripHeight + kBodyHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawTabStrip(context);

    bodyMin_ = ImVec2(origin.x, origin.y + kTabStripHeight);
    bodyHeight_ = kBodyHeight;
    drawList->AddRectFilled(bodyMin_, ImVec2(origin.x + width, bodyMin_.y + kBodyHeight), ImGui::GetColorU32(bodyBg));
    drawList->AddLine(ImVec2(origin.x, bodyMin_.y + kBodyHeight - 1.0f), ImVec2(origin.x + width, bodyMin_.y + kBodyHeight - 1.0f),
                      ImGui::GetColorU32(ImGuiCol_Border));

    cursorX_ = origin.x + 2.0f;
    switch (tab_) {
        case Tab::Home: drawHome(context); break;
        case Tab::Model: drawModel(context); break;
        case Tab::Test: drawTest(context); break;
        case Tab::View: drawView(context); break;
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, bodyMin_.y + kBodyHeight));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void StudioRibbon::drawTabStrip(const RibbonContext& context) {
    static constexpr const char* kLabels[] = {"HOME", "MODEL", "TEST", "VIEW"};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 bodyBg = lighten(ImGui::GetStyle().Colors[ImGuiCol_WindowBg], 0.035f);

    ImFont* bold = core::kronosBoldFont();
    float x = origin.x + 8.0f;
    for (int i = 0; i < 4; ++i) {
        if (bold != nullptr) ImGui::PushFont(bold);
        const ImVec2 textSize = ImGui::CalcTextSize(kLabels[i]);
        if (bold != nullptr) ImGui::PopFont();
        const float tabWidth = textSize.x + 26.0f;
        const ImVec2 min(x, origin.y + 3.0f);
        const ImVec2 max(x + tabWidth, origin.y + kTabStripHeight);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", ImVec2(tabWidth, kTabStripHeight - 3.0f))) tab_ = static_cast<Tab>(i);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool selected = static_cast<int>(tab_) == i;
        if (selected) {
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(bodyBg), 4.0f, ImDrawFlags_RoundCornersTop);
            drawList->AddLine(ImVec2(min.x + 6.0f, min.y + 1.0f), ImVec2(max.x - 6.0f, min.y + 1.0f), accentU32(1.0f), 2.0f);
        } else if (hovered) {
            drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 12), 4.0f, ImDrawFlags_RoundCornersTop);
        }
        const ImU32 textColor = selected ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_Text, hovered ? 0.85f : 0.6f);
        if (bold != nullptr) ImGui::PushFont(bold);
        drawList->AddText(ImVec2(min.x + 13.0f, min.y + (kTabStripHeight - 3.0f - textSize.y) * 0.5f), textColor, kLabels[i]);
        if (bold != nullptr) ImGui::PopFont();
        x = max.x + 2.0f;
    }

    // Quick access: undo / redo / save at the right end of the tab strip.
    struct Quick {
        const char* id;
        Icon icon;
        const char* tooltip;
        bool enabled;
    };
    const Quick quick[] = {{"q_save", Icon::Save, "Save scene (Ctrl+S)", context.state.canSave},
                           {"q_redo", Icon::Redo, "Redo (Ctrl+Y)", context.undo.canRedo()},
                           {"q_undo", Icon::Undo, "Undo (Ctrl+Z)", context.undo.canUndo()}};
    float right = origin.x + width - 6.0f;
    for (const Quick& q : quick) {
        right -= 24.0f;
        ImGui::SetCursorScreenPos(ImVec2(right, origin.y + 2.0f));
        ImGui::BeginDisabled(!q.enabled);
        const bool clicked = iconButton(q.id, q.icon, ImVec2(22.0f, 22.0f), false, q.tooltip);
        ImGui::EndDisabled();
        if (clicked && q.enabled) {
            if (q.icon == Icon::Undo) context.undo.undo();
            if (q.icon == Icon::Redo) context.undo.redo();
            if (q.icon == Icon::Save && context.actions.save) context.actions.save();
        }
    }
}

void StudioRibbon::drawToolsGroup(const RibbonContext& context) {
    panels::ViewportPanel& viewport = context.viewport;
    using Op = panels::ViewportPanel::GizmoOperation;
    const bool select = viewport.selectTool();
    beginGroup();
    if (bigButton("select", Icon::Select, "Select", select, true, "Select (Ctrl+1)", kGrey)) viewport.setSelectTool(true);
    if (bigButton("move", Icon::Translate, "Move", !select && viewport.gizmoOperation() == Op::Translate, true, "Move (W / Ctrl+2)",
                  kBlue)) {
        viewport.setGizmoOperation(Op::Translate);
    }
    if (bigButton("scale", Icon::Scale, "Scale", !select && viewport.gizmoOperation() == Op::Scale, true, "Scale (R / Ctrl+3)",
                  kGreen)) {
        viewport.setGizmoOperation(Op::Scale);
    }
    if (bigButton("rotate", Icon::Rotate, "Rotate", !select && viewport.gizmoOperation() == Op::Rotate, true,
                  "Rotate (E / Ctrl+4)", kOrange)) {
        viewport.setGizmoOperation(Op::Rotate);
    }
    endGroup("Tools");
}

void StudioRibbon::drawPlayGroup(const RibbonContext& context) {
    if (context.physics == nullptr) return;
    const bool playing = context.physics->isPlaying();
    beginGroup();
    if (bigButton("play", playing ? Icon::Stop : Icon::Play, playing ? "Stop" : "Play", playing, true,
                  playing ? "Stop the simulation and restore the scene" : "Simulate physics and run scripts",
                  playing ? kRed : kGreen)) {
        context.actions.togglePlay();
    }
    endGroup("Test");
}

void StudioRibbon::drawPartButton(const RibbonContext& context, const char* id) {
    if (bigButton(id, Icon::Part, "Part", false, true, "Insert a part", IM_COL32(150, 156, 168, 255), true)) {
        ImGui::OpenPopup("##ribbon_part_menu");
    }
    if (beginDropdown("##ribbon_part_menu")) {
        using P = panels::ViewportPanel::Primitive;
        struct Item {
            const char* label;
            P kind;
        };
        static constexpr Item kItems[] = {{"Block", P::Block}, {"Sphere", P::Sphere}, {"Cylinder", P::Cylinder},
                                          {"Plane", P::Plane},  {"Torus", P::Torus}};
        for (const Item& item : kItems) {
            if (ImGui::Selectable(item.label)) context.actions.insertPrimitive(item.kind);
        }
        endDropdown();
    }
}

void StudioRibbon::drawColorButton(const RibbonContext& context) {
    core::ECS& ecs = context.ecs;
    const std::vector<core::EntityId>& selection = context.explorer.selectedEntities();
    bool anyRenderable = false;
    for (core::EntityId e : selection) {
        if (ecs.raw().valid(e) && ecs.tryGetComponent<core::Renderable>(e) != nullptr) anyRenderable = true;
    }

    if (bigButton("color", Icon::Color, "Color", false, anyRenderable, "Color of the selected parts", kGrey, true)) {
        colorBefore_.clear();
        for (core::EntityId e : selection) {
            if (!ecs.raw().valid(e)) continue;
            if (auto* r = ecs.tryGetComponent<core::Renderable>(e)) {
                if (colorBefore_.empty()) editColor_ = r->baseColor;
                colorBefore_.emplace_back(e, r->baseColor);
            }
        }
        ImGui::OpenPopup("##ribbon_color");
    }

    if (beginDropdown("##ribbon_color")) {
        static constexpr ImU32 kPalette[] = {
            IM_COL32(242, 243, 243, 255), IM_COL32(163, 162, 165, 255), IM_COL32(99, 95, 98, 255),   IM_COL32(27, 42, 53, 255),
            IM_COL32(196, 40, 28, 255),   IM_COL32(218, 133, 65, 255),  IM_COL32(245, 205, 48, 255), IM_COL32(75, 151, 75, 255),
            IM_COL32(40, 127, 71, 255),   IM_COL32(13, 105, 172, 255),  IM_COL32(110, 153, 202, 255), IM_COL32(107, 50, 124, 255),
            IM_COL32(255, 102, 204, 255), IM_COL32(124, 92, 70, 255),   IM_COL32(204, 142, 105, 255), IM_COL32(0, 255, 255, 255)};
        auto apply = [&](glm::vec4 color) {
            for (const auto& [e, before] : colorBefore_) {
                if (!ecs.raw().valid(e)) continue;
                if (auto* r = ecs.tryGetComponent<core::Renderable>(e)) r->baseColor = glm::vec4(glm::vec3(color), before.a);
            }
        };
        auto commit = [&]() {
            std::vector<std::pair<core::EntityId, glm::vec4>> before = colorBefore_;
            std::vector<std::pair<core::EntityId, glm::vec4>> after;
            for (const auto& [e, c] : before) {
                if (auto* r = ecs.raw().valid(e) ? ecs.tryGetComponent<core::Renderable>(e) : nullptr) after.emplace_back(e, r->baseColor);
            }
            auto set = [&ecs](const std::vector<std::pair<core::EntityId, glm::vec4>>& values) {
                for (const auto& [e, c] : values) {
                    if (!ecs.raw().valid(e)) continue;
                    if (auto* r = ecs.tryGetComponent<core::Renderable>(e)) r->baseColor = c;
                }
            };
            context.undo.push({"Color", [set, before]() { set(before); }, [set, after]() { set(after); }});
            colorBefore_ = after;
        };

        for (int i = 0; i < 16; ++i) {
            if (i % 8 != 0) ImGui::SameLine(0.0f, 4.0f);
            const ImVec4 c = ImGui::ColorConvertU32ToFloat4(kPalette[i]);
            char id[16];
            std::snprintf(id, sizeof(id), "##sw%d", i);
            if (ImGui::ColorButton(id, c, ImGuiColorEditFlags_NoTooltip, ImVec2(22.0f, 22.0f))) {
                editColor_ = glm::vec4(c.x, c.y, c.z, 1.0f);
                apply(editColor_);
                commit();
            }
        }
        ImGui::Spacing();
        ImGui::SetNextItemWidth(186.0f);
        if (ImGui::ColorPicker3("##ribbon_color_picker", &editColor_.x,
                                ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_DisplayHex)) {
            apply(editColor_);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) commit();
        endDropdown();
    }
}

void StudioRibbon::drawMaterialButton(const RibbonContext& context) {
    core::ECS& ecs = context.ecs;
    const std::vector<core::EntityId>& selection = context.explorer.selectedEntities();
    bool anyRenderable = false;
    for (core::EntityId e : selection) {
        if (ecs.raw().valid(e) && ecs.tryGetComponent<core::Renderable>(e) != nullptr) anyRenderable = true;
    }
    if (bigButton("material", Icon::Material, "Material", false, anyRenderable, "Surface preset for the selected parts", kPurple, true)) {
        ImGui::OpenPopup("##ribbon_material");
    }
    if (beginDropdown("##ribbon_material")) {
        for (int i = 0; i < kMaterialPresetCount; ++i) {
            const MaterialPresetInfo& preset = kMaterialPresets[i];
            const ImVec4 swatch(preset.baseColor.x, preset.baseColor.y, preset.baseColor.z, 1.0f);
            ImGui::ColorButton("##swatch", swatch, ImGuiColorEditFlags_NoTooltip, ImVec2(16.0f, 16.0f));
            ImGui::SameLine();
            ImGui::PushID(i);
            if (ImGui::Selectable(preset.label)) {
                struct Surface {
                    core::EntityId entity;
                    glm::vec4 color;
                    float metallic, roughness;
                    glm::vec3 emissive;
                    float emissiveIntensity;
                };
                std::vector<Surface> before, after;
                for (core::EntityId e : selection) {
                    if (!ecs.raw().valid(e)) continue;
                    auto* r = ecs.tryGetComponent<core::Renderable>(e);
                    if (r == nullptr) continue;
                    before.push_back({e, r->baseColor, r->metallic, r->roughness, r->emissiveColor, r->emissiveIntensity});
                    after.push_back({e, preset.baseColor, preset.metallic, preset.roughness, preset.emissiveColor,
                                     preset.emissiveIntensity});
                }
                auto set = [&ecs](const std::vector<Surface>& values) {
                    for (const Surface& s : values) {
                        if (!ecs.raw().valid(s.entity)) continue;
                        if (auto* r = ecs.tryGetComponent<core::Renderable>(s.entity)) {
                            r->baseColor = s.color;
                            r->metallic = s.metallic;
                            r->roughness = s.roughness;
                            r->emissiveColor = s.emissive;
                            r->emissiveIntensity = s.emissiveIntensity;
                        }
                    }
                };
                set(after);
                context.undo.push({"Material", [set, before]() { set(before); }, [set, after]() { set(after); }});
            }
            ImGui::PopID();
        }
        endDropdown();
    }
}

void StudioRibbon::drawPluginToggle(const RibbonContext& context, const char* pluginName, Icon icon, const char* label) {
    IStudioPlugin* plugin = context.plugins.find(pluginName);
    if (plugin == nullptr) return;
    if (smallButton(pluginName, icon, label, plugin->isOpen(), true, nullptr, kGrey)) plugin->setOpen(!plugin->isOpen());
}

void StudioRibbon::drawHome(const RibbonContext& context) {
    const RibbonActions& actions = context.actions;
    const bool hasSelection = !context.explorer.selectedEntities().empty();
    const bool playing = context.physics != nullptr && context.physics->isPlaying();

    beginGroup();
    if (bigButton("paste", Icon::Paste, "Paste", false, context.state.canPaste && !playing, "Paste (Ctrl+V)", kGrey)) actions.paste();
    beginColumn();
    if (smallButton("copy", Icon::Copy, "Copy", false, hasSelection, "Copy (Ctrl+C)", kGrey)) actions.copy();
    if (smallButton("duplicate", Icon::Duplicate, "Duplicate", false, hasSelection && !playing, "Duplicate (Ctrl+D)", kGrey)) {
        actions.duplicate();
    }
    if (smallButton("delete", Icon::Delete, "Delete", false, hasSelection && !playing, "Delete (Del)", kRed)) actions.remove();
    endColumn();
    endGroup("Clipboard");

    drawToolsGroup(context);

    beginGroup();
    drawPartButton(context, "part_home");
    beginColumn();
    if (context.modelImporter != nullptr &&
        smallButton("import", Icon::Import, "Import 3D", false, true, "Import a glTF / OBJ / FBX model", kBlue)) {
        context.modelImporter->browseForFile();
    }
    drawPluginToggle(context, "Asset Browser", Icon::Folder, "Toolbox");
    drawPluginToggle(context, "Terrain Editor", Icon::Terrain, "Terrain");
    endColumn();
    endGroup("Insert");

    beginGroup();
    drawColorButton(context);
    drawMaterialButton(context);
    beginColumn();
    if (smallButton("group", Icon::Group, "Group", false, hasSelection && !playing, "Group selection (Ctrl+G)", kGrey)) actions.group();
    if (smallButton("ungroup", Icon::Group, "Ungroup", false, hasSelection && !playing, "Ungroup (Ctrl+U)", kGrey)) actions.ungroup();
    if (smallButton("anchor", Icon::Anchor, "Anchor", hasSelection && context.state.anchored, hasSelection && !playing,
                    "Anchored parts stay put when the simulation runs", kYellow)) {
        actions.toggleAnchor();
    }
    endColumn();
    endGroup("Edit");

    drawPlayGroup(context);

    beginGroup();
    beginColumn();
    drawPluginToggle(context, "Lighting Tools", Icon::Lighting, "Lighting");
    drawPluginToggle(context, "Publishing", Icon::Save, "Publish");
    drawPluginToggle(context, "Creator Tools", Icon::Prop, "Creator Tools");
    endColumn();
    endGroup("Game");
}

void StudioRibbon::drawModel(const RibbonContext& context) {
    panels::ViewportPanel& viewport = context.viewport;
    drawToolsGroup(context);

    beginGroup();
    const float rowX = cursorX_ + 2.0f;
    auto snapRow = [&](int row, const char* id, const char* label, bool* enabled, auto&& valueWidget) {
        const float y = bodyMin_.y + 5.0f + static_cast<float>(row) * (kSmallRowHeight + 2.0f);
        ImGui::SetCursorScreenPos(ImVec2(rowX, y));
        ImGui::PushID(id);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
        ImGui::Checkbox("##on", enabled);
        ImGui::PopStyleVar();
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3.0f);
        ImGui::TextUnformatted(label);
        ImGui::SetCursorScreenPos(ImVec2(rowX + 76.0f, y));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
        ImGui::SetNextItemWidth(70.0f);
        valueWidget();
        ImGui::PopStyleVar();
        ImGui::PopID();
    };
    auto presetCombo = [](const char* id, float* value, const float* presets, int count, const char* format) {
        char preview[24];
        std::snprintf(preview, sizeof(preview), format, *value);
        if (ImGui::BeginCombo(id, preview)) {
            for (int i = 0; i < count; ++i) {
                char text[24];
                std::snprintf(text, sizeof(text), format, presets[i]);
                if (ImGui::Selectable(text, std::fabs(*value - presets[i]) < 0.001f)) *value = presets[i];
            }
            ImGui::EndCombo();
        }
    };
    static constexpr float kMovePresets[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    static constexpr float kRotatePresets[] = {5.0f, 15.0f, 30.0f, 45.0f, 90.0f};
    snapRow(0, "snap_rotate", "Rotate", &viewport.angleSnapEnabled(),
            [&] { presetCombo("##rot", &viewport.rotateSnapDegrees(), kRotatePresets, 5, "%.0f\xc2\xb0"); });
    snapRow(1, "snap_move", "Move", &viewport.gridSnapEnabled(),
            [&] { presetCombo("##move", &viewport.translateSnap(), kMovePresets, 6, "%g m"); });
    snapRow(2, "snap_scale", "Scale", &viewport.scaleSnapEnabled(),
            [&] { ImGui::DragFloat("##scale", &viewport.scaleSnap(), 0.01f, 0.01f, 10.0f, "%.2f"); });
    cursorX_ = rowX + 150.0f;
    endGroup("Snap to Grid");

    beginGroup();
    const bool world = viewport.gizmoSpace() == panels::ViewportPanel::GizmoSpace::World;
    if (bigButton("space", world ? Icon::WorldSpace : Icon::LocalSpace, world ? "World" : "Local", false, true,
                  "Transform in world or local space", kBlue)) {
        viewport.setGizmoSpace(world ? panels::ViewportPanel::GizmoSpace::Local : panels::ViewportPanel::GizmoSpace::World);
    }
    endGroup("Space");

    beginGroup();
    drawPartButton(context, "part_model");
    beginColumn();
    const bool hasSelection = context.explorer.selectedEntity() != core::kNullEntity;
    if (smallButton("drop", Icon::Import, "Drop to Ground", false, hasSelection, "Rest the selection on what's below it (End)", kGreen)) {
        viewport.dropSelectedToGround(context.ecs, context.meshLibrary, context.explorer.selectedEntity());
    }
    if (smallButton("focus_m", Icon::Focus, "Focus", false, hasSelection, "Frame the selection (F)", kBlue)) {
        viewport.focusOn(context.ecs, context.meshLibrary, context.explorer.selectedEntities());
    }
    drawPluginToggle(context, "Align & Distribute", Icon::Layout, "Align");
    endColumn();
    endGroup("Parts");

    beginGroup();
    beginColumn();
    drawPluginToggle(context, "Modeling Mode", Icon::Part, "Modeling");
    drawPluginToggle(context, "Material Editor", Icon::Material, "Material Editor");
    drawPluginToggle(context, "Block Builder", Icon::Grid, "Block Builder");
    endColumn();
    beginColumn();
    drawPluginToggle(context, "Terrain Editor", Icon::Terrain, "Terrain");
    drawPluginToggle(context, "Particle Editor", Icon::Lighting, "Particles");
    drawPluginToggle(context, "Prefabs", Icon::Folder, "Prefabs");
    endColumn();
    endGroup("Advanced");
}

void StudioRibbon::drawTest(const RibbonContext& context) {
    drawPlayGroup(context);
    plugins::PhysicsPreviewPlugin* physics = context.physics;
    if (physics == nullptr) return;

    beginGroup();
    beginColumn();
    if (smallButton("colliders", Icon::Physics, "Colliders", physics->showColliders, true, "Draw collision shapes", kOrange)) {
        physics->showColliders = !physics->showColliders;
    }
    if (smallButton("contacts", Icon::Physics, "Contacts", physics->showContacts, true, "Draw contact points", kOrange)) {
        physics->showContacts = !physics->showContacts;
    }
    if (smallButton("raycasts", Icon::Physics, "Raycasts", physics->showRaycasts, true, "Draw raycasts", kOrange)) {
        physics->showRaycasts = !physics->showRaycasts;
    }
    endColumn();
    beginColumn();
    const core::Camera& camera = context.viewport.camera();
    if (smallButton("castray", Icon::Focus, "Cast Test Ray", false, physics->isPlaying(), "Cast a ray from the camera (while playing)",
                    kBlue)) {
        physics->castTestRay(camera.position, camera.forward(), 1000.0f);
    }
    drawPluginToggle(context, "Physics Preview", Icon::Physics, "Physics Panel");
    drawPluginToggle(context, "Diagnostics", Icon::Gauge, "Diagnostics");
    endColumn();
    endGroup("Physics Debug");
}

void StudioRibbon::drawView(const RibbonContext& context) {
    panels::ViewportPanel& viewport = context.viewport;
    const RibbonState& state = context.state;

    beginGroup();
    if (bigButton("perf", Icon::Gauge, "Performance", state.performanceOverlayOpen, true, "Performance overlay (F3)", kGreen)) {
        context.actions.togglePerformanceOverlay();
    }
    endGroup("Stats");

    beginGroup();
    beginColumn();
    if (smallButton("viewcube", Icon::Part, "View Cube", viewport.showViewCube(), true, "Orientation cube in the viewport", kBlue)) {
        viewport.showViewCube() = !viewport.showViewCube();
    }
    if (state.showEngineOverlays &&
        smallButton("bounds", Icon::Outline, "Bounding Boxes", viewport.showBoundingBoxes(), true, nullptr, kOrange)) {
        viewport.showBoundingBoxes() = !viewport.showBoundingBoxes();
    }
    if (state.showEngineOverlays &&
        smallButton("cascades", Icon::Lighting, "Shadow Cascades", viewport.showCascades(), true, nullptr, kYellow)) {
        viewport.showCascades() = !viewport.showCascades();
    }
    endColumn();
    if (state.showEngineOverlays) {
        beginColumn();
        if (smallButton("streaming", Icon::Terrain, "Terrain Streaming", viewport.showTerrainStreaming(), state.terrainAvailable,
                        state.terrainAvailable ? nullptr : "Create terrain first", kGreen)) {
            viewport.showTerrainStreaming() = !viewport.showTerrainStreaming();
        }
        endColumn();
    }
    endGroup("Show");

    beginGroup();
    const bool hasSelection = context.explorer.selectedEntity() != core::kNullEntity;
    if (bigButton("focus", Icon::Focus, "Focus", false, hasSelection, "Frame the selection (F)", kBlue)) {
        viewport.focusOn(context.ecs, context.meshLibrary, context.explorer.selectedEntities());
    }
    beginColumn();
    if (smallButton("resetcam", Icon::Undo, "Reset Camera", false, true, nullptr, kGrey)) viewport.resetCamera();
    endColumn();
    endGroup("Camera");

    beginGroup();
    if (bigButton("layout", Icon::Layout, "Reset Layout", false, true, "Restore the default window layout", kGrey)) {
        context.actions.resetLayout();
    }
    endGroup("Windows");
}

} // namespace engine::studio

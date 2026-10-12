#include "core/RobloxLeaderboard.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <imgui.h>

#include "core/InstanceTree.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/RobloxTools.hpp"
#include "core/UITheme.hpp"

namespace engine::core {

void drawLeaderboard(ECS& ecs, ImDrawList* drawList, float right, float top) {
    const players::Leaderboard board = players::leaderboard(ecs);
    if (board.columns.empty() || drawList == nullptr) return;

    constexpr float kPad = 8.0f;
    constexpr float kGap = 18.0f;
    const float rowHeight = ImGui::GetTextLineHeight() + 6.0f;

    std::vector<float> widths(board.columns.size() + 1, 0.0f);
    widths[0] = ImGui::CalcTextSize("Players").x;
    for (size_t c = 0; c < board.columns.size(); ++c) widths[c + 1] = ImGui::CalcTextSize(board.columns[c].c_str()).x;
    for (const auto& row : board.rows) {
        widths[0] = std::max(widths[0], ImGui::CalcTextSize(row.name.c_str()).x);
        for (size_t c = 0; c < row.values.size(); ++c) {
            widths[c + 1] = std::max(widths[c + 1], ImGui::CalcTextSize(row.values[c].c_str()).x);
        }
    }
    widths[0] = std::min(widths[0], 160.0f);
    float width = kPad * 2.0f;
    for (float w : widths) width += w;
    width += kGap * static_cast<float>(board.columns.size());
    const float height = kPad * 2.0f + rowHeight * static_cast<float>(board.rows.size() + 1);

    const ImVec2 origin(right - width, top);
    drawList->AddRectFilled(origin, ImVec2(right, top + height), IM_COL32(15, 17, 21, 190), 6.0f);
    const ImU32 header = ImGui::ColorConvertFloat4ToU32(ImVec4(kronos_palette::kTextMuted[0], kronos_palette::kTextMuted[1],
                                                               kronos_palette::kTextMuted[2], 1.0f));
    const ImU32 text = ImGui::ColorConvertFloat4ToU32(ImVec4(kronos_palette::kTextBright[0], kronos_palette::kTextBright[1],
                                                             kronos_palette::kTextBright[2], 1.0f));
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ImVec4(kronos_palette::kSkyBlue[0], kronos_palette::kSkyBlue[1],
                                                               kronos_palette::kSkyBlue[2], 1.0f));

    // Values are right-aligned in their column, like Roblox's list.
    auto drawRow = [&](float y, const char* name, const std::vector<std::string>& values, ImU32 color) {
        float x = origin.x + kPad;
        drawList->PushClipRect(ImVec2(x, y), ImVec2(x + widths[0], y + rowHeight), true);
        drawList->AddText(ImVec2(x, y), color, name);
        drawList->PopClipRect();
        x += widths[0];
        for (size_t c = 0; c < values.size(); ++c) {
            x += kGap + widths[c + 1];
            drawList->AddText(ImVec2(x - ImGui::CalcTextSize(values[c].c_str()).x, y), color, values[c].c_str());
        }
    };
    float y = origin.y + kPad;
    drawRow(y, "Players", board.columns, header);
    for (const auto& row : board.rows) {
        y += rowHeight;
        drawRow(y, row.name.c_str(), row.values, row.local ? accent : text);
    }
}

void drawToolHotbar(ECS& ecs, ImDrawList* drawList, float centerX, float bottom) {
    const std::vector<InstanceRef> slots = tools::hotbar(ecs);
    if (slots.empty() || drawList == nullptr) return;
    constexpr float kSlot = 64.0f;
    constexpr float kGap = 6.0f;
    const size_t count = std::min<size_t>(slots.size(), 9);
    const float width = kSlot * static_cast<float>(count) + kGap * static_cast<float>(count - 1);
    const InstanceRef player = players::localPlayer(ecs);
    InstanceRef character = kNoInstance;
    if (player != kNoInstance) {
        const PropertyDef* def = instances::findProperty("Player", "Character");
        InstanceValue value;
        if (def != nullptr && instances::getProperty(ecs, player, *def, value)) character = value.ref;
    }
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(ImVec4(kronos_palette::kSkyBlue[0], kronos_palette::kSkyBlue[1],
                                                               kronos_palette::kSkyBlue[2], 1.0f));
    for (size_t i = 0; i < count; ++i) {
        const ImVec2 min(centerX - width * 0.5f + static_cast<float>(i) * (kSlot + kGap), bottom - kSlot);
        const ImVec2 max(min.x + kSlot, bottom);
        const bool held = instances::parent(ecs, slots[i]) == character;
        drawList->AddRectFilled(min, max, IM_COL32(15, 17, 21, 190), 6.0f);
        if (held) drawList->AddRect(min, max, accent, 6.0f, 0, 2.5f);
        const std::string number = std::to_string(i + 1);
        drawList->AddText(ImVec2(min.x + 5.0f, min.y + 3.0f), IM_COL32(200, 204, 212, 255), number.c_str());
        const std::string name = instances::name(ecs, slots[i]);
        const ImVec2 size = ImGui::CalcTextSize(name.c_str());
        drawList->PushClipRect(ImVec2(min.x + 3.0f, min.y), ImVec2(max.x - 3.0f, max.y), true);
        drawList->AddText(ImVec2(min.x + std::max(3.0f, (kSlot - size.x) * 0.5f), min.y + (kSlot - size.y) * 0.5f + 6.0f),
                          IM_COL32(240, 242, 246, 255), name.c_str());
        drawList->PopClipRect();
    }
}

} // namespace engine::core

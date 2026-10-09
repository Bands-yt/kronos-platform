#include "core/RobloxLeaderboard.hpp"

#include <algorithm>
#include <vector>

#include <imgui.h>

#include "core/RobloxPlayers.hpp"
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

} // namespace engine::core

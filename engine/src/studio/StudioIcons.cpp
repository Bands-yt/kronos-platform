#include "studio/StudioIcons.hpp"

#include <algorithm>
#include <cmath>

namespace engine::studio {

namespace {

constexpr float kPi = 3.14159265358979323846f;

void drawArrowHead(ImDrawList* drawList, ImVec2 tip, ImVec2 direction, float length, float halfWidth, ImU32 color) {
    ImVec2 base(tip.x - direction.x * length, tip.y - direction.y * length);
    ImVec2 perp(-direction.y, direction.x);
    ImVec2 p1(base.x + perp.x * halfWidth, base.y + perp.y * halfWidth);
    ImVec2 p2(base.x - perp.x * halfWidth, base.y - perp.y * halfWidth);
    drawList->AddTriangleFilled(tip, p1, p2, color);
}

} // namespace

void drawIcon(ImDrawList* drawList, Icon icon, ImVec2 center, float size, ImU32 color) {
    float r = size * 0.5f;
    float thickness = std::max(1.4f, size * 0.09f);

    switch (icon) {
        case Icon::Translate: {
            float armLen = r * 0.85f;
            float headLen = r * 0.34f;
            float headHalfWidth = r * 0.22f;
            const ImVec2 dirs[4] = {{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f}};
            for (const ImVec2& d : dirs) {
                ImVec2 tip(center.x + d.x * armLen, center.y + d.y * armLen);
                ImVec2 base(center.x + d.x * (armLen - headLen), center.y + d.y * (armLen - headLen));
                drawList->AddLine(center, base, color, thickness);
                drawArrowHead(drawList, tip, d, headLen, headHalfWidth, color);
            }
            break;
        }
        case Icon::Rotate: {
            float radius = r * 0.68f;
            float startAngle = -0.10f * kPi;
            float endAngle = 1.55f * kPi;
            drawList->PathArcTo(center, radius, startAngle, endAngle, 20);
            drawList->PathStroke(color, ImDrawFlags_None, thickness);

            ImVec2 tip(center.x + std::cos(endAngle) * radius, center.y + std::sin(endAngle) * radius);
            ImVec2 tangent(-std::sin(endAngle), std::cos(endAngle));
            drawArrowHead(drawList, tip, tangent, r * 0.34f, r * 0.22f, color);
            break;
        }
        case Icon::Scale: {
            float half = r * 0.40f;
            drawList->AddRect(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), color,
                               1.5f, 0, thickness);
            ImVec2 from(center.x + half * 0.7f, center.y - half * 0.7f);
            ImVec2 to(center.x + r * 0.92f, center.y - r * 0.92f);
            drawList->AddLine(from, to, color, thickness);
            ImVec2 dir(to.x - from.x, to.y - from.y);
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len > 0.0001f) {
                dir.x /= len;
                dir.y /= len;
                drawArrowHead(drawList, to, dir, r * 0.26f, r * 0.16f, color);
            }
            break;
        }
        case Icon::WorldSpace: {
            drawList->AddCircle(center, r * 0.72f, color, 24, thickness * 0.9f);
            for (int i = 0; i <= 24; ++i) {
                float t = static_cast<float>(i) / 24.0f * kPi * 2.0f;
                drawList->PathLineTo(ImVec2(center.x + std::cos(t) * r * 0.72f, center.y + std::sin(t) * r * 0.28f));
            }
            drawList->PathStroke(color, ImDrawFlags_Closed, thickness * 0.7f);
            drawList->AddLine(ImVec2(center.x, center.y - r * 0.72f), ImVec2(center.x, center.y + r * 0.72f), color,
                               thickness * 0.7f);
            break;
        }
        case Icon::LocalSpace: {
            float half = r * 0.40f;
            ImVec2 frontMin(center.x - half, center.y - half * 0.55f);
            ImVec2 frontMax(center.x + half * 0.55f, center.y + half);
            ImVec2 offset(half * 0.55f, -half * 0.55f);

            drawList->AddLine(ImVec2(frontMin.x, frontMin.y), ImVec2(frontMin.x + offset.x, frontMin.y + offset.y), color,
                               thickness * 0.8f);
            drawList->AddLine(ImVec2(frontMax.x, frontMin.y), ImVec2(frontMax.x + offset.x, frontMin.y + offset.y), color,
                               thickness * 0.8f);
            drawList->AddLine(ImVec2(frontMax.x, frontMax.y), ImVec2(frontMax.x + offset.x, frontMax.y + offset.y), color,
                               thickness * 0.8f);
            drawList->AddLine(ImVec2(frontMin.x + offset.x, frontMin.y + offset.y),
                               ImVec2(frontMax.x + offset.x, frontMin.y + offset.y), color, thickness * 0.8f);
            drawList->AddLine(ImVec2(frontMax.x + offset.x, frontMin.y + offset.y),
                               ImVec2(frontMax.x + offset.x, frontMax.y + offset.y), color, thickness * 0.8f);
            drawList->AddRect(frontMin, frontMax, color, 1.0f, 0, thickness);
            break;
        }
        case Icon::Snap: {
            float spacing = r * 0.55f;
            float dotRadius = std::max(1.2f, size * 0.045f);
            for (int gx = -1; gx <= 1; ++gx) {
                for (int gy = -1; gy <= 1; ++gy) {
                    drawList->AddCircleFilled(ImVec2(center.x + static_cast<float>(gx) * spacing, center.y + static_cast<float>(gy) * spacing),
                                               dotRadius, color, 8);
                }
            }
            break;
        }
        // --- Sprint 7 ("Studio UI Revamp") additions -----------------
        case Icon::Terrain: {
            // Two overlapping jagged peaks, sitting on a base line --
            // real, recognizable "mountain/terrain" silhouette, not an
            // abstract shape reused for something else.
            float baseY = center.y + r * 0.45f;
            drawList->AddLine(ImVec2(center.x - r * 0.85f, baseY), ImVec2(center.x + r * 0.85f, baseY), color, thickness);
            ImVec2 peakA[4] = {{center.x - r * 0.75f, baseY}, {center.x - r * 0.30f, center.y - r * 0.55f},
                                {center.x + r * 0.05f, center.y - r * 0.05f}, {center.x - r * 0.75f, baseY}};
            drawList->AddPolyline(peakA, 3, color, ImDrawFlags_None, thickness);
            ImVec2 peakB[3] = {{center.x - r * 0.10f, baseY}, {center.x + r * 0.35f, center.y - r * 0.75f},
                                {center.x + r * 0.80f, baseY}};
            drawList->AddPolyline(peakB, 3, color, ImDrawFlags_None, thickness);
            break;
        }
        case Icon::Prop: {
            // A simple isometric-ish box outline -- "a placed object",
            // deliberately generic since real world props span
            // trees/rocks/crates/barrels/lamps/bushes, not one specific
            // shape.
            float half = r * 0.55f;
            ImVec2 top(center.x, center.y - half * 1.1f);
            ImVec2 topLeft(center.x - half, center.y - half * 0.35f);
            ImVec2 topRight(center.x + half, center.y - half * 0.35f);
            ImVec2 bottomLeft(center.x - half, center.y + half * 0.75f);
            ImVec2 bottomRight(center.x + half, center.y + half * 0.75f);
            ImVec2 bottom(center.x, center.y + half * 1.5f);
            drawList->AddLine(top, topLeft, color, thickness);
            drawList->AddLine(top, topRight, color, thickness);
            drawList->AddLine(topLeft, bottomLeft, color, thickness);
            drawList->AddLine(topRight, bottomRight, color, thickness);
            drawList->AddLine(bottomLeft, bottom, color, thickness);
            drawList->AddLine(bottomRight, bottom, color, thickness);
            drawList->AddLine(top, ImVec2(center.x, center.y - half * 0.35f), color, thickness * 0.8f);
            break;
        }
        case Icon::Script: {
            // A document page with a folded corner + a couple of text
            // lines -- the standard "this is code/text content" shape.
            float halfW = r * 0.55f;
            float halfH = r * 0.75f;
            float fold = halfW * 0.5f;
            ImVec2 pts[6] = {{center.x - halfW, center.y - halfH},
                              {center.x + halfW - fold, center.y - halfH},
                              {center.x + halfW, center.y - halfH + fold},
                              {center.x + halfW, center.y + halfH},
                              {center.x - halfW, center.y + halfH},
                              {center.x - halfW, center.y - halfH}};
            drawList->AddPolyline(pts, 6, color, ImDrawFlags_None, thickness);
            drawList->AddLine(ImVec2(center.x + halfW - fold, center.y - halfH),
                               ImVec2(center.x + halfW - fold, center.y - halfH + fold), color, thickness * 0.8f);
            drawList->AddLine(ImVec2(center.x + halfW - fold, center.y - halfH + fold),
                               ImVec2(center.x + halfW, center.y - halfH + fold), color, thickness * 0.8f);
            for (int i = 0; i < 3; ++i) {
                float lineY = center.y - halfH * 0.15f + static_cast<float>(i) * halfH * 0.42f;
                drawList->AddLine(ImVec2(center.x - halfW * 0.6f, lineY), ImVec2(center.x + halfW * 0.5f, lineY), color,
                                   thickness * 0.7f);
            }
            break;
        }
        case Icon::Material: {
            // A material-preview sphere: a filled circle plus a
            // brighter highlight arc, the standard "this is a shaded
            // material ball" convention every DCC/engine material
            // browser uses.
            drawList->AddCircleFilled(center, r * 0.72f, color, 24);
            ImU32 highlight = IM_COL32(255, 255, 255, 90);
            drawList->PathArcTo(ImVec2(center.x - r * 0.18f, center.y - r * 0.18f), r * 0.30f, kPi * 1.05f, kPi * 1.75f, 12);
            drawList->PathStroke(highlight, ImDrawFlags_None, thickness * 0.9f);
            break;
        }
        case Icon::Physics: {
            // A bounding box with a curved motion arrow sweeping around
            // it -- "this object really moves/collides", distinct from
            // Prop's plain static box outline.
            float half = r * 0.42f;
            drawList->AddRect(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half), color,
                               1.0f, 0, thickness * 0.85f);
            float startAngle = -0.15f * kPi;
            float endAngle = 1.35f * kPi;
            drawList->PathArcTo(center, r * 0.85f, startAngle, endAngle, 20);
            drawList->PathStroke(color, ImDrawFlags_None, thickness);
            ImVec2 tip(center.x + std::cos(endAngle) * r * 0.85f, center.y + std::sin(endAngle) * r * 0.85f);
            ImVec2 tangent(-std::sin(endAngle), std::cos(endAngle));
            drawArrowHead(drawList, tip, tangent, r * 0.28f, r * 0.18f, color);
            break;
        }
        case Icon::Lighting: {
            // A real sunburst -- a filled circle plus 8 radiating rays,
            // the standard "light source" glyph.
            drawList->AddCircleFilled(center, r * 0.40f, color, 16);
            for (int i = 0; i < 8; ++i) {
                float angle = static_cast<float>(i) / 8.0f * kPi * 2.0f;
                ImVec2 from(center.x + std::cos(angle) * r * 0.58f, center.y + std::sin(angle) * r * 0.58f);
                ImVec2 to(center.x + std::cos(angle) * r * 0.95f, center.y + std::sin(angle) * r * 0.95f);
                drawList->AddLine(from, to, color, thickness * 0.85f);
            }
            break;
        }
        case Icon::Folder: {
            // A classic folder silhouette -- a small tab, then the main
            // body, real and instantly recognizable rather than an
            // abstract stand-in.
            float halfW = r * 0.80f;
            float halfH = r * 0.55f;
            float tabWidth = halfW * 0.65f;
            float tabHeight = halfH * 0.35f;
            ImVec2 pts[8] = {{center.x - halfW, center.y + halfH},
                              {center.x - halfW, center.y - halfH + tabHeight},
                              {center.x - halfW + tabWidth * 0.3f, center.y - halfH + tabHeight},
                              {center.x - halfW + tabWidth * 0.55f, center.y - halfH},
                              {center.x - halfW + tabWidth, center.y - halfH},
                              {center.x + halfW, center.y - halfH + tabHeight},
                              {center.x + halfW, center.y + halfH},
                              {center.x - halfW, center.y + halfH}};
            drawList->AddPolyline(pts, 8, color, ImDrawFlags_None, thickness);
            break;
        }
    }
}

IconCategoryColor iconCategoryColor(Icon icon) {
    // Real, distinct hues per category -- chosen to stay legible against
    // StudioStyle's dark backgrounds and to stay apart from both the
    // teal/cyan accent and Notification.hpp's severity colors.
    switch (icon) {
        case Icon::Terrain: return {0.42f, 0.68f, 0.38f};   // earthy green
        case Icon::Prop: return {0.78f, 0.58f, 0.32f};      // warm wood/tan
        case Icon::Script: return {0.55f, 0.62f, 0.90f};    // cool blue-violet
        case Icon::Material: return {0.85f, 0.45f, 0.70f};  // magenta/pink
        case Icon::Physics: return {0.90f, 0.55f, 0.25f};   // orange
        case Icon::Lighting: return {0.95f, 0.80f, 0.30f};  // warm yellow
        case Icon::Folder: return {0.65f, 0.65f, 0.70f};    // neutral steel
        default: return {0.70f, 0.72f, 0.76f};              // gizmo icons -- neutral, no real "category"
    }
}

const char* iconCategoryName(Icon icon) {
    switch (icon) {
        case Icon::Terrain: return "Terrain";
        case Icon::Prop: return "Props";
        case Icon::Script: return "Scripts";
        case Icon::Material: return "Materials";
        case Icon::Physics: return "Physics";
        case Icon::Lighting: return "Lighting";
        case Icon::Folder: return "Group";
        default: return "Object";
    }
}

bool iconButton(const char* strId, Icon icon, ImVec2 buttonSize, bool active, const char* tooltip) {
    ImGui::PushID(strId);
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##icon_btn", buttonSize);
    bool hovered = ImGui::IsItemHovered();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 bgColor = active ? ImGui::GetColorU32(ImGuiCol_HeaderActive)
                            : (hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg));
    drawList->AddRectFilled(cursor, ImVec2(cursor.x + buttonSize.x, cursor.y + buttonSize.y), bgColor,
                             ImGui::GetStyle().FrameRounding);

    ImVec2 center(cursor.x + buttonSize.x * 0.5f, cursor.y + buttonSize.y * 0.5f);
    float iconSize = std::min(buttonSize.x, buttonSize.y) * 0.78f;
    drawIcon(drawList, icon, center, iconSize, ImGui::GetColorU32(ImGuiCol_Text));

    if (tooltip != nullptr && hovered) {
        ImGui::SetTooltip("%s", tooltip);
    }
    ImGui::PopID();
    return clicked;
}

} // namespace engine::studio

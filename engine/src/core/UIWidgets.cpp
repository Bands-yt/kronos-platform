#include "core/UIWidgets.hpp"

#include <algorithm>
#include <cmath>

#include <imgui_internal.h>

#include "core/UITheme.hpp"

namespace engine::ui {

namespace {

ImVec4 withAlpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }

ImVec4 lerp4(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 lighten(ImVec4 c, float amount) {
    return ImVec4(c.x + (1.0f - c.x) * amount, c.y + (1.0f - c.y) * amount, c.z + (1.0f - c.z) * amount, c.w);
}

ImVec4 darken(ImVec4 c, float amount) {
    return ImVec4(c.x * (1.0f - amount), c.y * (1.0f - amount), c.z * (1.0f - amount), c.w);
}

ImVec4 textOn(ImVec4 bg) {
    const float luminance = 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z;
    return luminance > 0.62f ? ImVec4(0.06f, 0.07f, 0.08f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

ImVec4 col(ImGuiCol idx) { return ImGui::GetStyle().Colors[idx]; }

ImGuiID key(ImGuiID id, const char* salt) { return ImHashStr(salt, 0, id); }

struct Interaction {
    float hover = 0.0f;
    float press = 0.0f;
};

Interaction animateInteraction(ImGuiID id, bool hovered, bool held) {
    Interaction state;
    state.hover = animate(key(id, "hover"), hovered ? 1.0f : 0.0f, 18.0f);
    state.press = animate(key(id, "press"), held ? 1.0f : 0.0f, 30.0f);
    return state;
}

void drawFocusRing(ImDrawList* drawList, ImGuiID id, ImVec2 min, ImVec2 max, float rounding) {
    ImGuiContext& g = *GImGui;
    if (g.NavId != id || !g.NavCursorVisible) return;
    drawList->AddRect(ImVec2(min.x - 2.0f, min.y - 2.0f), ImVec2(max.x + 2.0f, max.y + 2.0f),
                      ImGui::GetColorU32(withAlpha(accent(), 0.9f)), rounding + 2.0f, 0, 1.5f);
}

} // namespace

float animate(ImGuiID id, float target, float speed) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float* value = storage->GetFloatRef(id, target);
    const float blend = 1.0f - std::exp(-speed * ImGui::GetIO().DeltaTime);
    *value += (target - *value) * blend;
    if (std::fabs(target - *value) < 0.0005f) *value = target;
    return *value;
}

ImVec4 accent() { return col(ImGuiCol_CheckMark); }

ImU32 mix(ImVec4 a, ImVec4 b, float t, float alphaScale) {
    ImVec4 c = lerp4(a, b, std::clamp(t, 0.0f, 1.0f));
    c.w *= alphaScale;
    return ImGui::GetColorU32(c);
}

void softShadow(ImDrawList* drawList, ImVec2 min, ImVec2 max, float rounding, float size, ImVec4 color) {
    if (size <= 0.0f || color.w <= 0.0f) return;
    constexpr int kLayers = 10;
    for (int i = kLayers; i >= 1; --i) {
        const float t = static_cast<float>(i) / kLayers;
        const float spread = size * t;
        const float falloff = (1.0f - t) * (1.0f - t);
        ImVec4 layer = color;
        layer.w = color.w * falloff * (2.2f / kLayers);
        drawList->AddRectFilled(ImVec2(min.x - spread, min.y - spread + size * 0.18f),
                                ImVec2(max.x + spread, max.y + spread + size * 0.18f),
                                ImGui::GetColorU32(layer), rounding + spread);
    }
}

bool button(const char* label, ButtonKind kind, ImVec2 sizeArg) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 padding(style.FramePadding.x * 1.6f, style.FramePadding.y * 1.25f);
    const ImVec2 size = ImGui::CalcItemSize(sizeArg, labelSize.x + padding.x * 2.0f, labelSize.y + padding.y * 2.0f);
    const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + size.x, window->DC.CursorPos.y + size.y));
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const Interaction anim = animateInteraction(id, hovered, held);

    const ImVec4 surface = col(ImGuiCol_Button);
    const ImVec4 surfaceHover = col(ImGuiCol_ButtonHovered);
    ImVec4 rest;
    ImVec4 hot;
    ImVec4 text = col(ImGuiCol_Text);
    ImVec4 glow(0.0f, 0.0f, 0.0f, 0.0f);
    switch (kind) {
    case ButtonKind::Primary:
        rest = accent();
        hot = lighten(rest, 0.12f);
        text = textOn(rest);
        glow = withAlpha(rest, 0.45f);
        break;
    case ButtonKind::Success:
        rest = ImVec4(0.180f, 0.741f, 0.522f, 1.0f);
        hot = lighten(rest, 0.12f);
        text = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        glow = withAlpha(rest, 0.40f);
        break;
    case ButtonKind::Danger:
        rest = ImVec4(0.898f, 0.325f, 0.294f, 1.0f);
        hot = lighten(rest, 0.12f);
        text = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        glow = withAlpha(rest, 0.40f);
        break;
    case ButtonKind::Ghost:
        rest = withAlpha(surfaceHover, 0.0f);
        hot = surfaceHover;
        break;
    case ButtonKind::Secondary:
        rest = surface;
        hot = surfaceHover;
        break;
    }

    ImDrawList* drawList = window->DrawList;
    const float rounding = style.FrameRounding;
    const float inset = anim.press * 1.0f;
    const ImVec2 min(bb.Min.x + inset, bb.Min.y + inset);
    const ImVec2 max(bb.Max.x - inset, bb.Max.y - inset);

    if (glow.w > 0.0f) softShadow(drawList, min, max, rounding, 4.0f + 8.0f * anim.hover, withAlpha(glow, glow.w * (0.35f + 0.65f * anim.hover)));

    ImVec4 fill = lerp4(rest, hot, anim.hover);
    fill = lerp4(fill, darken(rest, 0.12f), anim.press * (kind == ButtonKind::Ghost ? 0.0f : 1.0f));
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(fill), rounding);
    if (kind == ButtonKind::Primary || kind == ButtonKind::Danger || kind == ButtonKind::Success) {
        drawList->AddRectFilledMultiColor(min, ImVec2(max.x, min.y + (max.y - min.y) * 0.5f), IM_COL32(255, 255, 255, 18),
                                          IM_COL32(255, 255, 255, 18), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    } else if (kind == ButtonKind::Secondary) {
        drawList->AddRect(min, max, mix(col(ImGuiCol_Border), withAlpha(accent(), 0.55f), anim.hover), rounding, 0, 1.0f);
    }
    drawFocusRing(drawList, id, bb.Min, bb.Max, rounding);

    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::RenderTextClipped(min, max, label, nullptr, &labelSize, ImVec2(0.5f, 0.5f), &bb);
    ImGui::PopStyleColor();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}

bool navItem(const char* label, bool selected, const char* subtitle, Icon icon) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float lineGap = subtitle != nullptr ? ImGui::GetTextLineHeight() * 0.85f : 0.0f;
    const float height = labelSize.y + lineGap + style.FramePadding.y * 3.2f;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + width, window->DC.CursorPos.y + height));
    ImGui::ItemSize(bb.GetSize(), 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const Interaction anim = animateInteraction(id, hovered, held);
    const float sel = animate(key(id, "selected"), selected ? 1.0f : 0.0f, 16.0f);

    ImDrawList* drawList = window->DrawList;
    const float rounding = style.FrameRounding;
    const ImVec4 hoverFill = withAlpha(col(ImGuiCol_Text), 0.05f);
    const ImVec4 selectedFill = withAlpha(accent(), 0.16f);
    ImVec4 fill = lerp4(withAlpha(hoverFill, 0.0f), hoverFill, anim.hover);
    fill = lerp4(fill, selectedFill, sel);
    drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(fill), rounding);

    if (sel > 0.001f) {
        const float pillHeight = (bb.GetHeight() - 14.0f) * sel;
        const float cy = (bb.Min.y + bb.Max.y) * 0.5f;
        drawList->AddRectFilled(ImVec2(bb.Min.x + 1.0f, cy - pillHeight * 0.5f), ImVec2(bb.Min.x + 4.0f, cy + pillHeight * 0.5f),
                                ImGui::GetColorU32(withAlpha(accent(), sel)), 2.0f);
    }
    drawFocusRing(drawList, id, bb.Min, bb.Max, rounding);

    const ImVec4 muted = col(ImGuiCol_TextDisabled);
    const ImVec4 bright = col(ImGuiCol_Text);
    const float emphasis = std::max(anim.hover * 0.6f, sel);
    float textX = bb.Min.x + style.FramePadding.x * 2.0f + 2.0f * sel;
    const float textY = bb.Min.y + (bb.GetHeight() - labelSize.y - lineGap) * 0.5f;
    if (icon != Icon::None) {
        const float iconSize = ImGui::GetTextLineHeight();
        drawIcon(drawList, icon, ImVec2(textX + iconSize * 0.5f, textY + labelSize.y * 0.5f), iconSize,
                 mix(lerp4(muted, bright, 0.3f), lerp4(bright, accent(), 0.65f), std::max(anim.hover * 0.5f, sel)));
        textX += iconSize + style.ItemInnerSpacing.x * 2.0f;
    }
    ImFont* font = core::kronosMediumFont();
    if (font != nullptr) ImGui::PushFont(font, 0.0f);
    drawList->AddText(ImVec2(textX, textY), mix(lerp4(muted, bright, 0.55f), bright, emphasis), label, ImGui::FindRenderedTextEnd(label));
    if (font != nullptr) ImGui::PopFont();
    if (subtitle != nullptr) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
        drawList->AddText(ImVec2(textX, textY + labelSize.y + 1.0f), ImGui::GetColorU32(muted), subtitle);
        ImGui::PopFont();
    }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}

bool iconButton(const char* idLabel, Icon icon, float size, bool dot) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID id = window->GetID(idLabel);
    const ImRect bb(window->DC.CursorPos, ImVec2(window->DC.CursorPos.x + size, window->DC.CursorPos.y + size));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const Interaction anim = animateInteraction(id, hovered, held);
    ImDrawList* drawList = window->DrawList;
    const float rounding = ImGui::GetStyle().FrameRounding;
    drawList->AddRectFilled(bb.Min, bb.Max, mix(withAlpha(col(ImGuiCol_ButtonHovered), 0.0f), col(ImGuiCol_ButtonHovered), anim.hover),
                            rounding);
    const ImVec2 center((bb.Min.x + bb.Max.x) * 0.5f, (bb.Min.y + bb.Max.y) * 0.5f + anim.press * 0.75f);
    drawIcon(drawList, icon, center, size * 0.52f, mix(lerp4(col(ImGuiCol_TextDisabled), col(ImGuiCol_Text), 0.4f), col(ImGuiCol_Text), anim.hover));
    if (dot) {
        const ImVec2 dotCenter(center.x + size * 0.17f, center.y - size * 0.19f);
        drawList->AddCircleFilled(dotCenter, size * 0.11f + 1.5f, ImGui::GetColorU32(ImGuiCol_WindowBg));
        drawList->AddCircleFilled(dotCenter, size * 0.11f, ImGui::GetColorU32(accent()));
    }
    drawFocusRing(drawList, id, bb.Min, bb.Max, rounding);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 color) {
    const float t = std::max(1.25f, s * 0.085f);
    const float h = s * 0.5f;
    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };
    switch (icon) {
    case Icon::None:
        break;
    case Icon::Home: {
        const ImVec2 roof[] = {P(-0.95f, -0.05f), P(0.0f, -0.88f), P(0.95f, -0.05f)};
        dl->AddPolyline(roof, 3, color, ImDrawFlags_None, t);
        const ImVec2 body[] = {P(-0.68f, -0.28f), P(-0.68f, 0.85f), P(0.68f, 0.85f), P(0.68f, -0.28f)};
        dl->AddPolyline(body, 4, color, ImDrawFlags_None, t);
        dl->AddRectFilled(P(-0.17f, 0.30f), P(0.17f, 0.85f), color, 1.0f);
        break;
    }
    case Icon::Discover: {
        dl->AddCircle(c, h * 0.9f, color, 0, t);
        const ImVec2 needle[] = {P(0.42f, -0.42f), P(0.13f, 0.13f), P(-0.42f, 0.42f), P(-0.13f, -0.13f)};
        dl->AddConvexPolyFilled(needle, 4, color);
        break;
    }
    case Icon::Avatar:
        dl->AddCircle(P(0.0f, -0.38f), h * 0.36f, color, 0, t);
        dl->PathArcTo(P(0.0f, 0.95f), h * 0.78f, IM_PI * 1.08f, IM_PI * 1.92f);
        dl->PathStroke(color, ImDrawFlags_None, t);
        break;
    case Icon::People:
        dl->AddCircle(P(-0.28f, -0.35f), h * 0.3f, color, 0, t);
        dl->PathArcTo(P(-0.28f, 0.85f), h * 0.6f, IM_PI * 1.08f, IM_PI * 1.92f);
        dl->PathStroke(color, ImDrawFlags_None, t);
        dl->AddCircle(P(0.42f, -0.22f), h * 0.24f, color, 0, t);
        dl->PathArcTo(P(0.5f, 0.85f), h * 0.46f, IM_PI * 1.2f, IM_PI * 1.95f);
        dl->PathStroke(color, ImDrawFlags_None, t);
        break;
    case Icon::Create:
        dl->AddRect(P(-0.85f, -0.85f), P(0.85f, 0.85f), color, h * 0.32f, 0, t);
        dl->AddLine(P(0.0f, -0.42f), P(0.0f, 0.42f), color, t);
        dl->AddLine(P(-0.42f, 0.0f), P(0.42f, 0.0f), color, t);
        break;
    case Icon::Settings: {
        constexpr int kTeeth = 8;
        ImVec2 points[kTeeth * 4];
        for (int i = 0; i < kTeeth; ++i) {
            const float base = (static_cast<float>(i) / kTeeth) * IM_PI * 2.0f;
            const float step = IM_PI * 2.0f / kTeeth;
            const float angles[4] = {base - step * 0.22f, base - step * 0.12f, base + step * 0.12f, base + step * 0.22f};
            const float radii[4] = {0.66f, 0.92f, 0.92f, 0.66f};
            for (int k = 0; k < 4; ++k) points[i * 4 + k] = P(std::cos(angles[k]) * radii[k], std::sin(angles[k]) * radii[k]);
        }
        dl->AddPolyline(points, kTeeth * 4, color, ImDrawFlags_Closed, t);
        dl->AddCircle(c, h * 0.3f, color, 0, t);
        break;
    }
    case Icon::Bell:
        dl->PathArcTo(P(0.0f, -0.25f), h * 0.55f, IM_PI, IM_PI * 2.0f);
        dl->PathLineTo(P(0.55f, 0.35f));
        dl->PathLineTo(P(0.82f, 0.62f));
        dl->PathLineTo(P(-0.82f, 0.62f));
        dl->PathLineTo(P(-0.55f, 0.35f));
        dl->PathStroke(color, ImDrawFlags_Closed, t);
        dl->PathArcTo(P(0.0f, 0.68f), h * 0.22f, 0.0f, IM_PI);
        dl->PathStroke(color, ImDrawFlags_None, t);
        dl->AddLine(P(0.0f, -0.92f), P(0.0f, -0.8f), color, t);
        break;
    case Icon::Chat: {
        dl->AddRect(P(-0.85f, -0.7f), P(0.85f, 0.45f), color, h * 0.3f, 0, t);
        const ImVec2 tail[] = {P(-0.45f, 0.45f), P(-0.55f, 0.88f), P(-0.05f, 0.45f)};
        dl->AddPolyline(tail, 3, color, ImDrawFlags_None, t);
        for (float x : {-0.4f, 0.0f, 0.4f}) dl->AddCircleFilled(P(x, -0.12f), t * 0.75f, color);
        break;
    }
    case Icon::Search:
        dl->AddCircle(P(-0.15f, -0.15f), h * 0.6f, color, 0, t);
        dl->AddLine(P(0.3f, 0.3f), P(0.85f, 0.85f), color, t * 1.15f);
        break;
    case Icon::Play: {
        const ImVec2 tri[] = {P(-0.5f, -0.75f), P(0.75f, 0.0f), P(-0.5f, 0.75f)};
        dl->AddConvexPolyFilled(tri, 3, color);
        break;
    }
    case Icon::Download: {
        dl->AddLine(P(0.0f, -0.85f), P(0.0f, 0.35f), color, t);
        const ImVec2 head[] = {P(-0.45f, -0.05f), P(0.0f, 0.4f), P(0.45f, -0.05f)};
        dl->AddPolyline(head, 3, color, ImDrawFlags_None, t);
        dl->AddLine(P(-0.85f, 0.85f), P(0.85f, 0.85f), color, t);
        break;
    }
    case Icon::Check: {
        const ImVec2 tick[] = {P(-0.7f, 0.0f), P(-0.2f, 0.5f), P(0.75f, -0.5f)};
        dl->AddPolyline(tick, 3, color, ImDrawFlags_None, t * 1.2f);
        break;
    }
    }
}

bool toggle(const char* label, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const float height = ImGui::GetFrameHeight();
    const float trackHeight = std::round(height * 0.8f);
    const float trackWidth = std::round(trackHeight * 1.8f);
    const float labelGap = labelSize.x > 0.0f ? style.ItemInnerSpacing.x * 1.5f : 0.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + trackWidth + labelGap + labelSize.x, pos.y + height));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed) {
        *value = !*value;
        ImGui::MarkItemEdited(id);
    }
    const Interaction anim = animateInteraction(id, hovered, held);
    const float on = animate(key(id, "on"), *value ? 1.0f : 0.0f, 16.0f);

    ImDrawList* drawList = window->DrawList;
    const ImVec2 trackMin(pos.x, pos.y + (height - trackHeight) * 0.5f);
    const ImVec2 trackMax(trackMin.x + trackWidth, trackMin.y + trackHeight);
    const ImVec4 offTrack = lighten(col(ImGuiCol_FrameBg), 0.06f + 0.05f * anim.hover);
    const ImVec4 onTrack = lighten(accent(), 0.08f * anim.hover);
    if (on > 0.0f) softShadow(drawList, trackMin, trackMax, trackHeight * 0.5f, 6.0f, withAlpha(accent(), 0.30f * on));
    drawList->AddRectFilled(trackMin, trackMax, mix(offTrack, onTrack, on), trackHeight * 0.5f);

    const float knobRadius = trackHeight * 0.5f - 2.5f;
    const float knobStretch = anim.press * 3.0f;
    const float travel = trackWidth - trackHeight - knobStretch;
    const float cx = trackMin.x + trackHeight * 0.5f + travel * on;
    const float cy = (trackMin.y + trackMax.y) * 0.5f;
    drawList->AddCircleFilled(ImVec2(cx + 0.5f, cy + 1.0f), knobRadius + 0.5f, IM_COL32(0, 0, 0, 60));
    drawList->AddRectFilled(ImVec2(cx - knobRadius, cy - knobRadius), ImVec2(cx + knobRadius + knobStretch, cy + knobRadius),
                            IM_COL32(255, 255, 255, 255), knobRadius);
    drawFocusRing(drawList, id, trackMin, trackMax, trackHeight * 0.5f);

    if (labelSize.x > 0.0f) {
        drawList->AddText(ImVec2(trackMax.x + labelGap, pos.y + (height - labelSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text),
                          label, ImGui::FindRenderedTextEnd(label));
    }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}

bool segmented(const char* id, const char* const* labels, int count, int* selected) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || count <= 0) return false;

    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushID(id);
    const ImGuiID baseId = window->GetID("##segmented");
    float segmentWidth = 0.0f;
    for (int i = 0; i < count; ++i) segmentWidth = std::max(segmentWidth, ImGui::CalcTextSize(labels[i], nullptr, true).x);
    segmentWidth += style.FramePadding.x * 3.0f;
    const float height = ImGui::GetFrameHeight() + 4.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + segmentWidth * count + 4.0f, pos.y + height));
    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, baseId)) {
        ImGui::PopID();
        return false;
    }

    ImDrawList* drawList = window->DrawList;
    drawList->AddRectFilled(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding + 2.0f);

    const float highlightX = animate(key(baseId, "highlight"), static_cast<float>(*selected), 18.0f);
    const ImVec2 hlMin(bb.Min.x + 2.0f + highlightX * segmentWidth, bb.Min.y + 2.0f);
    const ImVec2 hlMax(hlMin.x + segmentWidth, bb.Max.y - 2.0f);
    softShadow(drawList, hlMin, hlMax, style.FrameRounding, 4.0f, ImVec4(0.0f, 0.0f, 0.0f, 0.35f));
    drawList->AddRectFilled(hlMin, hlMax, ImGui::GetColorU32(ImGuiCol_ButtonHovered), style.FrameRounding);

    bool changed = false;
    for (int i = 0; i < count; ++i) {
        const ImRect seg(ImVec2(bb.Min.x + 2.0f + segmentWidth * i, bb.Min.y), ImVec2(bb.Min.x + 2.0f + segmentWidth * (i + 1), bb.Max.y));
        const ImGuiID segId = window->GetID(i);
        ImGui::KeepAliveID(segId);
        bool hovered = false;
        bool held = false;
        if (ImGui::ButtonBehavior(seg, segId, &hovered, &held) && *selected != i) {
            *selected = i;
            changed = true;
        }
        const float hot = animate(key(segId, "hot"), (hovered || *selected == i) ? 1.0f : 0.0f, 18.0f);
        const ImVec2 labelSize = ImGui::CalcTextSize(labels[i], nullptr, true);
        drawList->AddText(ImVec2(seg.Min.x + (seg.GetWidth() - labelSize.x) * 0.5f, seg.Min.y + (seg.GetHeight() - labelSize.y) * 0.5f),
                          mix(col(ImGuiCol_TextDisabled), col(ImGuiCol_Text), hot), labels[i], ImGui::FindRenderedTextEnd(labels[i]));
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (changed) ImGui::MarkItemEdited(baseId);
    ImGui::PopID();
    return changed;
}

void beginCard(const char* id, ImVec2 padding) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, std::max(style.ChildRounding, 10.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
}

void endCard() {
    ImGui::EndChild();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    softShadow(ImGui::GetWindowDrawList(), min, max, std::max(ImGui::GetStyle().ChildRounding, 10.0f), 10.0f,
               ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
}

void sectionHeader(const char* text) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImFont* font = core::kronosBoldFont();
    ImGui::PushFont(font, ImGui::GetStyle().FontSizeBase * 0.78f);
    ImGui::PushStyleColor(ImGuiCol_Text, col(ImGuiCol_TextDisabled));
    char upper[128];
    size_t n = 0;
    for (; text[n] != '\0' && n + 1 < sizeof(upper); ++n) {
        const char c = text[n];
        upper[n] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    upper[n] = '\0';
    ImGui::TextUnformatted(upper);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    const ImVec2 lineMin = ImGui::GetCursorScreenPos();
    window->DrawList->AddLine(ImVec2(lineMin.x, lineMin.y - 2.0f),
                              ImVec2(lineMin.x + ImGui::GetContentRegionAvail().x, lineMin.y - 2.0f),
                              ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

void pageTitle(const char* title, const char* subtitle) {
    ImFont* bold = core::kronosBoldFont();
    ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.6f);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(subtitle);
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
}

void settingLabel(const char* label, const char* description) {
    const float avail = ImGui::GetContentRegionAvail().x;
    const float labelWidth = std::max(180.0f, avail * 0.42f);
    const float startX = ImGui::GetCursorPosX();
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (description != nullptr) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.88f);
        ImGui::PushStyleColor(ImGuiCol_Text, col(ImGuiCol_TextDisabled));
        ImGui::PushTextWrapPos(startX + labelWidth - 16.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    ImGui::SameLine(startX + labelWidth);
    ImGui::SetNextItemWidth(std::min(avail - labelWidth, 420.0f));
}

void badge(const char* text, ImVec4 color) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 pad(7.0f, 2.0f);
    const ImVec2 pos = window->DC.CursorPos;
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 min(pos.x, pos.y + std::max(0.0f, (lineHeight - textSize.y) * 0.5f));
    const ImVec2 max(min.x + textSize.x + pad.x * 2.0f, min.y + textSize.y + pad.y * 2.0f);
    window->DrawList->AddRectFilled(min, max, ImGui::GetColorU32(withAlpha(color, 0.16f)), (max.y - min.y) * 0.5f);
    window->DrawList->AddRect(min, max, ImGui::GetColorU32(withAlpha(color, 0.35f)), (max.y - min.y) * 0.5f);
    window->DrawList->AddText(ImVec2(min.x + pad.x, min.y + pad.y), ImGui::GetColorU32(color), text);
    ImGui::Dummy(ImVec2(max.x - min.x, max.y - min.y));
    ImGui::PopFont();
}

} // namespace engine::ui

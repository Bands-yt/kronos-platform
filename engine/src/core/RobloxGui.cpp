#include "core/RobloxGui.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

namespace engine::core::gui {
namespace {

InstanceValue get(ECS& ecs, InstanceRef ref, const std::string& cls, const char* name) {
    InstanceValue value;
    if (const PropertyDef* def = instances::findProperty(cls, name)) (void)instances::getProperty(ecs, ref, *def, value);
    return value;
}

InstanceRef childOfClass(ECS& ecs, InstanceRef parent, const char* cls) {
    for (InstanceRef child : instances::children(ecs, parent)) {
        if (instances::className(ecs, child) == cls) return child;
    }
    return kNoInstance;
}

ImU32 color(const InstanceValue& rgb, double transparency, float shade = 1.0f) {
    const float alpha = std::clamp(1.0f - static_cast<float>(transparency), 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.vec.x * shade, rgb.vec.y * shade, rgb.vec.z * shade, alpha));
}

struct Stroke {
    bool on = false;
    ImU32 color = 0;
    float thickness = 0.0f;
};

// Contextual strokes outline text on text objects; Border strokes (and any
// stroke on a non-text object) outline the box.
Stroke strokeOf(ECS& ecs, InstanceRef ref, bool border) {
    const InstanceRef s = childOfClass(ecs, ref, "UIStroke");
    if (s == kNoInstance || !get(ecs, s, "UIStroke", "Enabled").boolean) return {};
    if ((get(ecs, s, "UIStroke", "ApplyStrokeMode").text == "Border") != border) return {};
    return {true, color(get(ecs, s, "UIStroke", "Color"), get(ecs, s, "UIStroke", "Transparency").number),
            static_cast<float>(get(ecs, s, "UIStroke", "Thickness").number)};
}

// UIGradient: multiplies the object's colours and fades it along a direction.
struct Gradient {
    bool on = false;
    std::vector<float> color; // time r g b
    std::vector<float> alpha; // time transparency envelope
    glm::vec2 center{0.0f};
    glm::vec2 dir{1.0f, 0.0f};
    float length = 1.0f;

    [[nodiscard]] float at(ImVec2 p) const { return 0.5f + ((p.x - center.x) * dir.x + (p.y - center.y) * dir.y) / length; }

    static float lerpKeys(const std::vector<float>& keys, size_t stride, size_t field, float t) {
        if (keys.size() < stride) return field == 1 && stride == 3 ? 0.0f : 1.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        for (size_t i = stride; i < keys.size(); i += stride) {
            if (t <= keys[i] || i + stride >= keys.size()) {
                const float t0 = keys[i - stride];
                const float span = keys[i] - t0;
                const float f = span > 1e-6f ? std::clamp((t - t0) / span, 0.0f, 1.0f) : 1.0f;
                return keys[i - stride + field] + (keys[i + field] - keys[i - stride + field]) * f;
            }
        }
        return keys[field];
    }

    [[nodiscard]] ImVec4 sample(float t) const {
        return ImVec4(lerpKeys(color, 4, 1, t), lerpKeys(color, 4, 2, t), lerpKeys(color, 4, 3, t),
                      1.0f - std::clamp(lerpKeys(alpha, 3, 1, t), 0.0f, 1.0f));
    }

    [[nodiscard]] ImU32 apply(ImVec4 base, ImVec2 p) const {
        const ImVec4 g = sample(at(p));
        return ImGui::ColorConvertFloat4ToU32(ImVec4(base.x * g.x, base.y * g.y, base.z * g.z, base.w * g.w));
    }

    // Where the colour changes slope: every keypoint time.
    [[nodiscard]] std::vector<float> cuts() const {
        std::vector<float> out = {-1e6f, 1e6f};
        for (size_t i = 0; i < color.size(); i += 4) out.push_back(color[i]);
        for (size_t i = 0; i < alpha.size(); i += 3) out.push_back(alpha[i]);
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
};

Gradient gradientOf(ECS& ecs, const Item& item, glm::vec2 origin) {
    Gradient g;
    const InstanceRef ref = childOfClass(ecs, item.ref, "UIGradient");
    if (ref == kNoInstance || !get(ecs, ref, "UIGradient", "Enabled").boolean) return g;
    g.on = true;
    g.color = get(ecs, ref, "UIGradient", "Color").keys;
    g.alpha = get(ecs, ref, "UIGradient", "Transparency").keys;
    const float radians = glm::radians(static_cast<float>(get(ecs, ref, "UIGradient", "Rotation").number));
    g.dir = glm::vec2(std::cos(radians), std::sin(radians));
    const InstanceValue offset = get(ecs, ref, "UIGradient", "Offset");
    g.center = origin + item.position + item.size * (0.5f + glm::vec2(offset.vec.x, offset.vec.y));
    g.length = std::max(1e-3f, std::abs(item.size.x * g.dir.x) + std::abs(item.size.y * g.dir.y));
    return g;
}

// Keeps the part of a convex polygon where the gradient position is >= lo
// (keepAbove) or <= hi.
std::vector<ImVec2> clipPolygon(const std::vector<ImVec2>& poly, const Gradient& g, float edge, bool keepAbove) {
    std::vector<ImVec2> out;
    const auto inside = [&](ImVec2 p) { return keepAbove ? g.at(p) >= edge : g.at(p) <= edge; };
    for (size_t i = 0; i < poly.size(); ++i) {
        const ImVec2 a = poly[i];
        const ImVec2 b = poly[(i + 1) % poly.size()];
        const bool ina = inside(a);
        const bool inb = inside(b);
        if (ina) out.push_back(a);
        if (ina != inb) {
            const float ta = g.at(a);
            const float tb = g.at(b);
            const float f = (edge - ta) / (tb - ta);
            out.emplace_back(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f);
        }
    }
    return out;
}

void fillGradient(ImDrawList* drawList, ImVec2 lo, ImVec2 hi, float rounding, ImVec4 base, const Gradient& g) {
    drawList->PathRect(lo, hi, rounding);
    const std::vector<ImVec2> shape(drawList->_Path.Data, drawList->_Path.Data + drawList->_Path.Size);
    drawList->PathClear();
    if (shape.size() < 3) return;
    const std::vector<float> cuts = g.cuts();
    const ImVec2 uv = drawList->_Data->TexUvWhitePixel;
    for (size_t c = 0; c + 1 < cuts.size(); ++c) {
        const std::vector<ImVec2> piece = clipPolygon(clipPolygon(shape, g, cuts[c], true), g, cuts[c + 1], false);
        if (piece.size() < 3) continue;
        const auto count = static_cast<int>(piece.size());
        drawList->PrimReserve((count - 2) * 3, count);
        const auto first = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
        for (const ImVec2& p : piece) drawList->PrimWriteVtx(p, uv, g.apply(base, p));
        for (int i = 2; i < count; ++i) {
            drawList->PrimWriteIdx(first);
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + i - 1));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + i));
        }
    }
}

// Tints vertices drawn since `start` (text, images) by the gradient.
void tintSince(ImDrawList* drawList, int start, const Gradient& g) {
    for (int i = start; i < drawList->VtxBuffer.Size; ++i) {
        ImDrawVert& v = drawList->VtxBuffer[i];
        v.col = g.apply(ImGui::ColorConvertU32ToFloat4(v.col), v.pos);
    }
}

bool isTextClass(const std::string& cls) { return cls == "TextLabel" || cls == "TextButton" || cls == "TextBox"; }

float textSize(ECS& ecs, ImFont* font, const Item& item, const std::string& cls, const std::string& text) {
    float size = static_cast<float>(get(ecs, item.ref, cls, "TextSize").number) * item.scale;
    if (!get(ecs, item.ref, cls, "TextScaled").boolean || text.empty()) return size;
    constexpr float kReference = 100.0f;
    const ImVec2 unit = font->CalcTextSizeA(kReference, FLT_MAX, 0.0f, text.c_str());
    size = std::min(item.size.y, unit.x > 0.0f ? item.size.x * kReference / unit.x : item.size.y);
    float lowest = 1.0f;
    float highest = 100.0f;
    if (const InstanceRef c = childOfClass(ecs, item.ref, "UITextSizeConstraint"); c != kNoInstance) {
        lowest = static_cast<float>(get(ecs, c, "UITextSizeConstraint", "MinTextSize").number);
        highest = std::max(lowest, static_cast<float>(get(ecs, c, "UITextSizeConstraint", "MaxTextSize").number));
    }
    return std::clamp(size, lowest, highest);
}

void drawText(ECS& ecs, ImDrawList* drawList, ImFont* font, const Item& item, const std::string& cls, ImVec2 lo,
              ImVec2 hi, const std::string& text, ImU32 textColor, const Stroke& stroke) {
    const float wrap = get(ecs, item.ref, cls, "TextWrapped").boolean ? item.size.x : 0.0f;
    const float size = textSize(ecs, font, item, cls, text);
    const ImVec2 measured = font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
    const std::string xAlign = get(ecs, item.ref, cls, "TextXAlignment").text;
    const std::string yAlign = get(ecs, item.ref, cls, "TextYAlignment").text;
    float x = lo.x + (item.size.x - measured.x) * 0.5f;
    if (xAlign == "Left") x = lo.x;
    if (xAlign == "Right") x = hi.x - measured.x;
    float y = lo.y + (item.size.y - measured.y) * 0.5f;
    if (yAlign == "Top") y = lo.y;
    if (yAlign == "Bottom") y = hi.y - measured.y;
    drawList->PushClipRect(lo, hi, true);
    if (stroke.on && stroke.thickness > 0.0f) {
        const float t = stroke.thickness;
        const ImVec2 offsets[] = {{-t, 0}, {t, 0}, {0, -t}, {0, t}, {-t, -t}, {t, t}, {-t, t}, {t, -t}};
        for (const ImVec2& d : offsets) {
            drawList->AddText(font, size, ImVec2(x + d.x, y + d.y), stroke.color, text.c_str(), nullptr, wrap);
        }
    }
    drawList->AddText(font, size, ImVec2(x, y), textColor, text.c_str(), nullptr, wrap);
    drawList->PopClipRect();
}

void drawScrollBar(ECS& ecs, ImDrawList* drawList, const Item& item, ImVec2 lo, ImVec2 hi) {
    const std::string cls = "ScrollingFrame";
    const float thickness = static_cast<float>(get(ecs, item.ref, cls, "ScrollBarThickness").number) * item.scale;
    if (thickness <= 0.0f) return;
    const InstanceValue canvas = get(ecs, item.ref, cls, "AbsoluteCanvasSize");
    const InstanceValue at = get(ecs, item.ref, cls, "CanvasPosition");
    const ImU32 barColor = color(get(ecs, item.ref, cls, "ScrollBarImageColor3"),
                                 get(ecs, item.ref, cls, "ScrollBarImageTransparency").number);
    if (canvas.vec.y > item.size.y + 0.5f) {
        const float length = item.size.y * item.size.y / canvas.vec.y;
        const float top = lo.y + (item.size.y - length) * at.vec.y / (canvas.vec.y - item.size.y);
        drawList->AddRectFilled(ImVec2(hi.x - thickness, top), ImVec2(hi.x, top + length), barColor);
    }
    if (canvas.vec.x > item.size.x + 0.5f) {
        const float length = item.size.x * item.size.x / canvas.vec.x;
        const float left = lo.x + (item.size.x - length) * at.vec.x / (canvas.vec.x - item.size.x);
        drawList->AddRectFilled(ImVec2(left, hi.y - thickness), ImVec2(left + length, hi.y), barColor);
    }
}

struct Editor {
    InstanceRef box = kNoInstance;
    std::string text;
    bool focusNext = false;
};

// The focused TextBox is edited by an ImGui text field over it.
void drawEditor(ECS& ecs, Editor& editor, const Item& item, ImVec2 lo, float fontSize) {
    const InstanceRef box = item.ref;
    if (editor.box != box) {
        editor.box = box;
        editor.text = get(ecs, box, "TextBox", "Text").text;
        editor.focusNext = true;
    }
    const InstanceValue rgb = get(ecs, box, "TextBox", "TextColor3");
    ImGui::SetNextWindowPos(lo);
    ImGui::SetNextWindowSize(ImVec2(item.size.x, item.size.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(rgb.vec.x, rgb.vec.y, rgb.vec.z, 1.0f));
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    bool submitted = false;
    bool lost = false;
    if (ImGui::Begin("##RobloxTextBox", nullptr, kFlags)) {
        ImGui::PushFont(nullptr, std::max(6.0f, fontSize));
        if (editor.focusNext) {
            ImGui::SetKeyboardFocusHere();
            editor.focusNext = false;
        }
        bool changed = false;
        if (get(ecs, box, "TextBox", "MultiLine").boolean) {
            changed = ImGui::InputTextMultiline("##text", &editor.text, ImVec2(item.size.x, item.size.y));
        } else {
            ImGui::SetNextItemWidth(item.size.x);
            ImGui::SetCursorPosY(std::max(0.0f, (item.size.y - ImGui::GetFrameHeight()) * 0.5f));
            submitted = ImGui::InputText("##text", &editor.text, ImGuiInputTextFlags_EnterReturnsTrue);
            changed = ImGui::IsItemEdited();
        }
        lost = !submitted && ImGui::IsItemDeactivated();
        if (changed || submitted) {
            if (const PropertyDef* def = instances::findProperty("TextBox", "Text")) {
                instances::setProperty(ecs, box, *def, InstanceValue::ofString(editor.text));
            }
        }
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    if (submitted || lost) {
        editor.box = kNoInstance;
        releaseFocus(ecs, box, submitted);
    }
}

} // namespace

void draw(ECS& ecs, ImDrawList* drawList, glm::vec2 origin) {
    static Editor editor;
    ImFont* font = ImGui::GetFont();
    const InstanceRef pressed = pressedButton(ecs);
    const InstanceRef hovered = hoveredButton(ecs);
    const InstanceRef focused = focusedTextBox(ecs);
    if (focused != editor.box) editor.box = kNoInstance;
    const Item* editing = nullptr;
    for (const Item& item : items(ecs)) {
        if (!instances::isAlive(ecs, item.ref)) continue;
        const std::string cls = instances::className(ecs, item.ref);
        const ImVec2 lo(origin.x + item.position.x, origin.y + item.position.y);
        const ImVec2 hi(lo.x + item.size.x, lo.y + item.size.y);
        drawList->PushClipRect(ImVec2(origin.x + item.clipMin.x, origin.y + item.clipMin.y),
                               ImVec2(origin.x + item.clipMax.x, origin.y + item.clipMax.y), true);

        float rounding = 0.0f;
        if (const InstanceRef corner = childOfClass(ecs, item.ref, "UICorner"); corner != kNoInstance) {
            const InstanceValue radius = get(ecs, corner, "UICorner", "CornerRadius");
            rounding = radius.vec.x * std::min(item.size.x, item.size.y) + radius.vec.y * item.scale;
        }
        float shade = 1.0f;
        if (instances::classIsA(cls, "GuiButton") && get(ecs, item.ref, cls, "AutoButtonColor").boolean) {
            if (pressed == item.ref) shade = 0.7f;
            else if (hovered == item.ref) shade = 0.85f;
        }
        const Gradient gradient = gradientOf(ecs, item, origin);
        const double background = get(ecs, item.ref, cls, "BackgroundTransparency").number;
        if (background < 1.0) {
            const ImU32 fill = color(get(ecs, item.ref, cls, "BackgroundColor3"), background, shade);
            if (gradient.on) {
                fillGradient(drawList, lo, hi, rounding, ImGui::ColorConvertU32ToFloat4(fill), gradient);
            } else {
                drawList->AddRectFilled(lo, hi, fill, rounding);
            }
            const double border = get(ecs, item.ref, cls, "BorderSizePixel").number;
            if (border > 0.0 && rounding <= 0.0) {
                drawList->AddRect(lo, hi, color(get(ecs, item.ref, cls, "BorderColor3"), background), 0.0f, 0,
                                  static_cast<float>(border));
            }
        }

        const int contentStart = drawList->VtxBuffer.Size;
        if (cls == "ImageLabel" || cls == "ImageButton") {
            if (void* texture = resolveImage(ecs, get(ecs, item.ref, cls, "Image").text)) {
                const ImU32 tint = color(get(ecs, item.ref, cls, "ImageColor3"),
                                         get(ecs, item.ref, cls, "ImageTransparency").number, shade);
                drawList->AddImageRounded(ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture))),
                                          lo, hi, ImVec2(0, 0), ImVec2(1, 1), tint, rounding);
            }
        }

        if (isTextClass(cls)) {
            if (cls == "TextBox" && focused == item.ref) {
                editing = &item;
            } else {
                std::string text = get(ecs, item.ref, cls, "Text").text;
                InstanceValue textColor = get(ecs, item.ref, cls, "TextColor3");
                if (cls == "TextBox" && text.empty()) {
                    text = get(ecs, item.ref, cls, "PlaceholderText").text;
                    textColor = get(ecs, item.ref, cls, "PlaceholderColor3");
                }
                drawText(ecs, drawList, font, item, cls, lo, hi, text,
                         color(textColor, get(ecs, item.ref, cls, "TextTransparency").number), strokeOf(ecs, item.ref, false));
            }
        }
        if (gradient.on) tintSince(drawList, contentStart, gradient);
        if (cls == "ScrollingFrame") drawScrollBar(ecs, drawList, item, lo, hi);

        Stroke border = strokeOf(ecs, item.ref, true);
        if (!border.on && !isTextClass(cls)) border = strokeOf(ecs, item.ref, false);
        if (border.on && border.thickness > 0.0f) {
            const float half = border.thickness * 0.5f;
            drawList->AddRect(ImVec2(lo.x - half, lo.y - half), ImVec2(hi.x + half, hi.y + half), border.color,
                              rounding > 0.0f ? rounding + half : 0.0f, 0, border.thickness);
        }
        drawList->PopClipRect();
    }
    if (editing != nullptr) {
        drawEditor(ecs, editor, *editing, ImVec2(origin.x + editing->position.x, origin.y + editing->position.y),
                   static_cast<float>(get(ecs, editing->ref, "TextBox", "TextSize").number) * editing->scale);
    }
}

} // namespace engine::core::gui

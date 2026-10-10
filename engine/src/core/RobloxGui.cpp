#include "core/RobloxGui.hpp"

#include <algorithm>
#include <cfloat>
#include <string>
#include <unordered_set>

#include <imgui.h>

#include "core/InstanceSignals.hpp"
#include "core/RobloxPlayers.hpp"

namespace engine::core::gui {
namespace {

struct State {
    std::vector<Item> items;
    std::unordered_set<InstanceRef> hovered;
    InstanceRef pressed = kNoInstance;
    InstanceRef topButton = kNoInstance;
};

State& stateOf(ECS& ecs) { return ecs.raw().ctx().emplace<State>(); }

InstanceValue get(ECS& ecs, InstanceRef ref, const std::string& cls, const char* name) {
    InstanceValue value;
    if (const PropertyDef* def = instances::findProperty(cls, name)) (void)instances::getProperty(ecs, ref, *def, value);
    return value;
}

void setOutput(ECS& ecs, InstanceRef ref, const std::string& cls, const char* name, glm::vec2 v) {
    const PropertyDef* def = instances::findProperty(cls, name);
    if (def == nullptr) return;
    InstanceValue current;
    (void)instances::getProperty(ecs, ref, *def, current);
    if (current.vec.x != v.x || current.vec.y != v.y) instances::setProperty(ecs, ref, *def, InstanceValue::ofVector2(v.x, v.y));
}

glm::vec2 resolve(const InstanceValue& udim2, glm::vec2 parent) {
    return {udim2.vec.x * parent.x + udim2.vec.y, udim2.vec.z * parent.y + static_cast<float>(udim2.number)};
}

float resolve(const InstanceValue& udim, float parent) { return udim.vec.x * parent + udim.vec.y; }

InstanceRef childOfClass(ECS& ecs, InstanceRef parent, const char* cls) {
    for (InstanceRef child : instances::children(ecs, parent)) {
        if (instances::className(ecs, child) == cls) return child;
    }
    return kNoInstance;
}

bool contains(const Item& item, glm::vec2 p) {
    const glm::vec2 lo = glm::max(item.position, item.clipMin);
    const glm::vec2 hi = glm::min(item.position + item.size, item.clipMax);
    return p.x >= lo.x && p.y >= lo.y && p.x < hi.x && p.y < hi.y;
}

void layoutChildren(ECS& ecs, State& s, InstanceRef parent, glm::vec2 position, glm::vec2 size, glm::vec2 clipMin,
                    glm::vec2 clipMax) {
    if (const InstanceRef padding = childOfClass(ecs, parent, "UIPadding"); padding != kNoInstance) {
        const float left = resolve(get(ecs, padding, "UIPadding", "PaddingLeft"), size.x);
        const float right = resolve(get(ecs, padding, "UIPadding", "PaddingRight"), size.x);
        const float top = resolve(get(ecs, padding, "UIPadding", "PaddingTop"), size.y);
        const float bottom = resolve(get(ecs, padding, "UIPadding", "PaddingBottom"), size.y);
        position += glm::vec2(left, top);
        size = glm::max(size - glm::vec2(left + right, top + bottom), glm::vec2(0.0f));
    }

    struct Kid {
        InstanceRef ref;
        std::string cls;
        glm::vec2 size;
        glm::vec2 position;
        double layoutOrder;
        double zIndex;
        std::string name;
    };
    std::vector<Kid> kids;
    for (InstanceRef child : instances::children(ecs, parent)) {
        const std::string cls = instances::className(ecs, child);
        if (!instances::classIsA(cls, "GuiObject") || !get(ecs, child, cls, "Visible").boolean) continue;
        const glm::vec2 kidSize = resolve(get(ecs, child, cls, "Size"), size);
        kids.push_back({child, cls, kidSize, glm::vec2(0.0f), get(ecs, child, cls, "LayoutOrder").number,
                        get(ecs, child, cls, "ZIndex").number, instances::name(ecs, child)});
    }

    if (const InstanceRef list = childOfClass(ecs, parent, "UIListLayout"); list != kNoInstance) {
        const bool byName = get(ecs, list, "UIListLayout", "SortOrder").text == "Name";
        std::stable_sort(kids.begin(), kids.end(), [&](const Kid& a, const Kid& b) {
            return byName ? a.name < b.name : a.layoutOrder < b.layoutOrder;
        });
        const bool vertical = get(ecs, list, "UIListLayout", "FillDirection").text != "Horizontal";
        const int main = vertical ? 1 : 0;
        const int cross = 1 - main;
        const float gap = resolve(get(ecs, list, "UIListLayout", "Padding"), size[main]);
        float total = 0.0f;
        for (const Kid& kid : kids) total += kid.size[main];
        if (!kids.empty()) total += gap * static_cast<float>(kids.size() - 1);
        const std::string mainAlign = get(ecs, list, "UIListLayout", vertical ? "VerticalAlignment" : "HorizontalAlignment").text;
        const std::string crossAlign = get(ecs, list, "UIListLayout", vertical ? "HorizontalAlignment" : "VerticalAlignment").text;
        float cursor = 0.0f;
        if (mainAlign == "Center") cursor = (size[main] - total) * 0.5f;
        if (mainAlign == "Bottom" || mainAlign == "Right") cursor = size[main] - total;
        for (Kid& kid : kids) {
            kid.position[main] = position[main] + cursor;
            float c = 0.0f;
            if (crossAlign == "Center") c = (size[cross] - kid.size[cross]) * 0.5f;
            if (crossAlign == "Right" || crossAlign == "Bottom") c = size[cross] - kid.size[cross];
            kid.position[cross] = position[cross] + c;
            cursor += kid.size[main] + gap;
        }
    } else {
        for (Kid& kid : kids) {
            const InstanceValue anchor = get(ecs, kid.ref, kid.cls, "AnchorPoint");
            kid.position = position + resolve(get(ecs, kid.ref, kid.cls, "Position"), size) -
                           glm::vec2(anchor.vec.x, anchor.vec.y) * kid.size;
        }
    }

    // Siblings draw in ZIndex order (Roblox's ZIndexBehavior.Sibling).
    std::stable_sort(kids.begin(), kids.end(), [](const Kid& a, const Kid& b) { return a.zIndex < b.zIndex; });
    for (const Kid& kid : kids) {
        setOutput(ecs, kid.ref, kid.cls, "AbsolutePosition", kid.position);
        setOutput(ecs, kid.ref, kid.cls, "AbsoluteSize", kid.size);
        s.items.push_back({kid.ref, kid.position, kid.size, clipMin, clipMax});
        glm::vec2 childMin = clipMin;
        glm::vec2 childMax = clipMax;
        if (get(ecs, kid.ref, kid.cls, "ClipsDescendants").boolean) {
            childMin = glm::max(childMin, kid.position);
            childMax = glm::min(childMax, kid.position + kid.size);
        }
        layoutChildren(ecs, s, kid.ref, kid.position, kid.size, childMin, childMax);
    }
}

InstanceRef playerGui(ECS& ecs) {
    const InstanceRef player = players::localPlayer(ecs);
    return player == kNoInstance ? kNoInstance : childOfClass(ecs, player, "PlayerGui");
}

void fire(ECS& ecs, InstanceRef ref, const char* event, std::vector<SignalArg> args = {}) {
    if (SignalHub* hub = signals::findHub(ecs)) hub->fire(ref, event, std::move(args));
}

ImU32 color(const InstanceValue& rgb, double transparency, float shade = 1.0f) {
    const float alpha = std::clamp(1.0f - static_cast<float>(transparency), 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(rgb.vec.x * shade, rgb.vec.y * shade, rgb.vec.z * shade, alpha));
}

} // namespace

void update(ECS& ecs, glm::vec2 screen, const Input& input) {
    State& s = stateOf(ecs);
    s.items.clear();
    const InstanceRef root = playerGui(ecs);
    if (root != kNoInstance) {
        std::vector<std::pair<double, InstanceRef>> screens;
        for (InstanceRef child : instances::children(ecs, root)) {
            const std::string cls = instances::className(ecs, child);
            if (cls == "ScreenGui" && get(ecs, child, cls, "Enabled").boolean) {
                screens.emplace_back(get(ecs, child, cls, "DisplayOrder").number, child);
            }
        }
        std::stable_sort(screens.begin(), screens.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [order, gui] : screens) {
            setOutput(ecs, gui, "ScreenGui", "AbsolutePosition", glm::vec2(0.0f));
            setOutput(ecs, gui, "ScreenGui", "AbsoluteSize", screen);
            layoutChildren(ecs, s, gui, glm::vec2(0.0f), screen, glm::vec2(0.0f), screen);
        }
    }

    std::unordered_set<InstanceRef> hovered;
    s.topButton = kNoInstance;
    for (auto it = s.items.rbegin(); it != s.items.rend(); ++it) {
        if (!contains(*it, input.mouse)) continue;
        hovered.insert(it->ref);
        if (s.topButton == kNoInstance && instances::classIsA(instances::className(ecs, it->ref), "GuiButton")) {
            s.topButton = it->ref;
        }
    }
    for (InstanceRef ref : hovered) {
        if (s.hovered.count(ref) == 0) fire(ecs, ref, "MouseEnter");
    }
    for (InstanceRef ref : s.hovered) {
        if (hovered.count(ref) == 0 && instances::isAlive(ecs, ref)) fire(ecs, ref, "MouseLeave");
    }
    s.hovered = std::move(hovered);

    const std::vector<SignalArg> at = {SignalArg::of(InstanceValue::ofNumber(input.mouse.x)),
                                       SignalArg::of(InstanceValue::ofNumber(input.mouse.y))};
    if (input.pressed) {
        s.pressed = s.topButton;
        if (s.pressed != kNoInstance) fire(ecs, s.pressed, "MouseButton1Down", at);
    }
    if (input.released) {
        if (s.topButton != kNoInstance) fire(ecs, s.topButton, "MouseButton1Up", at);
        if (s.topButton != kNoInstance && s.topButton == s.pressed) {
            fire(ecs, s.topButton, "MouseButton1Click");
            fire(ecs, s.topButton, "Activated");
        }
        s.pressed = kNoInstance;
    }
}

const std::vector<Item>& items(ECS& ecs) { return stateOf(ecs).items; }

bool mouseOverButton(ECS& ecs) { return stateOf(ecs).topButton != kNoInstance; }

void draw(ECS& ecs, ImDrawList* drawList, glm::vec2 origin) {
    State& s = stateOf(ecs);
    ImFont* font = ImGui::GetFont();
    for (const Item& item : s.items) {
        if (!instances::isAlive(ecs, item.ref)) continue;
        const std::string cls = instances::className(ecs, item.ref);
        const ImVec2 lo(origin.x + item.position.x, origin.y + item.position.y);
        const ImVec2 hi(lo.x + item.size.x, lo.y + item.size.y);
        drawList->PushClipRect(ImVec2(origin.x + item.clipMin.x, origin.y + item.clipMin.y),
                               ImVec2(origin.x + item.clipMax.x, origin.y + item.clipMax.y), true);

        float rounding = 0.0f;
        if (const InstanceRef corner = childOfClass(ecs, item.ref, "UICorner"); corner != kNoInstance) {
            rounding = resolve(get(ecs, corner, "UICorner", "CornerRadius"), std::min(item.size.x, item.size.y));
        }
        float shade = 1.0f;
        if (instances::classIsA(cls, "GuiButton") && get(ecs, item.ref, cls, "AutoButtonColor").boolean) {
            if (s.pressed == item.ref) shade = 0.7f;
            else if (s.topButton == item.ref) shade = 0.85f;
        }
        const double background = get(ecs, item.ref, cls, "BackgroundTransparency").number;
        if (background < 1.0) {
            drawList->AddRectFilled(lo, hi, color(get(ecs, item.ref, cls, "BackgroundColor3"), background, shade), rounding);
            const double border = get(ecs, item.ref, cls, "BorderSizePixel").number;
            if (border > 0.0 && rounding <= 0.0) {
                drawList->AddRect(lo, hi, color(get(ecs, item.ref, cls, "BorderColor3"), background), 0.0f, 0,
                                  static_cast<float>(border));
            }
        }

        if (cls == "TextLabel" || cls == "TextButton") {
            const std::string text = get(ecs, item.ref, cls, "Text").text;
            const bool wrapped = get(ecs, item.ref, cls, "TextWrapped").boolean;
            const float wrap = wrapped ? item.size.x : 0.0f;
            float size = static_cast<float>(get(ecs, item.ref, cls, "TextSize").number);
            if (get(ecs, item.ref, cls, "TextScaled").boolean && !text.empty()) {
                constexpr float kReference = 100.0f;
                const ImVec2 unit = font->CalcTextSizeA(kReference, FLT_MAX, 0.0f, text.c_str());
                size = std::min(item.size.y, unit.x > 0.0f ? item.size.x * kReference / unit.x : item.size.y);
                size = std::clamp(size, 1.0f, 100.0f);
            }
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
            drawList->AddText(font, size, ImVec2(x, y),
                              color(get(ecs, item.ref, cls, "TextColor3"), get(ecs, item.ref, cls, "TextTransparency").number),
                              text.c_str(), nullptr, wrap);
            drawList->PopClipRect();
        }
        drawList->PopClipRect();
    }
}

} // namespace engine::core::gui

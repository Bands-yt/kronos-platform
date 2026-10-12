#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>

#include "core/InstanceSignals.hpp"
#include "core/RobloxGui.hpp"
#include "core/RobloxPlayers.hpp"

namespace engine::core::gui {
namespace {

constexpr float kWheelStep = 40.0f;

struct State {
    std::vector<Item> items;
    std::unordered_set<InstanceRef> hovered;
    InstanceRef pressed = kNoInstance;
    InstanceRef topButton = kNoInstance;
    InstanceRef topTextBox = kNoInstance;
    InstanceRef focused = kNoInstance;
    ImageResolver images;
};

State& stateOf(ECS& ecs) { return ecs.raw().ctx().emplace<State>(); }

InstanceValue get(ECS& ecs, InstanceRef ref, const std::string& cls, const char* name) {
    InstanceValue value;
    if (const PropertyDef* def = instances::findProperty(cls, name)) (void)instances::getProperty(ecs, ref, *def, value);
    return value;
}

void set(ECS& ecs, InstanceRef ref, const std::string& cls, const char* name, const InstanceValue& value) {
    if (const PropertyDef* def = instances::findProperty(cls, name)) instances::setProperty(ecs, ref, *def, value);
}

void setOutput(ECS& ecs, InstanceRef ref, const std::string& cls, const char* name, glm::vec2 v) {
    const PropertyDef* def = instances::findProperty(cls, name);
    if (def == nullptr) return;
    InstanceValue current;
    (void)instances::getProperty(ecs, ref, *def, current);
    if (current.vec.x != v.x || current.vec.y != v.y) instances::setProperty(ecs, ref, *def, InstanceValue::ofVector2(v.x, v.y));
}

// UIScale multiplies pixel offsets; scale parts follow the (already scaled) parent.
glm::vec2 resolve(const InstanceValue& udim2, glm::vec2 parent, float scale = 1.0f) {
    return {udim2.vec.x * parent.x + udim2.vec.y * scale, udim2.vec.z * parent.y + static_cast<float>(udim2.number) * scale};
}

float resolve(const InstanceValue& udim, float parent, float scale = 1.0f) { return udim.vec.x * parent + udim.vec.y * scale; }

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

glm::vec2 applyConstraints(ECS& ecs, InstanceRef ref, glm::vec2 size) {
    if (const InstanceRef c = childOfClass(ecs, ref, "UISizeConstraint"); c != kNoInstance) {
        const InstanceValue lo = get(ecs, c, "UISizeConstraint", "MinSize");
        const InstanceValue hi = get(ecs, c, "UISizeConstraint", "MaxSize");
        size = glm::clamp(size, glm::vec2(lo.vec.x, lo.vec.y), glm::max(glm::vec2(lo.vec.x, lo.vec.y), glm::vec2(hi.vec.x, hi.vec.y)));
    }
    if (const InstanceRef c = childOfClass(ecs, ref, "UIAspectRatioConstraint"); c != kNoInstance) {
        const float ratio = std::max(1e-4f, static_cast<float>(get(ecs, c, "UIAspectRatioConstraint", "AspectRatio").number));
        if (get(ecs, c, "UIAspectRatioConstraint", "AspectType").text == "ScaleWithParentSize") {
            if (get(ecs, c, "UIAspectRatioConstraint", "DominantAxis").text == "Height") {
                size.x = size.y * ratio;
            } else {
                size.y = size.x / ratio;
            }
        } else if (size.y > 0.0f && size.x / size.y > ratio) {
            size.x = size.y * ratio;
        } else {
            size.y = size.x / ratio;
        }
    }
    return size;
}

struct Kid {
    InstanceRef ref;
    std::string cls;
    glm::vec2 size;
    glm::vec2 position;
    double layoutOrder;
    double zIndex;
    std::string name;
    float scale;
};

void sortForLayout(ECS& ecs, InstanceRef layout, const std::string& cls, std::vector<Kid>& kids) {
    const bool byName = get(ecs, layout, cls, "SortOrder").text == "Name";
    std::stable_sort(kids.begin(), kids.end(), [&](const Kid& a, const Kid& b) {
        return byName ? a.name < b.name : a.layoutOrder < b.layoutOrder;
    });
}

float alignOffset(const std::string& align, float space, float content) {
    if (align == "Center") return (space - content) * 0.5f;
    if (align == "Right" || align == "Bottom") return space - content;
    return 0.0f;
}

// Returns the content's extent (bottom-right relative to `position`).
glm::vec2 layoutChildren(ECS& ecs, State& s, InstanceRef parent, glm::vec2 position, glm::vec2 size, glm::vec2 clipMin,
                         glm::vec2 clipMax, float scale) {
    if (const InstanceRef padding = childOfClass(ecs, parent, "UIPadding"); padding != kNoInstance) {
        const float left = resolve(get(ecs, padding, "UIPadding", "PaddingLeft"), size.x, scale);
        const float right = resolve(get(ecs, padding, "UIPadding", "PaddingRight"), size.x, scale);
        const float top = resolve(get(ecs, padding, "UIPadding", "PaddingTop"), size.y, scale);
        const float bottom = resolve(get(ecs, padding, "UIPadding", "PaddingBottom"), size.y, scale);
        position += glm::vec2(left, top);
        size = glm::max(size - glm::vec2(left + right, top + bottom), glm::vec2(0.0f));
    }

    std::vector<Kid> kids;
    for (InstanceRef child : instances::children(ecs, parent)) {
        const std::string cls = instances::className(ecs, child);
        if (!instances::classIsA(cls, "GuiObject") || !get(ecs, child, cls, "Visible").boolean) continue;
        float own = 1.0f;
        if (const InstanceRef uiScale = childOfClass(ecs, child, "UIScale"); uiScale != kNoInstance) {
            own = std::max(0.0f, static_cast<float>(get(ecs, uiScale, "UIScale", "Scale").number));
        }
        const glm::vec2 kidSize = applyConstraints(ecs, child, resolve(get(ecs, child, cls, "Size"), size, scale) * own);
        kids.push_back({child, cls, kidSize, glm::vec2(0.0f), get(ecs, child, cls, "LayoutOrder").number,
                        get(ecs, child, cls, "ZIndex").number, instances::name(ecs, child), scale * own});
    }

    glm::vec2 extent(0.0f);
    if (const InstanceRef grid = childOfClass(ecs, parent, "UIGridLayout"); grid != kNoInstance) {
        sortForLayout(ecs, grid, "UIGridLayout", kids);
        const glm::vec2 cell = applyConstraints(ecs, grid, resolve(get(ecs, grid, "UIGridLayout", "CellSize"), size, scale));
        const glm::vec2 pad = resolve(get(ecs, grid, "UIGridLayout", "CellPadding"), size, scale);
        const bool horizontal = get(ecs, grid, "UIGridLayout", "FillDirection").text != "Vertical";
        const int main = horizontal ? 0 : 1;
        int perLine = std::max(1, static_cast<int>(std::floor((size[main] + pad[main]) / std::max(1.0f, cell[main] + pad[main]))));
        const int maxCells = static_cast<int>(get(ecs, grid, "UIGridLayout", "FillDirectionMaxCells").number);
        if (maxCells > 0) perLine = std::min(perLine, maxCells);
        const int count = static_cast<int>(kids.size());
        const int lines = count == 0 ? 0 : (count + perLine - 1) / perLine;
        glm::vec2 content(0.0f);
        content[main] = count == 0 ? 0.0f : static_cast<float>(std::min(count, perLine)) * (cell[main] + pad[main]) - pad[main];
        content[1 - main] = lines == 0 ? 0.0f : static_cast<float>(lines) * (cell[1 - main] + pad[1 - main]) - pad[1 - main];
        const glm::vec2 start(alignOffset(get(ecs, grid, "UIGridLayout", "HorizontalAlignment").text, size.x, content.x),
                              alignOffset(get(ecs, grid, "UIGridLayout", "VerticalAlignment").text, size.y, content.y));
        for (int i = 0; i < count; ++i) {
            glm::vec2 slot(0.0f);
            slot[main] = static_cast<float>(i % perLine) * (cell[main] + pad[main]);
            slot[1 - main] = static_cast<float>(i / perLine) * (cell[1 - main] + pad[1 - main]);
            kids[static_cast<size_t>(i)].size = cell;
            kids[static_cast<size_t>(i)].position = position + start + slot;
        }
        setOutput(ecs, grid, "UIGridLayout", "AbsoluteCellSize", cell);
        setOutput(ecs, grid, "UIGridLayout", "AbsoluteContentSize", content);
        extent = start + content;
    } else if (const InstanceRef list = childOfClass(ecs, parent, "UIListLayout"); list != kNoInstance) {
        sortForLayout(ecs, list, "UIListLayout", kids);
        const bool vertical = get(ecs, list, "UIListLayout", "FillDirection").text != "Horizontal";
        const int main = vertical ? 1 : 0;
        const int cross = 1 - main;
        const float gap = resolve(get(ecs, list, "UIListLayout", "Padding"), size[main], scale);
        float total = 0.0f;
        float widest = 0.0f;
        for (const Kid& kid : kids) {
            total += kid.size[main];
            widest = std::max(widest, kid.size[cross]);
        }
        if (!kids.empty()) total += gap * static_cast<float>(kids.size() - 1);
        const std::string mainAlign = get(ecs, list, "UIListLayout", vertical ? "VerticalAlignment" : "HorizontalAlignment").text;
        const std::string crossAlign = get(ecs, list, "UIListLayout", vertical ? "HorizontalAlignment" : "VerticalAlignment").text;
        float cursor = alignOffset(mainAlign, size[main], total);
        for (Kid& kid : kids) {
            kid.position[main] = position[main] + cursor;
            kid.position[cross] = position[cross] + alignOffset(crossAlign, size[cross], kid.size[cross]);
            cursor += kid.size[main] + gap;
        }
        glm::vec2 content(0.0f);
        content[main] = total;
        content[cross] = widest;
        setOutput(ecs, list, "UIListLayout", "AbsoluteContentSize", content);
        extent = content;
        extent[main] += alignOffset(mainAlign, size[main], total);
    } else {
        for (Kid& kid : kids) {
            const InstanceValue anchor = get(ecs, kid.ref, kid.cls, "AnchorPoint");
            kid.position = position + resolve(get(ecs, kid.ref, kid.cls, "Position"), size, scale) -
                           glm::vec2(anchor.vec.x, anchor.vec.y) * kid.size;
            extent = glm::max(extent, kid.position + kid.size - position);
        }
    }

    // Siblings draw in ZIndex order (Roblox's ZIndexBehavior.Sibling).
    std::stable_sort(kids.begin(), kids.end(), [](const Kid& a, const Kid& b) { return a.zIndex < b.zIndex; });
    for (const Kid& kid : kids) {
        setOutput(ecs, kid.ref, kid.cls, "AbsolutePosition", kid.position);
        setOutput(ecs, kid.ref, kid.cls, "AbsoluteSize", kid.size);
        s.items.push_back({kid.ref, kid.position, kid.size, clipMin, clipMax, kid.scale});
        glm::vec2 childMin = clipMin;
        glm::vec2 childMax = clipMax;
        const bool scrolling = kid.cls == "ScrollingFrame";
        if (scrolling || get(ecs, kid.ref, kid.cls, "ClipsDescendants").boolean) {
            childMin = glm::max(childMin, kid.position);
            childMax = glm::min(childMax, kid.position + kid.size);
        }
        if (!scrolling) {
            layoutChildren(ecs, s, kid.ref, kid.position, kid.size, childMin, childMax, kid.scale);
            continue;
        }
        // Children live on the canvas, moved by CanvasPosition.
        glm::vec2 canvas = glm::max(resolve(get(ecs, kid.ref, kid.cls, "CanvasSize"), kid.size, kid.scale), glm::vec2(0.0f));
        const std::string automatic = get(ecs, kid.ref, kid.cls, "AutomaticCanvasSize").text;
        const InstanceValue scrolled = get(ecs, kid.ref, kid.cls, "CanvasPosition");
        glm::vec2 offset(scrolled.vec.x, scrolled.vec.y);
        const size_t first = s.items.size();
        glm::vec2 content = layoutChildren(ecs, s, kid.ref, kid.position - offset, glm::max(canvas, kid.size), childMin,
                                           childMax, kid.scale);
        if (automatic != "None") {
            glm::vec2 grown = canvas;
            if (automatic == "X" || automatic == "XY") grown.x = std::max(canvas.x, content.x);
            if (automatic == "Y" || automatic == "XY") grown.y = std::max(canvas.y, content.y);
            if (grown != canvas) {
                canvas = grown;
                s.items.resize(first);
                content = layoutChildren(ecs, s, kid.ref, kid.position - offset, glm::max(canvas, kid.size), childMin,
                                         childMax, kid.scale);
            }
        }
        const glm::vec2 limit = glm::max(canvas - kid.size, glm::vec2(0.0f));
        const glm::vec2 clamped = glm::clamp(offset, glm::vec2(0.0f), limit);
        if (clamped != offset) {
            set(ecs, kid.ref, kid.cls, "CanvasPosition", InstanceValue::ofVector2(clamped.x, clamped.y));
            s.items.resize(first);
            layoutChildren(ecs, s, kid.ref, kid.position - clamped, glm::max(canvas, kid.size), childMin, childMax, kid.scale);
        }
        setOutput(ecs, kid.ref, kid.cls, "AbsoluteCanvasSize", glm::max(canvas, kid.size));
        setOutput(ecs, kid.ref, kid.cls, "AbsoluteWindowSize", kid.size);
    }
    return extent;
}

InstanceRef playerGui(ECS& ecs) {
    const InstanceRef player = players::localPlayer(ecs);
    return player == kNoInstance ? kNoInstance : childOfClass(ecs, player, "PlayerGui");
}

void fire(ECS& ecs, InstanceRef ref, const char* event, std::vector<SignalArg> args = {}) {
    if (SignalHub* hub = signals::findHub(ecs)) hub->fire(ref, event, std::move(args));
}

void scroll(ECS& ecs, const State& s, glm::vec2 mouse, float wheel) {
    for (auto it = s.items.rbegin(); it != s.items.rend(); ++it) {
        if (!contains(*it, mouse) || instances::className(ecs, it->ref) != "ScrollingFrame") continue;
        const std::string cls = "ScrollingFrame";
        if (!get(ecs, it->ref, cls, "ScrollingEnabled").boolean) continue;
        const InstanceValue canvas = get(ecs, it->ref, cls, "AbsoluteCanvasSize");
        const glm::vec2 room = glm::max(glm::vec2(canvas.vec.x, canvas.vec.y) - it->size, glm::vec2(0.0f));
        const std::string direction = get(ecs, it->ref, cls, "ScrollingDirection").text;
        const bool alongY = room.y > 0.0f && direction != "X";
        const bool alongX = !alongY && room.x > 0.0f && direction != "Y";
        if (!alongX && !alongY) continue;
        const InstanceValue at = get(ecs, it->ref, cls, "CanvasPosition");
        glm::vec2 position(at.vec.x, at.vec.y);
        position[alongY ? 1 : 0] -= wheel * kWheelStep;
        position = glm::clamp(position, glm::vec2(0.0f), room);
        set(ecs, it->ref, cls, "CanvasPosition", InstanceValue::ofVector2(position.x, position.y));
        return;
    }
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
        std::stable_sort(screens.begin(), screens.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [order, gui] : screens) {
            setOutput(ecs, gui, "ScreenGui", "AbsolutePosition", glm::vec2(0.0f));
            setOutput(ecs, gui, "ScreenGui", "AbsoluteSize", screen);
            layoutChildren(ecs, s, gui, glm::vec2(0.0f), screen, glm::vec2(0.0f), screen, 1.0f);
        }
    }

    // A focused TextBox that left the screen loses the focus.
    if (s.focused != kNoInstance &&
        std::none_of(s.items.begin(), s.items.end(), [&](const Item& item) { return item.ref == s.focused; })) {
        releaseFocus(ecs, s.focused, false);
    }

    std::unordered_set<InstanceRef> hovered;
    s.topButton = kNoInstance;
    s.topTextBox = kNoInstance;
    bool blocked = false;
    for (auto it = s.items.rbegin(); it != s.items.rend(); ++it) {
        if (!contains(*it, input.mouse)) continue;
        hovered.insert(it->ref);
        if (blocked) continue;
        const std::string cls = instances::className(ecs, it->ref);
        if (instances::classIsA(cls, "GuiButton")) {
            s.topButton = it->ref;
            blocked = true;
        } else if (cls == "TextBox") {
            s.topTextBox = it->ref;
            blocked = true;
        }
    }
    for (InstanceRef ref : hovered) {
        if (s.hovered.count(ref) == 0) fire(ecs, ref, "MouseEnter");
    }
    for (InstanceRef ref : s.hovered) {
        if (hovered.count(ref) == 0 && instances::isAlive(ecs, ref)) fire(ecs, ref, "MouseLeave");
    }
    s.hovered = std::move(hovered);

    if (input.wheel != 0.0f) scroll(ecs, s, input.mouse, input.wheel);

    const std::vector<SignalArg> at = {SignalArg::of(InstanceValue::ofNumber(input.mouse.x)),
                                       SignalArg::of(InstanceValue::ofNumber(input.mouse.y))};
    if (input.pressed) {
        s.pressed = s.topButton;
        if (s.pressed != kNoInstance) fire(ecs, s.pressed, "MouseButton1Down", at);
        if (s.topTextBox != kNoInstance && get(ecs, s.topTextBox, "TextBox", "TextEditable").boolean) {
            captureFocus(ecs, s.topTextBox);
        } else if (s.focused != kNoInstance) {
            releaseFocus(ecs, s.focused, false);
        }
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

bool mouseOverButton(ECS& ecs) {
    const State& s = stateOf(ecs);
    return s.topButton != kNoInstance || s.topTextBox != kNoInstance;
}

InstanceRef pressedButton(ECS& ecs) { return stateOf(ecs).pressed; }
InstanceRef hoveredButton(ECS& ecs) { return stateOf(ecs).topButton; }

InstanceRef focusedTextBox(ECS& ecs) {
    State& s = stateOf(ecs);
    if (s.focused != kNoInstance && !instances::isAlive(ecs, s.focused)) s.focused = kNoInstance;
    return s.focused;
}

void captureFocus(ECS& ecs, InstanceRef box) {
    State& s = stateOf(ecs);
    if (s.focused == box || !instances::isAlive(ecs, box) || instances::className(ecs, box) != "TextBox") return;
    if (s.focused != kNoInstance) releaseFocus(ecs, s.focused, false);
    s.focused = box;
    if (get(ecs, box, "TextBox", "ClearTextOnFocus").boolean) set(ecs, box, "TextBox", "Text", InstanceValue::ofString(""));
    fire(ecs, box, "Focused");
}

void releaseFocus(ECS& ecs, InstanceRef box, bool submitted) {
    State& s = stateOf(ecs);
    if (s.focused != box || box == kNoInstance) return;
    s.focused = kNoInstance;
    if (instances::isAlive(ecs, box)) {
        fire(ecs, box, "FocusLost", {SignalArg::of(InstanceValue::ofBool(submitted)), SignalArg::of(InstanceValue{})});
    }
}

void setImageResolver(ECS& ecs, ImageResolver resolver) { stateOf(ecs).images = std::move(resolver); }

void* resolveImage(ECS& ecs, const std::string& image) {
    const State& s = stateOf(ecs);
    return s.images && !image.empty() ? s.images(image) : nullptr;
}

} // namespace engine::core::gui

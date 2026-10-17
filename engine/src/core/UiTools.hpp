#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::uitools {

struct Rgb {
    float r, g, b;
};

// Themes and contrast (7, 40).
inline float luminance(Rgb c) {
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

inline float contrastRatio(Rgb a, Rgb b) {
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

struct ThemeTokens {
    Rgb background, text, accent;
};

inline ThemeTokens themeNamed(const std::string& name) {
    if (name == "light") return {{0.96f, 0.96f, 0.95f}, {0.1f, 0.1f, 0.12f}, {0.15f, 0.35f, 0.8f}};
    if (name == "high-contrast") return {{0, 0, 0}, {1, 1, 1}, {1, 0.9f, 0}};
    return {{0.09f, 0.1f, 0.12f}, {0.9f, 0.91f, 0.92f}, {0.45f, 0.62f, 1.0f}};
}

inline Rgb statusColor(const std::string& kind) {
    if (kind == "error") return {0.9f, 0.25f, 0.25f};
    if (kind == "warning") return {0.95f, 0.7f, 0.15f};
    if (kind == "selection") return {0.3f, 0.6f, 1.0f};
    if (kind == "success") return {0.3f, 0.75f, 0.45f};
    return {0.6f, 0.62f, 0.65f};
}

// Spacing scale (8): 4 px steps with a 2 px half step at 0.
inline float spacing(int step) { return step <= 0 ? 2.0f : 4.0f * static_cast<float>(step); }

// Icons (9): a name always maps to some icon.
inline std::string iconFor(const std::string& what) {
    static const std::map<std::string, std::string> icons = {{"part", "cube"}, {"script", "code"}, {"light", "sun"}, {"sound", "speaker"}, {"camera", "camera"}, {"folder", "folder"}};
    auto it = icons.find(what);
    return it == icons.end() ? "question" : it->second;
}

// Help panel (11) and empty states (15, 42).
inline std::string helpFor(const std::string& panel) {
    static const std::map<std::string, std::string> help = {
        {"Explorer", "Shows every object in your game. Click one to select it."},
        {"Inspector", "Shows the properties of the selected object. Change them here."},
        {"Script Editor", "Write Luau or Python code for your game."}};
    auto it = help.find(panel);
    return it == help.end() ? "No help for this panel yet." : it->second;
}

inline std::string emptyStateText(const std::string& panel) {
    if (panel == "Explorer") return "Nothing here yet. Use Insert to add your first part.";
    if (panel == "Inspector") return "Select an object to see its properties.";
    if (panel == "Script Editor") return "Open a script from the Explorer, or create a new one.";
    return "This panel is empty.";
}

// Undo status (12).
inline std::string undoStatus(bool canUndo, const std::string& label) { return canUndo ? "Undo " + label : "Nothing to undo"; }

// Reduced motion (24).
inline float animationSeconds(float base, bool reducedMotion) { return reducedMotion ? 0.0f : base; }

// Tabs (30): which gap between tabs a drag at x would drop into.
inline int tabDropIndex(const std::vector<float>& tabWidths, float x) {
    float edge = 0;
    for (std::size_t i = 0; i < tabWidths.size(); ++i) {
        if (x < edge + tabWidths[i] * 0.5f) return static_cast<int>(i);
        edge += tabWidths[i];
    }
    return static_cast<int>(tabWidths.size());
}

// Dock preview (31): the rectangle a panel would take if dropped on a zone of a host rectangle.
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
};

inline Rect dockPreview(const std::string& zone, Rect host) {
    if (zone == "left") return {host.x, host.y, host.w * 0.3f, host.h};
    if (zone == "right") return {host.x + host.w * 0.7f, host.y, host.w * 0.3f, host.h};
    if (zone == "top") return {host.x, host.y, host.w, host.h * 0.3f};
    if (zone == "bottom") return {host.x, host.y + host.h * 0.7f, host.w, host.h * 0.3f};
    return host;
}

// Panel search (32, 61, 73): case-insensitive substring filter that keeps order.
inline std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::vector<std::string> filterNames(const std::vector<std::string>& names, const std::string& query) {
    std::vector<std::string> out;
    const std::string q = lower(query);
    for (const auto& n : names) {
        if (q.empty() || lower(n).find(q) != std::string::npos) out.push_back(n);
    }
    return out;
}

// Quick mode switch (34).
inline std::string nextMode(const std::string& current) {
    static const std::vector<std::string> modes = {"Studio", "Player", "Movie Maker"};
    for (std::size_t i = 0; i < modes.size(); ++i) {
        if (modes[i] == current) return modes[(i + 1) % modes.size()];
    }
    return modes[0];
}

// Layout for small screens (36) and DPI (37, 38).
inline bool useCompactLayout(int screenHeight) { return screenHeight <= 800; }

inline std::string ellipsize(const std::string& text, std::size_t maxChars) {
    if (text.size() <= maxChars || maxChars < 4) return text.size() <= maxChars ? text : text.substr(0, maxChars);
    return text.substr(0, maxChars - 3) + "...";
}

inline float scaledSize(float base, float dpiScale) { return std::round(base * dpiScale); }

inline float hitTargetSize(float visual, float dpiScale) { return std::max(scaledSize(visual, dpiScale), scaledSize(24.0f, dpiScale)); }

// Property rows (39).
inline float labelColumnWidth(const std::vector<std::string>& names, float charWidth, float maxWidth) {
    std::size_t longest = 0;
    for (const auto& n : names) longest = std::max(longest, n.size());
    return std::min(static_cast<float>(longest) * charWidth + 12.0f, maxWidth);
}

// Style guide (41): one line per token.
inline std::string styleGuide(const std::string& themeName) {
    const ThemeTokens t = themeNamed(themeName);
    std::ostringstream out;
    out << "Theme " << themeName << "\nText on background contrast: " << contrastRatio(t.text, t.background) << "\nSpacing steps: 2, 4, 8, 12, 16\n";
    return out.str();
}

// Panel shortcuts (43): two panels may not claim the same key.
inline std::vector<std::string> shortcutConflicts(const std::vector<std::pair<std::string, std::string>>& panelKey) {
    std::map<std::string, std::string> seen;
    std::vector<std::string> out;
    for (const auto& [panel, key] : panelKey) {
        auto it = seen.find(key);
        if (it != seen.end()) out.push_back(key + " is used by " + it->second + " and " + panel);
        else seen[key] = panel;
    }
    return out;
}

// Pop-out window (45): where a panel opens when torn off.
inline Rect popOutRect(Rect panel, float screenW, float screenH) {
    Rect r{panel.x + 24, panel.y + 24, std::max(panel.w, 400.0f), std::max(panel.h, 300.0f)};
    r.x = std::clamp(r.x, 0.0f, std::max(0.0f, screenW - r.w));
    r.y = std::clamp(r.y, 0.0f, std::max(0.0f, screenH - r.h));
    return r;
}

// UI lint (50): problems in a list of (id, label, maxChars).
struct UiElement {
    std::string id, label;
    std::size_t maxChars = 40;
};

inline std::vector<std::string> lintUi(const std::vector<UiElement>& elements) {
    std::vector<std::string> out;
    std::set<std::string> ids;
    for (const auto& e : elements) {
        if (e.label.empty()) out.push_back(e.id + ": empty label");
        if (e.label.size() > e.maxChars) out.push_back(e.id + ": label longer than " + std::to_string(e.maxChars));
        if (!ids.insert(e.id).second) out.push_back(e.id + ": duplicate id");
    }
    return out;
}

// Components (59, 60): add or remove on many objects, group properties by owner.
enum class BulkOp { Add, Remove, Reset };

inline std::vector<std::set<std::string>> bulkApply(std::vector<std::set<std::string>> objects, const std::string& component, BulkOp op) {
    for (auto& o : objects) {
        if (op == BulkOp::Add) o.insert(component);
        else if (op == BulkOp::Remove) o.erase(component);
    }
    return objects;
}

inline std::map<std::string, std::vector<std::string>> groupByComponent(const std::vector<std::pair<std::string, std::string>>& componentProperty) {
    std::map<std::string, std::vector<std::string>> out;
    for (const auto& [c, p] : componentProperty) out[c].push_back(p);
    return out;
}

// Mixed values (62).
inline std::string mixedDisplay(const std::vector<std::string>& values) {
    if (values.empty()) return "";
    for (const auto& v : values) {
        if (v != values.front()) return "(mixed)";
    }
    return values.front();
}

inline std::string formatVec3(float x, float y, float z) {
    auto f = [](float v) {
        char b[24];
        std::snprintf(b, sizeof(b), "%.3f", std::fabs(v) < 0.0005f ? 0.0f : v);
        return std::string(b);
    };
    return f(x) + ", " + f(y) + ", " + f(z);
}

// Entity picker (70): matches by name, optionally only objects that have a component.
struct PickEntry {
    int id;
    std::string name;
    std::set<std::string> components;
};

inline std::vector<int> pickCandidates(const std::vector<PickEntry>& all, const std::string& query, const std::string& requiredComponent) {
    std::vector<int> out;
    const std::string q = lower(query);
    for (const auto& e : all) {
        if (!requiredComponent.empty() && !e.components.count(requiredComponent)) continue;
        if (q.empty() || lower(e.name).find(q) != std::string::npos) out.push_back(e.id);
    }
    return out;
}

// Inspector from reflection (73) and saved layouts (74).
struct PropertyInfo {
    std::string name, type;
    bool readOnly = false;
};

inline std::string inspectorSchemaText(const std::string& className, const std::vector<PropertyInfo>& props) {
    std::string out = className + "\n";
    for (const auto& p : props) out += "  " + p.name + ": " + p.type + (p.readOnly ? " (read only)" : "") + "\n";
    return out;
}

inline std::string serializeInspectorLayout(const std::vector<std::string>& sectionOrder, const std::set<std::string>& collapsed) {
    std::string out;
    for (const auto& s : sectionOrder) out += (collapsed.count(s) ? "-" : "+") + s + "\n";
    return out;
}

inline void parseInspectorLayout(const std::string& text, std::vector<std::string>& order, std::set<std::string>& collapsed) {
    order.clear();
    collapsed.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() < 2 || (line[0] != '+' && line[0] != '-')) continue;
        order.push_back(line.substr(1));
        if (line[0] == '-') collapsed.insert(line.substr(1));
    }
}

// Long lists (75): only rows in view are drawn.
struct RowRange {
    int first = 0, last = 0; // last is exclusive
};

inline RowRange visibleRows(int total, float rowHeight, float scroll, float viewHeight) {
    if (total <= 0 || rowHeight <= 0) return {};
    const int first = std::clamp(static_cast<int>(scroll / rowHeight), 0, total);
    const int last = std::clamp(static_cast<int>(std::ceil((scroll + viewHeight) / rowHeight)) + 1, first, total);
    return {first, last};
}

// Walkthrough camera (80): move a point but stay out of boxes by sliding along the blocked axis.
struct Box {
    float minX, minZ, maxX, maxZ;
};

inline bool insideAny(const std::vector<Box>& boxes, float x, float z) {
    return std::any_of(boxes.begin(), boxes.end(), [&](const Box& b) { return x > b.minX && x < b.maxX && z > b.minZ && z < b.maxZ; });
}

inline std::pair<float, float> walkMove(const std::vector<Box>& boxes, float x, float z, float dx, float dz) {
    if (!insideAny(boxes, x + dx, z + dz)) return {x + dx, z + dz};
    if (!insideAny(boxes, x + dx, z)) return {x + dx, z};
    if (!insideAny(boxes, x, z + dz)) return {x, z + dz};
    return {x, z};
}

// Selection and shading modes (86, 89).
inline std::string nextSelectionMode(const std::string& m) {
    if (m == "object") return "vertex";
    if (m == "vertex") return "edge";
    if (m == "edge") return "face";
    return "object";
}

inline std::string nextShadeMode(const std::string& m) {
    if (m == "shaded") return "wireframe";
    if (m == "wireframe") return "xray";
    return "shaded";
}

// Screenshots (94): what to hide.
struct CaptureFlags {
    bool gizmos, grid, selectionOutline, panels;
};

inline CaptureFlags cleanCapture() { return {false, false, false, false}; }

// Split viewports (99): rectangles for a layout name.
inline std::vector<Rect> viewportRects(const std::string& layout, float w, float h) {
    if (layout == "two-vertical") return {{0, 0, w / 2, h}, {w / 2, 0, w / 2, h}};
    if (layout == "two-horizontal") return {{0, 0, w, h / 2}, {0, h / 2, w, h / 2}};
    if (layout == "four") return {{0, 0, w / 2, h / 2}, {w / 2, 0, w / 2, h / 2}, {0, h / 2, w / 2, h / 2}, {w / 2, h / 2, w / 2, h / 2}};
    return {{0, 0, w, h}};
}

} // namespace engine::core::uitools

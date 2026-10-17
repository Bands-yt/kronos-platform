#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace engine::core::editor {

// Recent projects (17): newest first, no duplicates, and entries whose folder is gone can be pruned.
class RecentList {
public:
    explicit RecentList(std::size_t cap = 10) : cap_(cap) {}
    void add(const std::string& path) {
        items_.erase(std::remove(items_.begin(), items_.end(), path), items_.end());
        items_.insert(items_.begin(), path);
        if (items_.size() > cap_) items_.resize(cap_);
    }
    int pruneMissing() {
        const std::size_t before = items_.size();
        std::error_code ec;
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const std::string& p) { return !std::filesystem::exists(p, ec); }), items_.end());
        return static_cast<int>(before - items_.size());
    }
    [[nodiscard]] const std::vector<std::string>& items() const { return items_; }

private:
    std::size_t cap_;
    std::vector<std::string> items_;
};

// Project health (18): one plain line and a traffic light.
enum class Health { Good, Attention, Problem };

struct HealthInput {
    int errors = 0, warnings = 0, missingAssets = 0, unusedAssets = 0;
};

inline Health healthOf(const HealthInput& h) { return h.errors > 0 || h.missingAssets > 0 ? Health::Problem : (h.warnings > 0 || h.unusedAssets > 10 ? Health::Attention : Health::Good); }

inline std::string healthSummary(const HealthInput& h) {
    if (healthOf(h) == Health::Good) return "Everything looks fine.";
    std::string s;
    auto add = [&](int n, const char* what) {
        if (n > 0) s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + what;
    };
    add(h.errors, "errors");
    add(h.missingAssets, "missing files");
    add(h.warnings, "warnings");
    add(h.unusedAssets, "unused files");
    return s + ".";
}

// Autosave and crash recovery (19): "scene.autosave-3" files; keep the newest `keep`; offer recovery if newer than the saved file.
inline std::string autosaveName(const std::string& base, int counter) { return base + ".autosave-" + std::to_string(counter); }

struct AutosaveFile {
    std::string name;
    long long modified = 0; // any monotonic number, e.g. file time
};

inline std::vector<std::string> autosavesToDelete(std::vector<AutosaveFile> files, std::size_t keep) {
    std::sort(files.begin(), files.end(), [](const AutosaveFile& a, const AutosaveFile& b) { return a.modified > b.modified; });
    std::vector<std::string> out;
    for (std::size_t i = keep; i < files.size(); ++i) out.push_back(files[i].name);
    return out;
}

inline bool offerRecovery(long long savedModified, const std::vector<AutosaveFile>& autosaves) {
    for (const auto& a : autosaves) {
        if (a.modified > savedModified) return true;
    }
    return false;
}

// Per-project layout file (33).
inline std::string layoutFileFor(const std::string& projectDir) { return projectDir + "/.kronos/layout.ini"; }

// Entities with a component (55) and favourites (56).
inline std::vector<int> filterByComponent(const std::map<int, std::set<std::string>>& componentsOf, const std::string& component) {
    std::vector<int> out;
    for (const auto& [id, comps] : componentsOf) {
        if (comps.count(component)) out.push_back(id);
    }
    return out;
}

class Favorites {
public:
    void toggle(const std::string& stableId) {
        if (!ids_.erase(stableId)) ids_.insert(stableId);
    }
    [[nodiscard]] bool has(const std::string& id) const { return ids_.count(id) > 0; }
    [[nodiscard]] std::string serialize() const {
        std::string s;
        for (const auto& i : ids_) s += i + "\n";
        return s;
    }
    static Favorites parse(const std::string& text) {
        Favorites f;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) f.ids_.insert(line);
        }
        return f;
    }

private:
    std::set<std::string> ids_;
};

// Number scrubbing (63): drag distance to value change; Shift = fine, Ctrl = coarse.
inline double scrubValue(double start, double dragPixels, double perPixel, bool fine, bool coarse) {
    const double scale = fine ? 0.1 : coarse ? 10.0 : 1.0;
    return start + dragPixels * perPixel * scale;
}

// Copy and paste of component values (65): "Component.Property=value" lines.
inline std::string copyValues(const std::string& component, const std::map<std::string, std::string>& values) {
    std::string out;
    for (const auto& [k, v] : values) out += component + "." + k + "=" + v + "\n";
    return out;
}

inline std::map<std::string, std::string> pasteValues(const std::string& text, const std::string& intoComponent) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t dot = line.find('.'), eq = line.find('=');
        if (dot == std::string::npos || eq == std::string::npos || dot > eq) continue;
        if (line.substr(0, dot) != intoComponent) continue;
        out[line.substr(dot + 1, eq - dot - 1)] = line.substr(eq + 1);
    }
    return out;
}

// Lock, hide, solo (68): which objects are drawn given each one's flags and whether anything is soloed.
struct VisibilityFlags {
    bool hidden = false, solo = false;
};

inline std::vector<bool> visibleObjects(const std::vector<VisibilityFlags>& flags) {
    const bool anySolo = std::any_of(flags.begin(), flags.end(), [](const VisibilityFlags& f) { return f.solo; });
    std::vector<bool> out;
    for (const auto& f : flags) out.push_back(anySolo ? f.solo : !f.hidden);
    return out;
}

// Scene tree diff after an import (69): paths added and removed.
struct TreeDiff {
    std::vector<std::string> added, removed;
};

inline TreeDiff diffTrees(const std::set<std::string>& before, const std::set<std::string>& after) {
    TreeDiff d;
    for (const auto& p : after) {
        if (!before.count(p)) d.added.push_back(p);
    }
    for (const auto& p : before) {
        if (!after.count(p)) d.removed.push_back(p);
    }
    return d;
}

// Safe guides (84): title-safe and action-safe rectangles for a frame, as fractions of its size.
struct Rect {
    float x, y, w, h;
};

inline Rect safeGuide(float frameW, float frameH, bool titleSafe) {
    const float f = titleSafe ? 0.9f : 0.93f;
    return {frameW * (1 - f) / 2, frameH * (1 - f) / 2, frameW * f, frameH * f};
}

// Statistics overlay text (95).
inline std::string viewportStatsLine(int fps, double frameMs, long triangles, int drawCalls) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%d fps  %.1f ms  %ldk tris  %d draws", fps, frameMs, triangles / 1000, drawCalls);
    return buf;
}

// Interaction recording (100): a list of input events that can be saved and replayed in a test.
struct RecordedEvent {
    double time = 0;
    std::string kind; // "click", "key", "drag"
    float x = 0, y = 0;
    std::string key;
};

inline std::string serializeRecording(const std::vector<RecordedEvent>& events) {
    std::ostringstream out;
    for (const auto& e : events) out << e.time << ' ' << e.kind << ' ' << e.x << ' ' << e.y << ' ' << (e.key.empty() ? "-" : e.key) << '\n';
    return out.str();
}

inline std::vector<RecordedEvent> parseRecording(const std::string& text) {
    std::vector<RecordedEvent> out;
    std::istringstream in(text);
    RecordedEvent e;
    while (in >> e.time >> e.kind >> e.x >> e.y >> e.key) {
        if (e.key == "-") e.key.clear();
        out.push_back(e);
        e = {};
    }
    return out;
}

} // namespace engine::core::editor

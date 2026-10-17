#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::misc {

// Dependency repair (4): what to install or repair, given what is present and what is damaged.
struct RepairStep {
    std::string file, action; // "install" or "repair"
};

inline std::vector<RepairStep> repairPlan(const std::vector<std::string>& required, const std::set<std::string>& present, const std::set<std::string>& damaged) {
    std::vector<RepairStep> out;
    for (const auto& f : required) {
        if (!present.count(f)) out.push_back({f, "install"});
        else if (damaged.count(f)) out.push_back({f, "repair"});
    }
    return out;
}

// Guided tutorial (20, 751): steps complete in order; the current step is the first unfinished one.
class Tutorial {
public:
    explicit Tutorial(std::vector<std::string> steps) : steps_(std::move(steps)) {}
    bool complete(const std::string& step) {
        if (current() != step) return false;
        ++done_;
        return true;
    }
    [[nodiscard]] std::string current() const { return done_ < steps_.size() ? steps_[done_] : std::string(); }
    [[nodiscard]] int percent() const { return steps_.empty() ? 100 : static_cast<int>(done_ * 100 / steps_.size()); }

private:
    std::vector<std::string> steps_;
    std::size_t done_ = 0;
};

inline std::vector<std::string> firstGameSteps() { return {"Insert a part", "Move it", "Add a script", "Press Play", "Save your game"}; }

// String catalogue with fallback (22): a missing translation falls back to English, then to the key.
class StringCatalog {
public:
    void add(const std::string& language, const std::string& key, const std::string& text) { table_[language][key] = text; }
    [[nodiscard]] std::string get(const std::string& language, const std::string& key) const {
        for (const std::string& lang : {language, std::string("en")}) {
            auto l = table_.find(lang);
            if (l == table_.end()) continue;
            auto k = l->second.find(key);
            if (k != l->second.end()) return k->second;
        }
        return key;
    }
    [[nodiscard]] std::vector<std::string> missingKeys(const std::string& language) const {
        std::vector<std::string> out;
        auto en = table_.find("en");
        if (en == table_.end()) return out;
        auto l = table_.find(language);
        for (const auto& kv : en->second) {
            if (l == table_.end() || !l->second.count(kv.first)) out.push_back(kv.first);
        }
        return out;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> table_;
};

// Window placement (29): put a saved window back on a monitor that still exists.
struct WinRect {
    int x, y, w, h;
};

inline WinRect restoreWindow(WinRect saved, const std::vector<WinRect>& monitors) {
    if (monitors.empty()) return saved;
    for (const auto& m : monitors) {
        if (saved.x >= m.x && saved.y >= m.y && saved.x + saved.w <= m.x + m.w && saved.y + saved.h <= m.y + m.h) return saved;
    }
    const WinRect& m = monitors.front();
    const int w = std::min(saved.w, m.w), h = std::min(saved.h, m.h);
    return {m.x + (m.w - w) / 2, m.y + (m.h - h) / 2, w, h};
}

// Layout migration (44): old layout keys are renamed when the file version is older.
inline std::map<std::string, std::string> migrateLayout(std::map<std::string, std::string> layout, int fileVersion, int currentVersion) {
    if (fileVersion < 2 && currentVersion >= 2) {
        auto it = layout.find("panel.props");
        if (it != layout.end()) {
            layout["panel.inspector"] = it->second;
            layout.erase(it);
        }
    }
    layout["version"] = std::to_string(currentVersion);
    return layout;
}

// Mobile counters (919): frame time, draw calls, battery drain per minute.
struct MobileCounters {
    float frameMs = 0;
    int drawCalls = 0;
    float batteryPercentStart = 100, batteryPercentNow = 100;
    float minutes = 0;
};

inline float batteryDrainPerHour(const MobileCounters& c) { return c.minutes > 0 ? (c.batteryPercentStart - c.batteryPercentNow) / c.minutes * 60.0f : 0.0f; }

} // namespace engine::core::misc

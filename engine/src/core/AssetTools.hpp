#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace engine::core {

struct SheetCell {
    int index = 0;
    float x = 0, y = 0, w = 0, h = 0;
};

// Lays out `count` thumbnails on pages of columns x rows, each page `pageW` x `pageH`.
// Returns one cell per thumbnail (page-relative); `pages` receives the page count.
inline std::vector<SheetCell> contactSheetLayout(int count, int columns, int rows, float pageW, float pageH, float gap,
                                                 int& pages) {
    pages = 0;
    std::vector<SheetCell> out;
    if (count <= 0 || columns <= 0 || rows <= 0 || pageW <= 0 || pageH <= 0) return out;
    const float cellW = (pageW - gap * static_cast<float>(columns + 1)) / static_cast<float>(columns);
    const float cellH = (pageH - gap * static_cast<float>(rows + 1)) / static_cast<float>(rows);
    if (cellW <= 0 || cellH <= 0) return out;
    const int perPage = columns * rows;
    pages = (count + perPage - 1) / perPage;
    for (int i = 0; i < count; ++i) {
        const int slot = i % perPage;
        out.push_back({i, gap + static_cast<float>(slot % columns) * (cellW + gap),
                       gap + static_cast<float>(slot / columns) * (cellH + gap), cellW, cellH});
    }
    return out;
}

// Tags are lower-case words (letters, digits, - and _), optionally "key:value". Returns "" if invalid.
inline std::string normalizeTag(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (c == ' ') {
            if (!out.empty() && out.back() != '-') out += '-';
            continue;
        }
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == ':') out += static_cast<char>(std::tolower(u));
        else return {};
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty() || out.size() > 40 || out.front() == ':' || out.back() == ':') return {};
    return out;
}

class AssetTagSet {
public:
    bool add(const std::string& raw) {
        const std::string tag = normalizeTag(raw);
        return !tag.empty() && tags_.insert(tag).second;
    }
    bool remove(const std::string& raw) { return tags_.erase(normalizeTag(raw)) > 0; }
    [[nodiscard]] bool has(const std::string& raw) const { return tags_.count(normalizeTag(raw)) > 0; }
    [[nodiscard]] std::vector<std::string> list() const { return {tags_.begin(), tags_.end()}; }
    // Value of "key:value" tags, e.g. value("biome") -> "desert".
    [[nodiscard]] std::string value(const std::string& key) const {
        const std::string prefix = key + ":";
        for (const auto& t : tags_) {
            if (t.rfind(prefix, 0) == 0) return t.substr(prefix.size());
        }
        return {};
    }

private:
    std::set<std::string> tags_;
};

// Tracks GPU memory against a budget; warns at 80% and says when over.
class GpuMemoryBudget {
public:
    enum class State { Ok, Warning, OverBudget };
    explicit GpuMemoryBudget(uint64_t budgetBytes) : budget_(budgetBytes) {}
    void allocate(const std::string& category, uint64_t bytes) { used_[category] += bytes; }
    void release(const std::string& category, uint64_t bytes) {
        auto it = used_.find(category);
        if (it == used_.end()) return;
        it->second = bytes >= it->second ? 0 : it->second - bytes;
    }
    [[nodiscard]] uint64_t total() const {
        uint64_t sum = 0;
        for (const auto& kv : used_) sum += kv.second;
        return sum;
    }
    [[nodiscard]] State state() const {
        if (budget_ == 0) return State::Ok;
        if (total() > budget_) return State::OverBudget;
        return total() * 10 >= budget_ * 8 ? State::Warning : State::Ok;
    }
    // Biggest category first, so the report says what to shrink.
    [[nodiscard]] std::vector<std::pair<std::string, uint64_t>> byCategory() const {
        std::vector<std::pair<std::string, uint64_t>> out(used_.begin(), used_.end());
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        return out;
    }

private:
    uint64_t budget_;
    std::map<std::string, uint64_t> used_;
};

// Most-recently-used servers for "Rejoin last server" (newest first, no duplicates, capped).
class RecentServers {
public:
    explicit RecentServers(std::size_t cap = 5) : cap_(cap) {}
    void add(const std::string& address) {
        if (address.empty()) return;
        items_.erase(std::remove(items_.begin(), items_.end(), address), items_.end());
        items_.insert(items_.begin(), address);
        if (items_.size() > cap_) items_.resize(cap_);
    }
    [[nodiscard]] const std::vector<std::string>& list() const { return items_; }
    [[nodiscard]] std::string last() const { return items_.empty() ? std::string{} : items_.front(); }
    [[nodiscard]] std::string serialize() const {
        std::string out;
        for (const auto& i : items_) out += i + "\n";
        return out;
    }
    static RecentServers parse(const std::string& text, std::size_t cap = 5) {
        RecentServers r(cap);
        std::vector<std::string> lines;
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            if (end > pos) lines.push_back(text.substr(pos, end - pos));
            pos = end + 1;
        }
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) r.add(*it); // oldest first, so order is kept
        return r;
    }

private:
    std::size_t cap_;
    std::vector<std::string> items_;
};

// Which draw batches a scene breaks into: objects sharing a material draw together.
// Returns advice lines for materials used by only one tiny object while many others share a base.
inline std::vector<std::string> batchingHints(const std::map<std::string, int>& objectsPerMaterial) {
    std::vector<std::string> hints;
    int single = 0;
    for (const auto& kv : objectsPerMaterial) {
        if (kv.second == 1) ++single;
    }
    const int materials = static_cast<int>(objectsPerMaterial.size());
    if (materials >= 20 && single * 2 >= materials) {
        hints.push_back(std::to_string(single) + " of " + std::to_string(materials) +
                        " materials are used by one object. Sharing materials lets the renderer draw them together.");
    }
    for (const auto& kv : objectsPerMaterial) {
        if (kv.second >= 200) {
            hints.push_back("'" + kv.first + "' is used by " + std::to_string(kv.second) +
                            " objects; that is good for batching.");
        }
    }
    return hints;
}

} // namespace engine::core

namespace engine::core {

struct AssetProvenance {
    std::string asset;
    std::string source;  // "made here", a URL, or "Roblox place import"
    std::string license; // "" when unknown
};

// Markdown report of where assets came from; unknown licences are listed first so they get fixed.
inline std::string provenanceReport(std::vector<AssetProvenance> assets) {
    std::stable_sort(assets.begin(), assets.end(), [](const AssetProvenance& a, const AssetProvenance& b) {
        return a.license.empty() && !b.license.empty();
    });
    int unknown = 0;
    for (const auto& a : assets) unknown += a.license.empty() ? 1 : 0;
    std::string out = "# Asset provenance\n\n" + std::to_string(assets.size()) + " assets, " + std::to_string(unknown) +
                      " with no licence recorded.\n\n| Asset | Source | Licence |\n|---|---|---|\n";
    for (const auto& a : assets) {
        out += "| " + a.asset + " | " + (a.source.empty() ? "unknown" : a.source) + " | " +
               (a.license.empty() ? "**unknown**" : a.license) + " |\n";
    }
    return out;
}

enum class SaveState { Saved, Unsaved, Saving, Failed };

// Short text for the status bar. `secondsAgo` is how long since the last good save (-1 = never).
inline std::string saveStatusText(SaveState state, int secondsAgo) {
    switch (state) {
    case SaveState::Saving: return "Saving...";
    case SaveState::Failed: return "Save failed. Your work is not saved; try Save As.";
    case SaveState::Unsaved:
        return secondsAgo < 0 ? "Not saved yet" : "Unsaved changes (last saved " + std::to_string(secondsAgo / 60) + " min ago)";
    case SaveState::Saved: break;
    }
    if (secondsAgo < 0) return "Saved";
    if (secondsAgo < 60) return "Saved just now";
    return "Saved " + std::to_string(secondsAgo / 60) + " min ago";
}

} // namespace engine::core

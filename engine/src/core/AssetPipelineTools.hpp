#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace engine::core::assets {

inline uint64_t contentHash(const std::vector<uint8_t>& bytes) { // FNV-1a 64
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

inline std::string hex64(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[i] = d[v & 0xF];
    return s;
}

inline bool verifyHash(const std::vector<uint8_t>& bytes, const std::string& expectedHex) {
    return hex64(contentHash(bytes)) == expectedHex;
}

// Groups of asset names whose bytes are identical (only groups of 2+).
inline std::vector<std::vector<std::string>> findDuplicates(const std::map<std::string, std::vector<uint8_t>>& files) {
    std::map<uint64_t, std::vector<std::string>> byHash;
    for (const auto& [name, bytes] : files) byHash[contentHash(bytes)].push_back(name);
    std::vector<std::vector<std::string>> out;
    for (auto& kv : byHash) {
        if (kv.second.size() > 1) out.push_back(kv.second);
    }
    return out;
}

// A stable id: the same path and source always give the same id, so reimporting keeps references.
inline std::string stableAssetId(const std::string& path, const std::string& source = {}) {
    const std::string text = source + "|" + path;
    uint64_t a = contentHash({text.begin(), text.end()});
    std::string reversed(text.rbegin(), text.rend());
    uint64_t b = contentHash({reversed.begin(), reversed.end()});
    const std::string h = hex64(a) + hex64(b);
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20, 12);
}

// What is unused, and what is referenced but missing.
struct ScanResult {
    std::vector<std::string> orphans;
    std::vector<std::string> missing;
};

inline ScanResult scanAssets(const std::set<std::string>& existing, const std::set<std::string>& referenced) {
    ScanResult r;
    for (const auto& a : existing) {
        if (!referenced.count(a)) r.orphans.push_back(a);
    }
    for (const auto& a : referenced) {
        if (!existing.count(a)) r.missing.push_back(a);
    }
    return r;
}

// Asset dependency graph: asset -> assets it uses.
using DepMap = std::map<std::string, std::set<std::string>>;

inline std::set<std::string> transitiveDeps(const DepMap& deps, const std::string& root) {
    std::set<std::string> seen;
    std::vector<std::string> todo{root};
    while (!todo.empty()) {
        const std::string cur = todo.back();
        todo.pop_back();
        auto it = deps.find(cur);
        if (it == deps.end()) continue;
        for (const auto& d : it->second) {
            if (seen.insert(d).second) todo.push_back(d);
        }
    }
    return seen;
}

inline std::set<std::string> dependents(const DepMap& deps, const std::string& asset) {
    std::set<std::string> out;
    for (const auto& [name, used] : deps) {
        if (used.count(asset)) out.insert(name);
    }
    return out;
}

// scene -> list of assets used (with repeats); counts uses of each asset across scenes.
inline std::map<std::string, int> usageCounts(const std::map<std::string, std::vector<std::string>>& scenes) {
    std::map<std::string, int> counts;
    for (const auto& kv : scenes) {
        for (const auto& a : kv.second) ++counts[a];
    }
    return counts;
}

// Replaces one asset name by another in every reference list; returns how many references changed.
inline int replaceAsset(std::map<std::string, std::vector<std::string>>& scenes, const std::string& from, const std::string& to) {
    int n = 0;
    for (auto& kv : scenes) {
        for (auto& a : kv.second) {
            if (a == from) {
                a = to;
                ++n;
            }
        }
    }
    return n;
}

inline std::string baseName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    return name;
}

inline int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// Best candidate for a missing asset: same base name first, else the closest name within `maxDistance`.
inline std::string suggestRelink(const std::string& missing, const std::set<std::string>& candidates, int maxDistance = 3) {
    const std::string want = baseName(missing);
    std::string best;
    int bestDist = maxDistance + 1;
    for (const auto& c : candidates) {
        const int d = editDistance(want, baseName(c));
        if (d < bestDist) {
            bestDist = d;
            best = c;
        }
    }
    return best;
}

// Missing references with a suggested replacement (empty when nothing is close).
inline std::vector<std::pair<std::string, std::string>> repairSuggestions(const std::set<std::string>& existing,
                                                                          const std::set<std::string>& referenced) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& m : scanAssets(existing, referenced).missing) out.push_back({m, suggestRelink(m, existing)});
    return out;
}

// --- Naming (58, 66) -----------------------------------------------------------------------
// Pattern tokens: {name} the old name, {n} a counter (start 1), {nn} the counter with two digits.
inline std::vector<std::string> bulkRename(const std::vector<std::string>& names, const std::string& pattern, int start = 1) {
    auto replaceAll = [](std::string s, const std::string& from, const std::string& to) {
        for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) s.replace(pos, from.size(), to);
        return s;
    };
    std::vector<std::string> out;
    int n = start;
    for (const auto& name : names) {
        std::string s = replaceAll(pattern, "{name}", name);
        const std::string two = (n < 10 ? "0" : "") + std::to_string(n);
        s = replaceAll(s, "{nn}", two);
        s = replaceAll(s, "{n}", std::to_string(n));
        out.push_back(s);
        ++n;
    }
    return out;
}

inline std::string entityPath(const std::vector<std::string>& ancestorsOutermostFirst) {
    std::string out;
    for (const auto& n : ancestorsOutermostFirst) out += (out.empty() ? "" : "/") + n;
    return out;
}

// --- Texture channel packing (172) ---------------------------------------------------------
struct PackPreset {
    const char* name;
    const char* r;
    const char* g;
    const char* b;
    const char* a;
};

inline const std::vector<PackPreset>& packPresets() {
    static const std::vector<PackPreset> list = {
        {"ORM", "occlusion", "roughness", "metallic", "-"},
        {"Roughness-Metallic", "-", "roughness", "metallic", "-"},
        {"Albedo+Alpha", "albedo.r", "albedo.g", "albedo.b", "opacity"},
    };
    return list;
}

// Packs up to four equal-length 8-bit channels into RGBA (a missing channel is filled with `fill`).
inline std::vector<uint8_t> packChannels(const std::array<const std::vector<uint8_t>*, 4>& ch, std::size_t pixels, uint8_t fill = 255) {
    std::vector<uint8_t> out(pixels * 4, fill);
    for (std::size_t c = 0; c < 4; ++c) {
        if (!ch[c] || ch[c]->size() < pixels) continue;
        for (std::size_t i = 0; i < pixels; ++i) out[i * 4 + c] = (*ch[c])[i];
    }
    return out;
}

// --- Units and axes (173) ----------------------------------------------------------------------
enum class UpAxis { Y, Z };

struct Vec3f {
    float x, y, z;
};

inline Vec3f convertPoint(Vec3f p, UpAxis from, UpAxis to, float unitScale) {
    if (from != to) {
        // Z-up (x right, y forward, z up) <-> Y-up (x right, y up, z back)
        p = from == UpAxis::Z ? Vec3f{p.x, p.z, -p.y} : Vec3f{p.x, -p.z, p.y};
    }
    return {p.x * unitScale, p.y * unitScale, p.z * unitScale};
}

} // namespace engine::core::assets

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/AssetPipelineTools.hpp"

namespace engine::core::content {

// Reimport (156): the source changed if its hash differs from the one saved at the last import.
inline bool needsReimport(const std::string& savedHashHex, const std::vector<uint8_t>& currentBytes) {
    return savedHashHex != assets::hex64(assets::contentHash(currentBytes));
}

// Import conflicts (160).
enum class ConflictChoice { Skip, Replace, KeepBoth };

inline std::string resolveConflict(const std::string& name, const std::set<std::string>& existing, ConflictChoice choice) {
    if (!existing.count(name)) return name;
    if (choice == ConflictChoice::Skip) return {};
    if (choice == ConflictChoice::Replace) return name;
    const std::size_t dot = name.find_last_of('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot), ext = dot == std::string::npos ? "" : name.substr(dot);
    for (int n = 2;; ++n) {
        const std::string candidate = stem + " (" + std::to_string(n) + ")" + ext;
        if (!existing.count(candidate)) return candidate;
    }
}

// Asset pack manifest (196): files with sizes and hashes as JSON text.
struct PackFile {
    std::string path;
    std::vector<uint8_t> bytes;
};

inline std::string packManifest(const std::string& name, const std::string& version, const std::vector<PackFile>& files) {
    std::ostringstream out;
    out << "{\"name\":\"" << name << "\",\"version\":\"" << version << "\",\"files\":[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        out << (i ? "," : "") << "{\"path\":\"" << files[i].path << "\",\"size\":" << files[i].bytes.size() << ",\"hash\":\"" << assets::hex64(assets::contentHash(files[i].bytes)) << "\"}";
    }
    out << "]}";
    return out.str();
}

// Aliases (197): "@hero" -> "characters/hero_v3.glb". Unknown names pass through unchanged.
inline std::string resolveAlias(const std::map<std::string, std::string>& aliases, const std::string& name, int maxHops = 8) {
    std::string cur = name;
    for (int i = 0; i < maxHops; ++i) {
        auto it = aliases.find(cur);
        if (it == aliases.end()) return cur;
        cur = it->second;
    }
    return cur;
}

// Material instances (205, 223): a base material's parameters with per-instance overrides, saved as text.
using Params = std::map<std::string, float>;

inline Params resolveMaterial(const Params& base, const Params& overrides) {
    Params out = base;
    for (const auto& [k, v] : overrides) {
        if (base.count(k)) out[k] = v; // overrides for unknown parameters are ignored
    }
    return out;
}

inline std::string serializeMaterial(const std::string& name, const Params& p) {
    std::ostringstream out;
    out << "material " << name << "\n";
    for (const auto& [k, v] : p) out << k << "=" << v << "\n";
    return out.str();
}

inline bool parseMaterial(const std::string& text, std::string& name, Params& p) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("material ", 0) != 0) return false;
    name = line.substr(9);
    p.clear();
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) p[line.substr(0, eq)] = static_cast<float>(std::atof(line.c_str() + eq + 1));
    }
    return true;
}

// Shader includes (212): replaces `#include "name"` with the library text, each file once; errors name the file.
inline bool expandIncludes(const std::string& source, const std::map<std::string, std::string>& library, std::string& out, std::string& error,
                           std::set<std::string>* seen = nullptr, int depth = 0) {
    std::set<std::string> local;
    if (!seen) seen = &local;
    if (depth > 8) {
        error = "includes are nested too deeply";
        return false;
    }
    std::istringstream in(source);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t p = line.find("#include");
        if (p == std::string::npos) {
            out += line + "\n";
            continue;
        }
        const std::size_t a = line.find('"', p), b = line.find('"', a + 1);
        if (a == std::string::npos || b == std::string::npos) {
            error = "bad #include line: " + line;
            return false;
        }
        const std::string name = line.substr(a + 1, b - a - 1);
        auto it = library.find(name);
        if (it == library.end()) {
            error = "unknown include \"" + name + "\"";
            return false;
        }
        if (seen->insert(name).second && !expandIncludes(it->second, library, out, error, seen, depth + 1)) return false;
    }
    return true;
}

// Platform shader variants (215) and permutation budget (240).
struct ShaderOption {
    std::string name;
    int values = 2;
};

inline long permutationCount(const std::vector<ShaderOption>& options) {
    long n = 1;
    for (const auto& o : options) n *= std::max(o.values, 1);
    return n;
}

inline bool withinPermutationBudget(const std::vector<ShaderOption>& options, long budget) { return permutationCount(options) <= budget; }

inline std::vector<std::string> variantDefines(const std::string& platform) {
    if (platform == "mobile") return {"LOW_PRECISION", "NO_SOFT_PARTICLES", "SIMPLE_SHADOWS"};
    if (platform == "integrated") return {"SIMPLE_SHADOWS"};
    return {};
}

// Documentation from shader nodes (224).
struct NodeDoc {
    std::string name, description;
    std::vector<std::pair<std::string, std::string>> inputs; // name, type
    std::string output;
};

inline std::string shaderNodeDocs(const std::vector<NodeDoc>& nodes) {
    std::string out = "# Shader nodes\n\n";
    for (const auto& n : nodes) {
        out += "## " + n.name + "\n\n" + n.description + "\n\n| Input | Type |\n|---|---|\n";
        for (const auto& i : n.inputs) out += "| " + i.first + " | " + i.second + " |\n";
        out += "\nOutput: " + n.output + "\n\n";
    }
    return out;
}

// Frustum culling on the CPU (230): sphere against six planes (normal xyz, distance w), normals pointing inside.
struct Plane {
    float nx, ny, nz, d;
};

inline bool sphereInFrustum(const std::array<Plane, 6>& planes, float cx, float cy, float cz, float radius) {
    for (const Plane& p : planes) {
        if (p.nx * cx + p.ny * cy + p.nz * cz + p.d < -radius) return false;
    }
    return true;
}

// Cache quota (547, 923): which cached downloads to delete to get under a byte quota (oldest use first).
struct CachedItem {
    std::string name;
    uint64_t bytes = 0;
    long long lastUsed = 0;
};

inline std::vector<std::string> evictForQuota(std::vector<CachedItem> items, uint64_t quota) {
    std::sort(items.begin(), items.end(), [](const CachedItem& a, const CachedItem& b) { return a.lastUsed < b.lastUsed; });
    uint64_t total = 0;
    for (const auto& i : items) total += i.bytes;
    std::vector<std::string> out;
    for (const auto& i : items) {
        if (total <= quota) break;
        total -= i.bytes;
        out.push_back(i.name);
    }
    return out;
}

// Plugins: load order from dependencies (709), permissions in words (723), config (710), update with rollback (722).
inline bool pluginLoadOrder(const std::map<std::string, std::vector<std::string>>& deps, std::vector<std::string>& order, std::string& error) {
    order.clear();
    std::map<std::string, int> state;
    bool ok = true;
    auto visit = [&](auto&& self, const std::string& id) -> void {
        if (!ok || state[id] == 2) return;
        if (state[id] == 1) {
            ok = false;
            error = "plugins depend on each other in a circle at " + id;
            return;
        }
        state[id] = 1;
        auto it = deps.find(id);
        if (it != deps.end()) {
            for (const auto& d : it->second) {
                if (!deps.count(d)) {
                    ok = false;
                    error = id + " needs " + d + ", which is not installed";
                    return;
                }
                self(self, d);
            }
        }
        state[id] = 2;
        order.push_back(id);
    };
    for (const auto& kv : deps) visit(visit, kv.first);
    return ok;
}

inline std::vector<std::string> describeCapabilities(uint64_t bits) {
    static const std::vector<std::pair<uint64_t, const char*>> names = {
        {1u << 0, "Read the scene"}, {1u << 1, "Change the scene"}, {1u << 2, "Add import formats"}, {1u << 3, "Add editor panels"}, {1u << 4, "Share data channels"}};
    std::vector<std::string> out;
    for (const auto& [bit, text] : names) {
        if (bits & bit) out.push_back(text);
    }
    return out;
}

inline std::string pluginConfigPath(const std::string& configDir, const std::string& pluginId) {
    std::string safe;
    for (char c : pluginId) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-') ? c : '_';
    return configDir + "/" + safe + ".cfg";
}

// Installs `newFile` over `target`, keeping the old one as target + ".previous". Returns false (and restores) on failure.
inline bool installWithRollback(const std::filesystem::path& newFile, const std::filesystem::path& target) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path previous = target.string() + ".previous";
    if (fs::exists(target, ec)) {
        fs::remove(previous, ec);
        fs::rename(target, previous, ec);
        if (ec) return false;
    }
    fs::copy_file(newFile, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (fs::exists(previous)) fs::rename(previous, target, ec);
        return false;
    }
    return true;
}

inline bool rollbackInstall(const std::filesystem::path& target) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path previous = target.string() + ".previous";
    if (!fs::exists(previous, ec)) return false;
    fs::remove(target, ec);
    fs::rename(previous, target, ec);
    return !ec;
}

// Player settings: rebinding with conflict detection (533), per-game audio (534), discovery filter (538), parental (540), ping (543), friends (544).
struct RebindResult {
    bool ok = false;
    std::string conflictsWith;
};

inline RebindResult rebind(std::map<std::string, std::string>& actionToKey, const std::string& action, const std::string& key) {
    for (const auto& [a, k] : actionToKey) {
        if (a != action && k == key) return {false, a};
    }
    actionToKey[action] = key;
    return {true, {}};
}

inline std::string serializeGameAudio(const std::map<std::string, std::map<std::string, float>>& perGame) {
    std::ostringstream out;
    for (const auto& [game, buses] : perGame)
        for (const auto& [bus, vol] : buses) out << game << ' ' << bus << ' ' << vol << '\n';
    return out.str();
}

inline std::map<std::string, std::map<std::string, float>> parseGameAudio(const std::string& text) {
    std::map<std::string, std::map<std::string, float>> out;
    std::istringstream in(text);
    std::string game, bus;
    float v;
    while (in >> game >> bus >> v) out[game][bus] = std::clamp(v, 0.0f, 1.0f);
    return out;
}

struct GameListing {
    std::string name;
    std::set<std::string> tags;
};

inline std::vector<std::string> gamesWithTags(const std::vector<GameListing>& games, const std::set<std::string>& wanted) {
    std::vector<std::string> out;
    for (const auto& g : games) {
        if (std::all_of(wanted.begin(), wanted.end(), [&](const std::string& t) { return g.tags.count(t) > 0; })) out.push_back(g.name);
    }
    return out;
}

struct ParentalSettings {
    bool chatEnabled = true, friendRequests = true, joinFriendsOnly = false;
};

inline ParentalSettings youngPlayerDefaults() { return {false, false, true}; }

inline std::string pingQuality(int ms) { return ms < 0 ? "unknown" : ms < 60 ? "great" : ms < 120 ? "good" : ms < 250 ? "laggy" : "bad"; }

inline bool canJoinFriend(const ParentalSettings& mine, bool isFriend, bool friendAllowsJoins) { return (isFriend || !mine.joinFriendsOnly) && (!isFriend || friendAllowsJoins); }

} // namespace engine::core::content

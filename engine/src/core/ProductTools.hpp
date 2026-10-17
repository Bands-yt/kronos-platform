#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::product {

// Service shims (505).
inline std::string shimStatus(const std::string& service) {
    static const std::map<std::string, std::string> s = {{"Players", "works"}, {"TweenService", "works"}, {"DataStoreService", "local only"}, {"HttpService", "no web requests"}, {"MarketplaceService", "stub"}, {"PathfindingService", "missing"}};
    auto it = s.find(service);
    return it == s.end() ? "unknown" : it->second;
}

// GUI sizes (506): UDim = scale of the parent plus an offset in pixels.
inline float resolveUDim(float scale, float offset, float parentSize) { return scale * parentSize + offset; }

// Asset id map (508) and report (511).
inline std::string mapAsset(const std::map<std::string, std::string>& idToFile, const std::string& rbxassetid) {
    const std::size_t p = rbxassetid.find_last_not_of("0123456789");
    const std::string digits = p == std::string::npos ? rbxassetid : rbxassetid.substr(p + 1);
    auto it = idToFile.find(digits);
    return it == idToFile.end() ? std::string() : it->second;
}

inline std::string mappingReport(const std::string& kind, const std::vector<std::string>& ids, const std::map<std::string, std::string>& idToFile) {
    int mapped = 0;
    std::vector<std::string> missing;
    for (const auto& id : ids) {
        if (!mapAsset(idToFile, id).empty()) ++mapped;
        else missing.push_back(id);
    }
    std::string out = kind + ": " + std::to_string(mapped) + " of " + std::to_string(ids.size()) + " found";
    for (const auto& m : missing) out += "\n  missing " + m;
    return out;
}

// Lighting mapping (512): Roblox Lighting properties to Kronos environment values.
struct KronosLighting {
    float exposure, sunElevation, fogDensity;
};

inline KronosLighting mapLighting(float brightness, float clockTimeHours, float fogEnd) {
    const float sun = 90.0f * std::sin((clockTimeHours - 6.0f) / 12.0f * 3.14159265f);
    return {std::clamp(brightness / 2.0f, 0.1f, 4.0f), sun, fogEnd > 0 && fogEnd < 100000 ? 3.0f / fogEnd : 0.0f};
}

// Tags (514).
inline std::string serializeTags(const std::set<std::string>& tags) {
    std::string s;
    for (const auto& t : tags) s += (s.empty() ? "" : ",") + t;
    return s;
}

inline std::set<std::string> parseTags(const std::string& text) {
    std::set<std::string> out;
    std::istringstream in(text);
    std::string t;
    while (std::getline(in, t, ',')) {
        if (!t.empty()) out.insert(t);
    }
    return out;
}

// Remote ordering (515): events on one remote arrive in the order they were sent.
inline bool arrivedInOrder(const std::vector<int>& sentSeq, const std::vector<int>& receivedSeq) {
    int last = -1;
    std::size_t j = 0;
    for (int r : receivedSeq) {
        if (r < last) return false;
        last = r;
        while (j < sentSeq.size() && sentSeq[j] != r) ++j;
        if (j == sentSeq.size()) return false;
    }
    return true;
}

// DataStore emulator (516): values with versions; UpdateAsync applies a function.
class DataStoreEmulator {
public:
    int set(const std::string& key, const std::string& value) {
        auto& e = data_[key];
        e.value = value;
        return ++e.version;
    }
    bool get(const std::string& key, std::string& value) const {
        auto it = data_.find(key);
        if (it == data_.end()) return false;
        value = it->second.value;
        return true;
    }
    int update(const std::string& key, const std::function<std::string(const std::string&)>& fn) {
        auto& e = data_[key];
        e.value = fn(e.value);
        return ++e.version;
    }
    [[nodiscard]] int version(const std::string& key) const {
        auto it = data_.find(key);
        return it == data_.end() ? 0 : it->second.version;
    }
    void remove(const std::string& key) { data_.erase(key); }

private:
    struct Entry {
        std::string value;
        int version = 0;
    };
    std::map<std::string, Entry> data_;
};

// Unsupported API report (517): which known-missing names appear in a script, with line numbers.
inline std::vector<std::pair<int, std::string>> unsupportedApiUses(const std::string& source, const std::set<std::string>& unsupported) {
    std::vector<std::pair<int, std::string>> out;
    std::istringstream in(source);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        for (const auto& api : unsupported) {
            if (line.find(api) != std::string::npos) out.push_back({n, api});
        }
    }
    return out;
}

// Command line parsing (520, 570): --key value pairs and bare flags.
struct CliArgs {
    std::map<std::string, std::string> options;
    std::vector<std::string> positional;
};

inline CliArgs parseArgs(const std::vector<std::string>& args) {
    CliArgs a;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i].rfind("--", 0) == 0) {
            const std::string key = args[i].substr(2);
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) a.options[key] = args[++i];
            else a.options[key] = "true";
        } else {
            a.positional.push_back(args[i]);
        }
    }
    return a;
}

// Roblox file version (522): "version 0.5" header of rbxlx or "<roblox!" magic of binary.
inline std::string robloxFileKind(const std::string& head) {
    if (head.rfind("<roblox!", 0) == 0) return "binary";
    if (head.find("<roblox ") != std::string::npos) return "xml";
    return "unknown";
}

// Compatibility export (525).
inline std::string compatMarkdown(const std::vector<std::pair<std::string, int>>& scores) {
    std::string out = "| Place | Score |\n|---|---|\n";
    int sum = 0;
    for (const auto& [n, s] : scores) {
        out += "| " + n + " | " + std::to_string(s) + "% |\n";
        sum += s;
    }
    if (!scores.empty()) out += "| **Average** | " + std::to_string(sum / static_cast<int>(scores.size())) + "% |\n";
    return out;
}

// Avatar (536, 537).
inline std::string serializeLoadout(const std::map<std::string, std::string>& slots) {
    std::string s;
    for (const auto& [k, v] : slots) s += k + "=" + v + "\n";
    return s;
}

inline std::map<std::string, std::string> parseLoadout(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) out[line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

inline int avatarLod(float distance, bool performanceMode) {
    const float scale = performanceMode ? 0.5f : 1.0f;
    return distance < 15 * scale ? 0 : distance < 40 * scale ? 1 : 2;
}

// Offline mode (541), crash-safe exit (545).
inline std::vector<std::string> featuresAvailable(bool online) {
    std::vector<std::string> f = {"Play local games", "Studio", "Customize avatar", "Settings"};
    if (online) f.insert(f.end(), {"Friends", "Discover", "Publish"});
    return f;
}

inline std::string exitAction(int exitCode, bool wasInGame) {
    if (exitCode == 0) return wasInGame ? "return to home" : "close";
    return wasInGame ? "return to home and show a crash notice" : "show a crash notice";
}

// Versions and update channels (548, 559).
struct Version {
    int major = 0, minor = 0, patch = 0;
    std::string suffix;
    bool operator<(const Version& o) const { return std::tie(major, minor, patch) < std::tie(o.major, o.minor, o.patch); }
};

inline Version parseVersion(const std::string& text) {
    Version v;
    std::string s = text;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    const std::size_t dash = s.find('-');
    if (dash != std::string::npos) {
        v.suffix = s.substr(dash + 1);
        s = s.substr(0, dash);
    }
    std::sscanf(s.c_str(), "%d.%d.%d", &v.major, &v.minor, &v.patch);
    return v;
}

inline std::string pickRelease(const std::string& channel, const std::vector<std::string>& releases) {
    std::string best;
    Version bestV;
    for (const auto& r : releases) {
        const Version v = parseVersion(r);
        const bool nightly = v.suffix.find("nightly") != std::string::npos;
        const bool pre = !v.suffix.empty() && !nightly;
        const bool ok = channel == "nightly" || (channel == "preview" && !nightly) || (channel == "stable" && !pre && !nightly);
        if (!ok) continue;
        if (best.empty() || bestV < v) {
            best = r;
            bestV = v;
        }
    }
    return best;
}

inline bool updateIsSafe(const std::string& current, const std::string& incoming, bool hasBackup) { return hasBackup && !(parseVersion(incoming) < parseVersion(current)); }

// Privacy centre (550).
inline std::vector<std::string> privacyCentreLines() {
    return {"Crash reports stay on your PC.", "No usage data is sent.", "Signing in and publishing talk to the Kronos server.", "Chat is filtered on your PC."};
}

// Package checks (555, 556, 575).
inline std::vector<std::string> missingFiles(const std::set<std::string>& present, const std::vector<std::string>& required) {
    std::vector<std::string> out;
    for (const auto& r : required) {
        if (!present.count(r)) out.push_back(r);
    }
    return out;
}

inline std::vector<std::string> uninstallTargets(const std::vector<std::string>& installedFiles, bool keepUserData) {
    std::vector<std::string> out;
    for (const auto& f : installedFiles) {
        const bool user = f.rfind("saves/", 0) == 0 || f.rfind("projects/", 0) == 0 || f.rfind("My Games/", 0) == 0;
        if (!(keepUserData && user)) out.push_back(f);
    }
    return out;
}

// Compression profile (566).
struct CompressionProfile {
    std::string textureFormat;
    int maxTextureSide;
    bool streamAudio;
};

inline CompressionProfile compressionFor(const std::string& target) {
    if (target == "mobile") return {"astc", 1024, true};
    if (target == "web") return {"basis", 2048, true};
    return {"bc7", 4096, false};
}

// Build cache (567) and progress (568, 569).
inline bool canReuseBuild(const std::string& cachedInputHash, const std::string& currentInputHash) { return !cachedInputHash.empty() && cachedInputHash == currentInputHash; }

class BuildProgress {
public:
    explicit BuildProgress(int steps) : total_(std::max(steps, 1)) {}
    void advance() { if (!cancelled_ && done_ < total_) ++done_; }
    void cancel() { cancelled_ = true; }
    [[nodiscard]] int percent() const { return done_ * 100 / total_; }
    [[nodiscard]] bool cancelled() const { return cancelled_; }

private:
    int total_, done_ = 0;
    bool cancelled_ = false;
};

struct BuildError {
    std::string file;
    int line = 0;
    std::string message;
};

inline std::vector<BuildError> parseBuildErrors(const std::string& output) {
    std::vector<BuildError> out;
    std::istringstream in(output);
    std::string l;
    while (std::getline(in, l)) {
        const std::size_t e = l.find(": error");
        if (e == std::string::npos) continue;
        const std::string prefix = l.substr(0, e);
        const std::size_t c2 = prefix.rfind(':');
        if (c2 == std::string::npos) continue;
        const std::size_t c1 = c2 == 0 ? std::string::npos : prefix.rfind(':', c2 - 1);
        BuildError b;
        const bool hasColumn = c1 != std::string::npos && std::isdigit(static_cast<unsigned char>(prefix[c1 + 1]));
        const std::size_t lineAt = hasColumn ? c1 : c2;
        b.file = prefix.substr(0, lineAt);
        b.line = std::atoi(prefix.c_str() + lineAt + 1);
        const std::size_t colon = l.find(':', e + 7);
        b.message = colon == std::string::npos ? l.substr(e + 2) : l.substr(colon + 2);
        out.push_back(b);
    }
    return out;
}

// Late join (592): a snapshot split into numbered chunks and put back together.
struct Chunk {
    uint32_t index = 0, count = 0;
    std::string data;
};

inline std::vector<Chunk> splitSnapshot(const std::string& bytes, std::size_t chunkSize) {
    std::vector<Chunk> out;
    const uint32_t count = static_cast<uint32_t>((bytes.size() + chunkSize - 1) / std::max<std::size_t>(chunkSize, 1));
    for (uint32_t i = 0; i < count; ++i) out.push_back({i, count, bytes.substr(i * chunkSize, chunkSize)});
    return out;
}

inline bool joinSnapshot(std::vector<Chunk> chunks, std::string& out) {
    out.clear();
    if (chunks.empty()) return false;
    std::sort(chunks.begin(), chunks.end(), [](const Chunk& a, const Chunk& b) { return a.index < b.index; });
    if (chunks.size() != chunks.front().count) return false;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].index != i) return false;
        out += chunks[i].data;
    }
    return true;
}

// Remote call rate limit (596): token bucket.
class TokenBucket {
public:
    TokenBucket(double perSecond, double burst) : rate_(perSecond), cap_(burst), tokens_(burst) {}
    bool allow(double now) {
        tokens_ = std::min(cap_, tokens_ + (now - last_) * rate_);
        last_ = now;
        if (tokens_ < 1) return false;
        tokens_ -= 1;
        return true;
    }

private:
    double rate_, cap_, tokens_, last_ = 0;
};

// Fuzz harness (597, 816): deterministic mutations of a seed input fed to a parser; returns the count that threw.
inline int fuzzParser(const std::string& seed, int iterations, uint32_t rngSeed, const std::function<void(const std::string&)>& parser) {
    uint32_t x = rngSeed ? rngSeed : 1;
    auto next = [&]() {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return x;
    };
    int threw = 0;
    for (int i = 0; i < iterations; ++i) {
        std::string s = seed;
        const int edits = 1 + static_cast<int>(next() % 4);
        for (int e = 0; e < edits && !s.empty(); ++e) {
            const std::size_t pos = next() % s.size();
            switch (next() % 3) {
            case 0: s[pos] = static_cast<char>(next()); break;
            case 1: s.erase(pos, 1 + next() % 4); break;
            default: s.insert(pos, std::string(1 + next() % 3, static_cast<char>(next()))); break;
            }
        }
        try {
            parser(s);
        } catch (...) {
            ++threw;
        }
    }
    return threw;
}

} // namespace engine::core::product

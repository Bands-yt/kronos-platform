#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/AssetPipelineTools.hpp"

namespace engine::core::importtools {

struct Vec3 {
    float x, y, z;
};

// Procedural mesh from parameters (121): a flat grid of nx by nz quads.
struct GridMesh {
    std::vector<Vec3> positions;
    std::vector<uint32_t> indices;
};

inline GridMesh gridMesh(float width, float depth, int nx, int nz) {
    GridMesh m;
    nx = std::max(nx, 1);
    nz = std::max(nz, 1);
    for (int j = 0; j <= nz; ++j)
        for (int i = 0; i <= nx; ++i) m.positions.push_back({width * (static_cast<float>(i) / nx - 0.5f), 0, depth * (static_cast<float>(j) / nz - 0.5f)});
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * (nx + 1) + i), b = a + 1, c = a + static_cast<uint32_t>(nx + 1), d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    return m;
}

// Mesh operation history (122): operations as text lines that can be saved and replayed through a callback.
class OpHistory {
public:
    void record(const std::string& op) { ops_.push_back(op); }
    [[nodiscard]] std::size_t size() const { return ops_.size(); }
    [[nodiscard]] std::string serialize() const {
        std::string s;
        for (const auto& o : ops_) s += o + "\n";
        return s;
    }
    static OpHistory parse(const std::string& text) {
        OpHistory h;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) h.ops_.push_back(line);
        }
        return h;
    }
    void replay(const std::function<void(const std::string&)>& apply, std::size_t upTo) const {
        for (std::size_t i = 0; i < ops_.size() && i < upTo; ++i) apply(ops_[i]);
    }

private:
    std::vector<std::string> ops_;
};

// Deform brush (123): smooth falloff, 1 at the centre and 0 at the radius.
inline float brushFalloff(float distance, float radius) {
    if (radius <= 0 || distance >= radius) return 0;
    const float t = 1.0f - distance / radius;
    return t * t * (3 - 2 * t);
}

inline void deformBrush(std::vector<Vec3>& points, Vec3 centre, float radius, float amountY) {
    for (auto& p : points) {
        const float d = std::sqrt((p.x - centre.x) * (p.x - centre.x) + (p.z - centre.z) * (p.z - centre.z));
        p.y += amountY * brushFalloff(d, radius);
    }
}

// Sculpt undo (141): each stroke stores the cells it changed.
struct HeightCell {
    int index;
    float before;
};

class SculptUndo {
public:
    void beginStroke() { strokes_.emplace_back(); }
    void touch(const std::vector<float>& heights, int index) {
        if (strokes_.empty()) return;
        for (const auto& c : strokes_.back()) {
            if (c.index == index) return;
        }
        strokes_.back().push_back({index, heights[static_cast<std::size_t>(index)]});
    }
    bool undo(std::vector<float>& heights) {
        if (strokes_.empty()) return false;
        for (const auto& c : strokes_.back()) heights[static_cast<std::size_t>(c.index)] = c.before;
        strokes_.pop_back();
        return true;
    }
    [[nodiscard]] std::size_t strokes() const { return strokes_.size(); }

private:
    std::vector<std::vector<HeightCell>> strokes_;
};

// Import report (155).
struct ImportIssue {
    std::string file, message;
    bool skipped = false;
};

inline std::string importReport(int imported, const std::vector<ImportIssue>& issues) {
    int skipped = 0;
    for (const auto& i : issues) skipped += i.skipped ? 1 : 0;
    std::string out = std::to_string(imported) + " imported, " + std::to_string(issues.size()) + " warnings, " + std::to_string(skipped) + " skipped.\n";
    for (const auto& i : issues) out += (i.skipped ? "SKIPPED " : "warning ") + i.file + ": " + i.message + "\n";
    return out;
}

// Format sniffing (161, 162, 166, 167, 168, 169, 170).
inline bool startsWith(const std::vector<uint8_t>& b, const char* magic, std::size_t offset = 0) {
    const std::size_t n = std::strlen(magic);
    return b.size() >= offset + n && std::memcmp(b.data() + offset, magic, n) == 0;
}

inline std::string detectFormat(const std::vector<uint8_t>& b) {
    if (startsWith(b, "\x89PNG")) return "png";
    if (startsWith(b, "\xFF\xD8\xFF")) return "jpeg";
    if (startsWith(b, "GIF8")) return "gif";
    if (startsWith(b, "BM")) return "bmp";
    if (startsWith(b, "#?RADIANCE")) return "hdr";
    if (b.size() >= 4 && b[0] == 0x76 && b[1] == 0x2f && b[2] == 0x31 && b[3] == 0x01) return "exr";
    if (startsWith(b, "glTF")) return "glb";
    if (startsWith(b, "RIFF") && startsWith(b, "WAVE", 8)) return "wav";
    if (startsWith(b, "OggS")) return "ogg";
    if (startsWith(b, "fLaC")) return "flac";
    if (startsWith(b, "ID3")) return "mp3";
    if (startsWith(b, "ftyp", 4)) return "mp4";
    if (startsWith(b, "wOFF")) return "woff";
    if (startsWith(b, "OTTO")) return "otf";
    if (b.size() >= 4 && b[0] == 0 && b[1] == 1 && b[2] == 0 && b[3] == 0) return "ttf";
    if (startsWith(b, "<svg") || startsWith(b, "<?xml")) return "svg";
    return "unknown";
}

inline bool gltfLooksValid(const std::string& json, std::string& problem) {
    if (json.find("\"asset\"") == std::string::npos) return problem = "missing asset block", false;
    if (json.find("\"version\"") == std::string::npos || json.find("2.0") == std::string::npos) return problem = "not glTF 2.0", false;
    if (json.find("\"meshes\"") == std::string::npos && json.find("\"scenes\"") == std::string::npos) return problem = "no meshes or scenes", false;
    return true;
}

struct ObjCounts {
    int vertices = 0, faces = 0, badFaces = 0;
    std::vector<std::string> materialLibs;
};

inline ObjCounts parseObjCounts(const std::string& text) {
    ObjCounts c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "v") ++c.vertices;
        else if (tag == "mtllib") {
            std::string name;
            ls >> name;
            c.materialLibs.push_back(name);
        } else if (tag == "f") {
            int refs = 0;
            std::string tok;
            bool bad = false;
            while (ls >> tok) {
                const int idx = std::atoi(tok.c_str());
                ++refs;
                if (idx == 0) bad = true;
            }
            if (refs < 3 || bad) ++c.badFaces;
            else ++c.faces;
        }
    }
    return c;
}

struct WavInfo {
    bool ok = false;
    int channels = 0, sampleRate = 0, bits = 0;
    double seconds = 0;
};

inline WavInfo parseWav(const std::vector<uint8_t>& b) {
    WavInfo w;
    if (!startsWith(b, "RIFF") || !startsWith(b, "WAVE", 8)) return w;
    auto u16 = [&](std::size_t o) { return static_cast<int>(b[o] | (b[o + 1] << 8)); };
    auto u32 = [&](std::size_t o) { return static_cast<uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24)); };
    std::size_t pos = 12;
    uint32_t dataBytes = 0;
    while (pos + 8 <= b.size()) {
        const uint32_t size = u32(pos + 4);
        if (std::memcmp(b.data() + pos, "fmt ", 4) == 0 && pos + 24 <= b.size()) {
            w.channels = u16(pos + 10);
            w.sampleRate = static_cast<int>(u32(pos + 12));
            w.bits = u16(pos + 22);
        } else if (std::memcmp(b.data() + pos, "data", 4) == 0) {
            dataBytes = size;
        }
        pos += 8 + size + (size & 1);
    }
    if (w.channels > 0 && w.sampleRate > 0 && w.bits > 0) {
        w.ok = true;
        w.seconds = static_cast<double>(dataBytes) / (w.sampleRate * w.channels * (w.bits / 8.0));
    }
    return w;
}

inline std::string mp4Brand(const std::vector<uint8_t>& b) {
    if (!startsWith(b, "ftyp", 4) || b.size() < 12) return {};
    return std::string(reinterpret_cast<const char*>(b.data() + 8), 4);
}

// Untrusted parser limits (174).
struct ImportLimits {
    uint64_t maxBytes = 256ull << 20;
    uint32_t maxVertices = 20'000'000;
    uint32_t maxTextureSide = 16384;
};

inline std::string checkLimits(const ImportLimits& l, uint64_t bytes, uint32_t vertices, uint32_t textureSide) {
    if (bytes > l.maxBytes) return "file is too big";
    if (vertices > l.maxVertices) return "too many vertices";
    if (textureSide > l.maxTextureSide) return "texture is too large";
    return {};
}

// Corpus regression runner (175): expected summaries against actual ones.
inline std::vector<std::string> corpusRegressions(const std::map<std::string, std::string>& expected, const std::map<std::string, std::string>& actual) {
    std::vector<std::string> out;
    for (const auto& [name, want] : expected) {
        auto it = actual.find(name);
        if (it == actual.end()) out.push_back(name + ": missing");
        else if (it->second != want) out.push_back(name + ": expected " + want + ", got " + it->second);
    }
    return out;
}

// Version history (185).
class AssetVersions {
public:
    int add(const std::vector<uint8_t>& bytes, const std::string& note) {
        versions_.push_back({assets::hex64(assets::contentHash(bytes)), note});
        return static_cast<int>(versions_.size());
    }
    [[nodiscard]] std::size_t count() const { return versions_.size(); }
    [[nodiscard]] std::string hashOf(int version) const { return version >= 1 && version <= static_cast<int>(versions_.size()) ? versions_[static_cast<std::size_t>(version - 1)].first : std::string(); }
    [[nodiscard]] std::string noteOf(int version) const { return version >= 1 && version <= static_cast<int>(versions_.size()) ? versions_[static_cast<std::size_t>(version - 1)].second : std::string(); }

private:
    std::vector<std::pair<std::string, std::string>> versions_;
};

// Licence metadata (187).
struct LicenseInfo {
    std::string license, author, source;
};

inline std::string serializeLicense(const LicenseInfo& l) { return "license=" + l.license + "\nauthor=" + l.author + "\nsource=" + l.source + "\n"; }

inline LicenseInfo parseLicense(const std::string& text) {
    LicenseInfo l;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "license") l.license = v;
        else if (k == "author") l.author = v;
        else if (k == "source") l.source = v;
    }
    return l;
}

inline std::string licenseWarning(const LicenseInfo& l) {
    static const std::set<std::string> known = {"CC0", "CC-BY-4.0", "CC-BY-SA-4.0", "MIT", "Apache-2.0", "own work", "commercial"};
    if (l.license.empty()) return "no licence recorded";
    if (!known.count(l.license)) return "unknown licence: " + l.license;
    if ((l.license == "CC-BY-4.0" || l.license == "CC-BY-SA-4.0") && l.author.empty()) return "needs an author for credit";
    return {};
}

// Preview helpers (190, 191, 192, 193).
inline std::pair<float, float> lightingPreset(const std::string& name) { // sun elevation degrees, ambient 0..1
    if (name == "studio") return {50, 0.35f};
    if (name == "sunset") return {8, 0.2f};
    if (name == "night") return {-20, 0.05f};
    return {60, 0.4f};
}

inline std::string previewerFor(const std::string& format) {
    if (format == "wav" || format == "ogg" || format == "flac" || format == "mp3") return "audio";
    if (format == "mp4") return "video";
    if (format == "png" || format == "jpeg" || format == "gif" || format == "bmp" || format == "svg" || format == "hdr" || format == "exr") return "image";
    if (format == "glb") return "model";
    return "none";
}

inline float clipTime(float t, float length, bool loop) {
    if (length <= 0) return 0;
    if (loop) {
        const float m = std::fmod(t, length);
        return m < 0 ? m + length : m;
    }
    return std::clamp(t, 0.0f, length);
}

inline std::vector<std::string> compareMetadata(const std::map<std::string, std::string>& a, const std::map<std::string, std::string>& b) {
    std::vector<std::string> out;
    std::set<std::string> keys;
    for (const auto& kv : a) keys.insert(kv.first);
    for (const auto& kv : b) keys.insert(kv.first);
    for (const auto& k : keys) {
        auto ia = a.find(k), ib = b.find(k);
        const std::string va = ia == a.end() ? "-" : ia->second, vb = ib == b.end() ? "-" : ib->second;
        if (va != vb) out.push_back(k + ": " + va + " -> " + vb);
    }
    return out;
}

// Dropping an asset into the scene (194): hit the ground plane y = 0 with a ray; null when it points up or away.
inline bool dropOnGround(Vec3 origin, Vec3 dir, Vec3& hit) {
    if (dir.y >= -1e-6f) return false;
    const float t = -origin.y / dir.y;
    if (t < 0) return false;
    hit = {origin.x + dir.x * t, 0, origin.z + dir.z * t};
    return true;
}

// Remote asset cache (195).
inline std::string cacheFileName(const std::string& url) {
    std::string safe;
    for (char c : url) safe += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return safe.substr(0, 80) + "_" + assets::hex64(assets::contentHash(std::vector<uint8_t>(url.begin(), url.end()))).substr(0, 8);
}

inline bool cacheExpired(long long storedAt, long long now, long long ttlSeconds) { return now - storedAt > ttlSeconds; }

// Asset database repair (199).
struct DbRepair {
    std::vector<std::string> missingFiles, orphanFiles;
};

inline DbRepair repairPlan(const std::set<std::string>& inDatabase, const std::set<std::string>& onDisk) {
    DbRepair r;
    for (const auto& n : inDatabase) {
        if (!onDisk.count(n)) r.missingFiles.push_back(n);
    }
    for (const auto& n : onDisk) {
        if (!inDatabase.count(n)) r.orphanFiles.push_back(n);
    }
    return r;
}

// Keyboard navigation in a grid (200).
inline int gridNavigate(int index, const std::string& key, int columns, int count) {
    if (count <= 0) return -1;
    int i = index;
    if (key == "left") i = index - 1;
    else if (key == "right") i = index + 1;
    else if (key == "up") i = index - columns;
    else if (key == "down") i = index + columns;
    else if (key == "home") i = 0;
    else if (key == "end") i = count - 1;
    return std::clamp(i, 0, count - 1);
}

// UV preview (208): how many UVs sit outside 0..1 and the bounds.
struct UvInfo {
    float minU = 0, minV = 0, maxU = 0, maxV = 0;
    int outside = 0;
};

inline UvInfo uvInfo(const std::vector<std::pair<float, float>>& uv) {
    UvInfo r;
    bool first = true;
    for (const auto& [u, v] : uv) {
        if (first) {
            r.minU = r.maxU = u;
            r.minV = r.maxV = v;
            first = false;
        }
        r.minU = std::min(r.minU, u);
        r.maxU = std::max(r.maxU, u);
        r.minV = std::min(r.minV, v);
        r.maxV = std::max(r.maxV, v);
        if (u < 0 || u > 1 || v < 0 || v > 1) ++r.outside;
    }
    return r;
}

// Decal projection bounds (211): a point is inside when it is within half-extents of the decal centre (no rotation).
inline bool insideDecal(Vec3 centre, Vec3 half, Vec3 p) { return std::fabs(p.x - centre.x) <= half.x && std::fabs(p.y - centre.y) <= half.y && std::fabs(p.z - centre.z) <= half.z; }

// Shader cache (213, 214): key from source and defines; changed when the key differs.
inline std::string shaderCacheKey(const std::string& source, const std::vector<std::string>& defines) {
    std::string all = source;
    for (const auto& d : defines) all += "\n#define " + d;
    return assets::hex64(assets::contentHash(std::vector<uint8_t>(all.begin(), all.end())));
}

inline std::map<std::string, int> groupDiagnostics(const std::vector<std::string>& messages) {
    std::map<std::string, int> out;
    for (const auto& m : messages) {
        std::string key;
        bool lastDigit = false;
        for (char c : m) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                if (!lastDigit) key += '#';
                lastDigit = true;
            } else {
                key += c;
                lastDigit = false;
            }
        }
        ++out[key];
    }
    return out;
}

// Debug views (216): overdraw count to a heat colour name.
inline std::string overdrawHeat(int layers) { return layers <= 1 ? "cool" : layers <= 3 ? "warm" : layers <= 6 ? "hot" : "critical"; }

// Graph migration (222): applies version steps one after another.
inline int migrateVersion(int from, int to, const std::function<void(int)>& step) {
    int v = from;
    while (v < to) {
        step(v);
        ++v;
    }
    return v;
}

// Material graph evaluation (225): a tiny node graph for unit tests.
struct GraphNode {
    std::string op; // "const", "add", "mul"
    float value = 0;
    int a = -1, b = -1;
};

inline float evalGraph(const std::vector<GraphNode>& nodes, int index, int depth = 0) {
    if (index < 0 || index >= static_cast<int>(nodes.size()) || depth > 64) return 0;
    const GraphNode& n = nodes[static_cast<std::size_t>(index)];
    if (n.op == "const") return n.value;
    const float x = evalGraph(nodes, n.a, depth + 1), y = evalGraph(nodes, n.b, depth + 1);
    return n.op == "add" ? x + y : n.op == "mul" ? x * y : 0;
}

} // namespace engine::core::importtools

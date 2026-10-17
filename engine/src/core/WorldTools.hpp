#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::world {

struct V3 {
    float x = 0, y = 0, z = 0;
};

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalized(V3 a) {
    const float l = length(a);
    return l > 1e-8f ? a * (1.0f / l) : V3{};
}

// ---- Camera (76 to 82)
inline V3 orbitPosition(V3 target, float yawDeg, float pitchDeg, float distance) {
    const float y = yawDeg * 0.0174532925f, p = pitchDeg * 0.0174532925f;
    return target + V3{std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)} * distance;
}

inline std::pair<float, float> orbitPreset(const std::string& name) { // yaw, pitch
    if (name == "front") return {0, 0};
    if (name == "right") return {90, 0};
    if (name == "back") return {180, 0};
    if (name == "left") return {270, 0};
    if (name == "top") return {0, 89};
    return {35, 25};
}

inline float frameDistance(float boundingRadius, float verticalFovDeg) { return boundingRadius / std::sin(verticalFovDeg * 0.5f * 0.0174532925f); }

inline float approach(float current, float target, float dt, float speed) { return current + (target - current) * (1.0f - std::exp(-speed * dt)); }

inline float adjustFlySpeed(float speed, int wheelSteps) { return std::clamp(speed * std::pow(1.25f, static_cast<float>(wheelSteps)), 0.5f, 500.0f); }

inline float horizontalFov(float verticalDeg, float aspect) { return 2.0f * std::atan(std::tan(verticalDeg * 0.5f * 0.0174532925f) * aspect) * 57.2957795f; }

inline float verticalFovFromHorizontal(float horizontalDeg, float aspect) { return 2.0f * std::atan(std::tan(horizontalDeg * 0.5f * 0.0174532925f) / aspect) * 57.2957795f; }

// ---- Hierarchy (54, 57)
struct Node {
    std::string name, type;
    std::set<std::string> tags;
};

inline std::vector<int> searchNodes(const std::vector<Node>& nodes, const std::string& text, const std::string& type, const std::string& tag) {
    auto low = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::vector<int> out;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (!text.empty() && low(n.name).find(low(text)) == std::string::npos) continue;
        if (!type.empty() && n.type != type) continue;
        if (!tag.empty() && !n.tags.count(tag)) continue;
        out.push_back(static_cast<int>(i));
    }
    return out;
}

inline std::string validateName(const std::string& name, const std::vector<std::string>& siblings) {
    if (name.empty()) return "A name can't be empty.";
    if (name.size() > 100) return "That name is too long.";
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return "A name can't contain slashes.";
    if (std::find(siblings.begin(), siblings.end(), name) != siblings.end()) return "Another object here already has this name.";
    return {};
}

// ---- Mesh editing (101 to 113)
inline int uvSphereVertexCount(int segments, int rings) { return (segments + 1) * (rings + 1); }
inline int uvSphereTriangleCount(int segments, int rings) { return segments * (rings - 1) * 2; }

inline std::vector<V3> cylinderRing(float radius, float y, int segments) {
    std::vector<V3> out;
    for (int i = 0; i < segments; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
        out.push_back({std::cos(a) * radius, y, std::sin(a) * radius});
    }
    return out;
}

inline V3 centroid(const std::vector<V3>& poly) {
    V3 c;
    for (const auto& p : poly) c = c + p;
    return poly.empty() ? c : c * (1.0f / static_cast<float>(poly.size()));
}

inline V3 polygonNormal(const std::vector<V3>& poly) {
    V3 n;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const V3 a = poly[i], b = poly[(i + 1) % poly.size()];
        n = n + V3{(a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)};
    }
    return normalized(n);
}

// Inset (103): corners move toward the centre by `amount` (0 = same, 1 = a point).
inline std::vector<V3> insetPolygon(const std::vector<V3>& poly, float amount) {
    const V3 c = centroid(poly);
    std::vector<V3> out;
    for (const auto& p : poly) out.push_back(p + (c - p) * amount);
    return out;
}

// Extrude (102): the inset ring moved along the normal; returns the new top ring.
inline std::vector<V3> extrudeFace(const std::vector<V3>& face, float inset, float offset) {
    const V3 n = polygonNormal(face);
    std::vector<V3> out = insetPolygon(face, inset);
    for (auto& p : out) p = p + n * offset;
    return out;
}

// Bevel (103): a corner replaced by two points along its edges.
inline std::pair<V3, V3> bevelCorner(V3 prev, V3 corner, V3 next, float amount) {
    return {corner + normalized(prev - corner) * amount, corner + normalized(next - corner) * amount};
}

// Edge slide and loop cut (104).
inline V3 slideOnEdge(V3 a, V3 b, float t) { return a + (b - a) * t; }

inline std::vector<V3> loopCutPoints(const std::vector<V3>& ringA, const std::vector<V3>& ringB, float t) {
    std::vector<V3> out;
    for (std::size_t i = 0; i < ringA.size() && i < ringB.size(); ++i) out.push_back(slideOnEdge(ringA[i], ringB[i], t));
    return out;
}

// Plane cut (106): intersection of a segment with a plane (normal n, offset d: n.p = d).
inline bool segmentPlane(V3 a, V3 b, V3 n, float d, V3& hit) {
    const float da = dot(n, a) - d, db = dot(n, b) - d;
    if ((da > 0) == (db > 0) || da == db) return false;
    hit = slideOnEdge(a, b, da / (da - db));
    return true;
}

inline int classifyPolygon(const std::vector<V3>& poly, V3 n, float d) { // 1 above, -1 below, 0 straddles
    bool above = false, below = false;
    for (const auto& p : poly) {
        const float s = dot(n, p) - d;
        above = above || s > 1e-6f;
        below = below || s < -1e-6f;
    }
    return above && below ? 0 : above ? 1 : -1;
}

// Boolean operations (107), boxes only.
struct Box3 {
    V3 lo, hi;
};

inline bool boxesOverlap(const Box3& a, const Box3& b) { return a.lo.x < b.hi.x && a.hi.x > b.lo.x && a.lo.y < b.hi.y && a.hi.y > b.lo.y && a.lo.z < b.hi.z && a.hi.z > b.lo.z; }

inline Box3 boxIntersect(const Box3& a, const Box3& b) { return {{std::max(a.lo.x, b.lo.x), std::max(a.lo.y, b.lo.y), std::max(a.lo.z, b.lo.z)}, {std::min(a.hi.x, b.hi.x), std::min(a.hi.y, b.hi.y), std::min(a.hi.z, b.hi.z)}}; }

inline float boxVolume(const Box3& b) { return std::max(0.0f, b.hi.x - b.lo.x) * std::max(0.0f, b.hi.y - b.lo.y) * std::max(0.0f, b.hi.z - b.lo.z); }

inline float unionVolume(const Box3& a, const Box3& b) { return boxVolume(a) + boxVolume(b) - (boxesOverlap(a, b) ? boxVolume(boxIntersect(a, b)) : 0.0f); }

// a minus b as up to six boxes.
inline std::vector<Box3> boxSubtract(const Box3& a, const Box3& b) {
    if (!boxesOverlap(a, b)) return {a};
    const Box3 i = boxIntersect(a, b);
    std::vector<Box3> out;
    if (i.lo.x > a.lo.x) out.push_back({a.lo, {i.lo.x, a.hi.y, a.hi.z}});
    if (i.hi.x < a.hi.x) out.push_back({{i.hi.x, a.lo.y, a.lo.z}, a.hi});
    if (i.lo.y > a.lo.y) out.push_back({{i.lo.x, a.lo.y, a.lo.z}, {i.hi.x, i.lo.y, a.hi.z}});
    if (i.hi.y < a.hi.y) out.push_back({{i.lo.x, i.hi.y, a.lo.z}, {i.hi.x, a.hi.y, a.hi.z}});
    if (i.lo.z > a.lo.z) out.push_back({{i.lo.x, i.lo.y, a.lo.z}, {i.hi.x, i.hi.y, i.lo.z}});
    if (i.hi.z < a.hi.z) out.push_back({{i.lo.x, i.lo.y, i.hi.z}, {i.hi.x, i.hi.y, a.hi.z}});
    return out;
}

// Modifier stack (108): modifiers apply in order and can be switched off.
struct Modifier {
    std::string kind; // "mirrorX", "scale", "offset"
    float amount = 1;
    bool enabled = true;
};

inline std::vector<V3> applyModifiers(std::vector<V3> pts, const std::vector<Modifier>& stack) {
    for (const auto& m : stack) {
        if (!m.enabled) continue;
        if (m.kind == "scale") {
            for (auto& p : pts) p = p * m.amount;
        } else if (m.kind == "offset") {
            for (auto& p : pts) p.y += m.amount;
        } else if (m.kind == "mirrorX") {
            const std::size_t n = pts.size();
            for (std::size_t i = 0; i < n; ++i) pts.push_back({-pts[i].x, pts[i].y, pts[i].z});
        }
    }
    return pts;
}

// Decimate (113): vertex clustering on a grid; returns how many vertices remain.
inline std::vector<V3> clusterDecimate(const std::vector<V3>& pts, float cell) {
    std::map<std::array<int, 3>, std::pair<V3, int>> bins;
    for (const auto& p : pts) {
        const std::array<int, 3> key = {static_cast<int>(std::floor(p.x / cell)), static_cast<int>(std::floor(p.y / cell)), static_cast<int>(std::floor(p.z / cell))};
        auto& b = bins[key];
        b.first = b.first + p;
        ++b.second;
    }
    std::vector<V3> out;
    for (const auto& kv : bins) out.push_back(kv.second.first * (1.0f / static_cast<float>(kv.second.second)));
    return out;
}

// ---- Terrain (126 to 148)
struct Heightmap {
    int w = 0, h = 0;
    std::vector<float> v;
    Heightmap() = default;
    Heightmap(int width, int height) : w(width), h(height), v(static_cast<std::size_t>(width * height), 0.0f) {}
    float& at(int x, int y) { return v[static_cast<std::size_t>(y * w + x)]; }
    [[nodiscard]] float at(int x, int y) const { return v[static_cast<std::size_t>(y * w + x)]; }
    [[nodiscard]] bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
};

// 16-bit binary PGM (126).
inline std::string exportPgm16(const Heightmap& m, float maxHeight) {
    std::string out = "P5\n" + std::to_string(m.w) + " " + std::to_string(m.h) + "\n65535\n";
    for (float f : m.v) {
        const uint16_t q = static_cast<uint16_t>(std::clamp(f / maxHeight, 0.0f, 1.0f) * 65535.0f + 0.5f);
        out += static_cast<char>(q >> 8);
        out += static_cast<char>(q & 255);
    }
    return out;
}

inline bool importPgm16(const std::string& data, float maxHeight, Heightmap& out) {
    std::istringstream in(data);
    std::string magic;
    int w = 0, h = 0, maxv = 0;
    in >> magic >> w >> h >> maxv;
    if (magic != "P5" || w <= 0 || h <= 0 || maxv != 65535) return false;
    in.get();
    out = Heightmap(w, h);
    for (auto& f : out.v) {
        const int hi = in.get(), lo = in.get();
        if (hi < 0 || lo < 0) return false;
        f = static_cast<float>((hi << 8) | lo) / 65535.0f * maxHeight;
    }
    return true;
}

inline float brushWeight(float dist, float radius) {
    if (dist >= radius || radius <= 0) return 0;
    const float t = 1.0f - dist / radius;
    return t * t * (3 - 2 * t);
}

enum class BrushMode { Raise, Lower, Smooth, Flatten };

inline void paintTerrain(Heightmap& m, float cx, float cy, float radius, float strength, BrushMode mode, float flattenTo = 0) {
    for (int y = std::max(0, static_cast<int>(cy - radius)); y <= std::min(m.h - 1, static_cast<int>(cy + radius)); ++y) {
        for (int x = std::max(0, static_cast<int>(cx - radius)); x <= std::min(m.w - 1, static_cast<int>(cx + radius)); ++x) {
            const float wgt = brushWeight(std::hypot(static_cast<float>(x) - cx, static_cast<float>(y) - cy), radius) * strength;
            if (wgt <= 0) continue;
            float& h = m.at(x, y);
            if (mode == BrushMode::Raise) h += wgt;
            else if (mode == BrushMode::Lower) h -= wgt;
            else if (mode == BrushMode::Flatten) h += (flattenTo - h) * std::min(wgt, 1.0f);
            else {
                float sum = 0;
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (m.inside(x + dx, y + dy)) {
                            sum += m.at(x + dx, y + dy);
                            ++n;
                        }
                h += (sum / static_cast<float>(n) - h) * std::min(wgt, 1.0f);
            }
        }
    }
}

// Thermal erosion (128, 148): material slides down slopes steeper than `talus`.
inline void thermalErosion(Heightmap& m, float talus, float rate, int iterations) {
    for (int it = 0; it < iterations; ++it) {
        Heightmap next = m;
        for (int y = 0; y < m.h; ++y)
            for (int x = 0; x < m.w; ++x) {
                float total = 0, maxDiff = 0;
                for (const auto& d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                    if (!m.inside(x + d.first, y + d.second)) continue;
                    const float diff = m.at(x, y) - m.at(x + d.first, y + d.second);
                    if (diff > talus) {
                        total += diff - talus;
                        maxDiff = std::max(maxDiff, diff);
                    }
                }
                if (total <= 0) continue;
                for (const auto& d : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                    if (!m.inside(x + d.first, y + d.second)) continue;
                    const float diff = m.at(x, y) - m.at(x + d.first, y + d.second);
                    if (diff > talus) {
                        const float moved = rate * (maxDiff - talus) * 0.5f * (diff - talus) / total;
                        next.at(x, y) -= moved;
                        next.at(x + d.first, y + d.second) += moved;
                    }
                }
            }
        m = next;
    }
}

// Layers (129): weights per cell always sum to 1.
inline void paintLayer(std::vector<float>& weights, int layer, float amount) {
    if (layer < 0 || layer >= static_cast<int>(weights.size())) return;
    weights[static_cast<std::size_t>(layer)] = std::clamp(weights[static_cast<std::size_t>(layer)] + amount, 0.0f, 1.0f);
    float sum = 0;
    for (float w : weights) sum += w;
    if (sum <= 0) {
        weights[0] = 1;
        return;
    }
    for (float& w : weights) w /= sum;
}

// Chunk streaming (130) and LOD (133).
struct ChunkId {
    int x, z;
    bool operator<(const ChunkId& o) const { return x != o.x ? x < o.x : z < o.z; }
    bool operator==(const ChunkId& o) const { return x == o.x && z == o.z; }
};

struct StreamPlan {
    std::vector<ChunkId> load, unload;
};

inline StreamPlan planChunkStreaming(float px, float pz, float chunkSize, int loadRadius, const std::set<ChunkId>& loaded) {
    const int cx = static_cast<int>(std::floor(px / chunkSize)), cz = static_cast<int>(std::floor(pz / chunkSize));
    std::set<ChunkId> want;
    for (int dz = -loadRadius; dz <= loadRadius; ++dz)
        for (int dx = -loadRadius; dx <= loadRadius; ++dx) want.insert({cx + dx, cz + dz});
    StreamPlan p;
    for (const auto& c : want) {
        if (!loaded.count(c)) p.load.push_back(c);
    }
    for (const auto& c : loaded) {
        if (std::abs(c.x - cx) > loadRadius + 1 || std::abs(c.z - cz) > loadRadius + 1) p.unload.push_back(c);
    }
    return p;
}

inline int chunkLod(float distance, float chunkSize) { return distance < chunkSize * 1.5f ? 0 : distance < chunkSize * 4.0f ? 1 : distance < chunkSize * 8.0f ? 2 : 3; }

// Neighbouring chunks that differ by more than one level need a skirt or stitch to avoid cracks.
inline bool needsStitch(int lodA, int lodB) { return std::abs(lodA - lodB) >= 1; }

inline int gridResolutionAtLod(int base, int lod) { return std::max(2, (base - 1) >> lod) + 1; }

// Voxels (131, 143).
class VoxelGrid {
public:
    VoxelGrid(int x, int y, int z) : x_(x), y_(y), z_(z), cells_(static_cast<std::size_t>(x * y * z), 0) {}
    bool set(int x, int y, int z, uint8_t v) {
        if (!inside(x, y, z)) return false;
        cells_[idx(x, y, z)] = v;
        return true;
    }
    [[nodiscard]] uint8_t get(int x, int y, int z) const { return inside(x, y, z) ? cells_[idx(x, y, z)] : 0; }
    [[nodiscard]] int count() const { return static_cast<int>(std::count_if(cells_.begin(), cells_.end(), [](uint8_t c) { return c != 0; })); }
    // Carves a sphere (caves and overhangs); returns the number of cells removed.
    int carveSphere(float cx, float cy, float cz, float r) {
        int removed = 0;
        for (int z = 0; z < z_; ++z)
            for (int y = 0; y < y_; ++y)
                for (int x = 0; x < x_; ++x) {
                    const float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy, dz = static_cast<float>(z) - cz;
                    if (dx * dx + dy * dy + dz * dz <= r * r && cells_[idx(x, y, z)]) {
                        cells_[idx(x, y, z)] = 0;
                        ++removed;
                    }
                }
        return removed;
    }

private:
    [[nodiscard]] bool inside(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < x_ && y < y_ && z < z_; }
    [[nodiscard]] std::size_t idx(int x, int y, int z) const { return static_cast<std::size_t>((z * y_ + y) * x_ + x); }
    int x_, y_, z_;
    std::vector<uint8_t> cells_;
};

// Origin rebasing (145).
inline bool shouldRebase(V3 camera, float threshold) { return std::fabs(camera.x) > threshold || std::fabs(camera.z) > threshold; }

inline V3 rebaseOffset(V3 camera, float grid) { return {std::round(camera.x / grid) * grid, 0, std::round(camera.z / grid) * grid}; }

// Import presets (154).
struct ImportPreset {
    int maxTexture;
    bool generateLods, compressTextures;
};

inline ImportPreset importPresetFor(const std::string& platform) {
    if (platform == "mobile") return {1024, true, true};
    if (platform == "web") return {2048, true, true};
    return {4096, false, false};
}

// Dependencies (157, 182).
class DependencyGraph {
public:
    void add(const std::string& asset, const std::string& dependsOn) { deps_[asset].insert(dependsOn); deps_[dependsOn]; }
    [[nodiscard]] std::vector<std::string> usedBy(const std::string& asset) const {
        std::vector<std::string> out;
        for (const auto& [a, d] : deps_) {
            if (d.count(asset)) out.push_back(a);
        }
        return out;
    }
    [[nodiscard]] bool hasCycle() const {
        std::map<std::string, int> state;
        bool cyc = false;
        auto visit = [&](auto&& self, const std::string& n) -> void {
            if (cyc || state[n] == 2) return;
            if (state[n] == 1) { cyc = true; return; }
            state[n] = 1;
            for (const auto& d : deps_.at(n)) self(self, d);
            state[n] = 2;
        };
        for (const auto& kv : deps_) visit(visit, kv.first);
        return cyc;
    }
    [[nodiscard]] std::string deleteWarning(const std::string& asset) const {
        const auto u = usedBy(asset);
        return u.empty() ? std::string() : asset + " is used by " + std::to_string(u.size()) + " other asset(s)";
    }

private:
    std::map<std::string, std::set<std::string>> deps_;
};

// Thumbnail cache (176): least recently used.
class ThumbnailCache {
public:
    explicit ThumbnailCache(std::size_t cap) : cap_(cap) {}
    void put(const std::string& key) {
        order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
        order_.push_back(key);
        if (order_.size() > cap_) order_.erase(order_.begin());
    }
    [[nodiscard]] bool has(const std::string& key) const { return std::find(order_.begin(), order_.end(), key) != order_.end(); }

private:
    std::size_t cap_;
    std::vector<std::string> order_;
};

// Asset search and smart collections (177, 178, 179).
struct AssetInfo {
    std::string name, type;
    std::set<std::string> tags;
    int usedByCount = 0;
};

struct SmartRule {
    std::string type, tag;
    bool unusedOnly = false;
};

inline bool ruleMatches(const SmartRule& r, const AssetInfo& a) { return (r.type.empty() || r.type == a.type) && (r.tag.empty() || a.tags.count(r.tag)) && (!r.unusedOnly || a.usedByCount == 0); }

inline std::string movePath(const std::string& path, const std::string& fromFolder, const std::string& toFolder) {
    if (path.rfind(fromFolder + "/", 0) != 0) return path;
    return toFolder + path.substr(fromFolder.size());
}

// Material tools (201 to 207).
struct MatNode {
    std::string id, kind;
    std::vector<std::string> inputs;
};

inline std::vector<std::string> graphProblems(const std::vector<MatNode>& nodes) {
    std::set<std::string> ids;
    for (const auto& n : nodes) ids.insert(n.id);
    std::vector<std::string> out;
    for (const auto& n : nodes)
        for (const auto& in : n.inputs)
            if (!ids.count(in)) out.push_back(n.id + ": input " + in + " does not exist");
    return out;
}

inline std::vector<std::string> evaluationOrder(const std::vector<MatNode>& nodes) {
    std::map<std::string, const MatNode*> byId;
    for (const auto& n : nodes) byId[n.id] = &n;
    std::vector<std::string> order;
    std::set<std::string> done, active;
    bool ok = true;
    auto visit = [&](auto&& self, const std::string& id) -> void {
        if (done.count(id) || !ok) return;
        if (!active.insert(id).second) { ok = false; return; }
        auto it = byId.find(id);
        if (it != byId.end()) {
            for (const auto& in : it->second->inputs) self(self, in);
        }
        active.erase(id);
        done.insert(id);
        order.push_back(id);
    };
    for (const auto& n : nodes) visit(visit, n.id);
    if (!ok) order.clear();
    return order;
}

// Packs metallic and roughness (203) and builds a normal from heights (204).
inline uint32_t packOrm(uint8_t occlusion, uint8_t roughness, uint8_t metallic) { return (static_cast<uint32_t>(occlusion) << 16) | (static_cast<uint32_t>(roughness) << 8) | metallic; }

inline V3 normalFromHeights(float left, float right, float down, float up, float strength) { return normalized({(left - right) * strength, 2.0f, (down - up) * strength}); }

// Tiling (207).
inline float wrapCoord(float u, const std::string& mode) {
    if (mode == "clamp") return std::clamp(u, 0.0f, 1.0f);
    if (mode == "mirror") {
        const float f = std::fmod(std::fabs(u), 2.0f);
        return f <= 1.0f ? f : 2.0f - f;
    }
    return u - std::floor(u);
}

} // namespace engine::core::world

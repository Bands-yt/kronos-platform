#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace engine::core::gfxanim {

// ---- Frame graph (226): a resource lives from its first to its last pass; two resources can share memory when their lives don't overlap.
struct PassUse {
    std::string pass;
    std::vector<std::string> reads, writes;
};

struct Lifetime {
    int first = 1 << 30, last = -1;
};

inline std::map<std::string, Lifetime> resourceLifetimes(const std::vector<PassUse>& passes) {
    std::map<std::string, Lifetime> out;
    for (std::size_t i = 0; i < passes.size(); ++i) {
        for (const auto* list : {&passes[i].reads, &passes[i].writes})
            for (const auto& r : *list) {
                auto& l = out[r];
                l.first = std::min(l.first, static_cast<int>(i));
                l.last = std::max(l.last, static_cast<int>(i));
            }
    }
    return out;
}

inline bool canAlias(const Lifetime& a, const Lifetime& b) { return a.last < b.first || b.last < a.first; }

// A resource that is read but never written, or written but never read.
inline std::vector<std::string> graphWarnings(const std::vector<PassUse>& passes) {
    std::set<std::string> r, w;
    for (const auto& p : passes) {
        r.insert(p.reads.begin(), p.reads.end());
        w.insert(p.writes.begin(), p.writes.end());
    }
    std::vector<std::string> out;
    for (const auto& n : r)
        if (!w.count(n)) out.push_back(n + " is read but never written");
    for (const auto& n : w)
        if (!r.count(n)) out.push_back(n + " is written but never read");
    return out;
}

// ---- GPU resource tracking (227).
class ResourceTracker {
public:
    void created(const std::string& kind, const std::string& name) { ++live_[kind + ":" + name]; }
    void destroyed(const std::string& kind, const std::string& name) {
        auto it = live_.find(kind + ":" + name);
        if (it != live_.end() && --it->second <= 0) live_.erase(it);
    }
    [[nodiscard]] std::vector<std::string> leaks() const {
        std::vector<std::string> out;
        for (const auto& kv : live_) out.push_back(kv.first);
        return out;
    }

private:
    std::map<std::string, int> live_;
};

// Bindless support (228): needs descriptor indexing features.
struct DescriptorFeatures {
    bool runtimeDescriptorArray = false, partiallyBound = false, updateAfterBind = false, nonUniformIndexing = false;
};

inline bool bindlessSupported(const DescriptorFeatures& f) { return f.runtimeDescriptorArray && f.partiallyBound && f.updateAfterBind && f.nonUniformIndexing; }

// Multi-draw indirect (229): consecutive draws with the same pipeline and material become one command.
struct DrawItem {
    int pipeline, material;
    uint32_t firstIndex, indexCount;
};

struct IndirectCommand {
    int pipeline;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
};

inline std::vector<IndirectCommand> buildIndirect(std::vector<DrawItem> draws) {
    std::stable_sort(draws.begin(), draws.end(), [](const DrawItem& a, const DrawItem& b) { return a.pipeline < b.pipeline; });
    std::vector<IndirectCommand> out;
    for (const auto& d : draws) {
        if (out.empty() || out.back().pipeline != d.pipeline) out.push_back({d.pipeline, {}});
        out.back().ranges.push_back({d.firstIndex, d.indexCount});
    }
    return out;
}

// State change count (232).
inline int stateChanges(const std::vector<DrawItem>& draws) {
    int n = 0, prevP = -1, prevM = -1;
    for (const auto& d : draws) {
        if (d.pipeline != prevP) ++n;
        if (d.material != prevM) ++n;
        prevP = d.pipeline;
        prevM = d.material;
    }
    return n;
}

// Software occlusion (231): a box covering a screen rectangle at nearest depth z is hidden when every depth cell in it is closer.
struct DepthBuffer {
    int w, h;
    std::vector<float> depth;
    DepthBuffer(int width, int height, float fill) : w(width), h(height), depth(static_cast<std::size_t>(width * height), fill) {}
    void drawRect(int x0, int y0, int x1, int y1, float z) {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x) depth[static_cast<std::size_t>(y * w + x)] = std::min(depth[static_cast<std::size_t>(y * w + x)], z);
    }
    [[nodiscard]] bool occluded(int x0, int y0, int x1, int y1, float nearestZ) const {
        for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(w, x1); ++x)
                if (depth[static_cast<std::size_t>(y * w + x)] > nearestZ) return false;
        return true;
    }
};

// Meshlets (247, 248): triangles are grouped until a vertex or triangle limit is reached.
struct Meshlet {
    std::vector<uint32_t> triangles; // triangle indices
    std::set<uint32_t> vertices;
};

inline std::vector<Meshlet> buildMeshlets(const std::vector<uint32_t>& indices, std::size_t maxVertices, std::size_t maxTriangles) {
    std::vector<Meshlet> out(1);
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        std::set<uint32_t> add = out.back().vertices;
        add.insert({indices[t], indices[t + 1], indices[t + 2]});
        if (add.size() > maxVertices || out.back().triangles.size() >= maxTriangles) {
            out.emplace_back();
            add = {indices[t], indices[t + 1], indices[t + 2]};
        }
        out.back().vertices = add;
        out.back().triangles.push_back(static_cast<uint32_t>(t / 3));
    }
    if (out.back().triangles.empty()) out.pop_back();
    return out;
}

// ---- Lights (251, 252, 253)
inline float pointLightAttenuation(float distance, float range) {
    if (distance >= range) return 0;
    const float d2 = std::max(distance * distance, 0.0001f);
    const float window = std::pow(std::clamp(1.0f - std::pow(distance / range, 4.0f), 0.0f, 1.0f), 2.0f);
    return window / d2;
}

inline float spotFactor(float cosAngle, float innerCos, float outerCos) {
    if (cosAngle >= innerCos) return 1;
    if (cosAngle <= outerCos) return 0;
    const float t = (cosAngle - outerCos) / (innerCos - outerCos);
    return t * t * (3 - 2 * t);
}

struct ShadowPreset {
    int resolution, cascades;
    bool softShadows;
};

inline ShadowPreset shadowPreset(const std::string& name) {
    if (name == "low") return {1024, 2, false};
    if (name == "medium") return {2048, 3, true};
    if (name == "high") return {4096, 4, true};
    return {512, 1, false};
}

inline std::vector<float> cascadeSplits(float nearPlane, float farPlane, int cascades, float lambda) {
    std::vector<float> out;
    for (int i = 1; i <= cascades; ++i) {
        const float f = static_cast<float>(i) / static_cast<float>(cascades);
        const float log = nearPlane * std::pow(farPlane / nearPlane, f), lin = nearPlane + (farPlane - nearPlane) * f;
        out.push_back(lambda * log + (1 - lambda) * lin);
    }
    return out;
}

inline float snapToTexel(float coord, float worldUnitsPerTexel) { return worldUnitsPerTexel > 0 ? std::floor(coord / worldUnitsPerTexel) * worldUnitsPerTexel : coord; }

// Probe volumes (256), reflection probes (257).
inline std::vector<std::array<float, 3>> probeGrid(float x0, float y0, float z0, float spacing, int nx, int ny, int nz) {
    std::vector<std::array<float, 3>> out;
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) out.push_back({x0 + static_cast<float>(x) * spacing, y0 + static_cast<float>(y) * spacing, z0 + static_cast<float>(z) * spacing});
    return out;
}

inline std::vector<float> probeBlendWeights(const std::vector<float>& distances, const std::vector<float>& radii) {
    std::vector<float> w(distances.size(), 0.0f);
    float sum = 0;
    for (std::size_t i = 0; i < distances.size() && i < radii.size(); ++i) {
        w[i] = radii[i] > 0 ? std::clamp(1.0f - distances[i] / radii[i], 0.0f, 1.0f) : 0.0f;
        sum += w[i];
    }
    if (sum > 0)
        for (float& x : w) x /= sum;
    return w;
}

// SSAO kernel (258): deterministic hemisphere samples, denser near the origin.
inline std::vector<std::array<float, 3>> ssaoKernel(int count) {
    std::vector<std::array<float, 3>> out;
    uint32_t s = 12345;
    auto rnd = [&]() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.0f;
    };
    for (int i = 0; i < count; ++i) {
        float x = rnd() * 2 - 1, y = rnd() * 2 - 1, z = rnd();
        const float l = std::sqrt(x * x + y * y + z * z);
        if (l < 1e-5f) { --i; continue; }
        const float scale = 0.1f + 0.9f * std::pow(static_cast<float>(i) / static_cast<float>(count), 2.0f);
        out.push_back({x / l * scale, y / l * scale, z / l * scale});
    }
    return out;
}

// Volumetric fog (260): Beer-Lambert transmittance.
inline float fogTransmittance(float density, float distance) { return std::exp(-density * distance); }

// ---- Particles (276 to 282)
struct Particle {
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, age = 0, life = 1;
    std::vector<std::array<float, 3>> trail;
};

struct Emitter {
    float ratePerSecond = 10;
    int burstCount = 0;
    float gravity = -9.81f, bounce = 0.4f, groundY = 0, life = 2;
    std::size_t trailLength = 0;
    float carry = 0;
};

inline void emitParticles(const Emitter& e, float dt, std::vector<Particle>& out, Emitter& state) {
    state.carry += e.ratePerSecond * dt;
    while (state.carry >= 1.0f) {
        Particle p;
        p.life = e.life;
        p.vy = 5;
        p.vx = static_cast<float>(out.size() % 5) - 2.0f;
        out.push_back(p);
        state.carry -= 1.0f;
    }
}

inline void burst(const Emitter& e, std::vector<Particle>& out) {
    for (int i = 0; i < e.burstCount; ++i) {
        Particle p;
        p.life = e.life;
        p.vy = 4;
        p.vx = std::cos(static_cast<float>(i)) * 3;
        p.vz = std::sin(static_cast<float>(i)) * 3;
        out.push_back(p);
    }
}

inline void stepParticles(const Emitter& e, float dt, std::vector<Particle>& ps) {
    for (auto& p : ps) {
        p.vy += e.gravity * dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;
        p.age += dt;
        if (p.y < e.groundY) {
            p.y = e.groundY;
            p.vy = -p.vy * e.bounce;
        }
        if (e.trailLength > 0) {
            p.trail.push_back({p.x, p.y, p.z});
            if (p.trail.size() > e.trailLength) p.trail.erase(p.trail.begin());
        }
    }
    ps.erase(std::remove_if(ps.begin(), ps.end(), [](const Particle& p) { return p.age >= p.life; }), ps.end());
}

// Curve editors (282): cubic Hermite through keys with tangents.
struct CurveKey {
    float t, v, inTangent, outTangent;
};

inline float evalCurve(const std::vector<CurveKey>& keys, float t) {
    if (keys.empty()) return 0;
    if (t <= keys.front().t) return keys.front().v;
    if (t >= keys.back().t) return keys.back().v;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].t) {
            const CurveKey &a = keys[i - 1], &b = keys[i];
            const float h = b.t - a.t, s = (t - a.t) / h, s2 = s * s, s3 = s2 * s;
            return (2 * s3 - 3 * s2 + 1) * a.v + (s3 - 2 * s2 + s) * h * a.outTangent + (-2 * s3 + 3 * s2) * b.v + (s3 - s2) * h * b.inTangent;
        }
    }
    return keys.back().v;
}

// ---- Animation (301 to 309)
struct ClipInfo {
    std::string name;
    float seconds;
    std::set<std::string> tags;
};

inline std::vector<std::string> findClips(const std::vector<ClipInfo>& clips, const std::string& text, const std::string& tag) {
    std::vector<std::string> out;
    for (const auto& c : clips) {
        if ((text.empty() || c.name.find(text) != std::string::npos) && (tag.empty() || c.tags.count(tag))) out.push_back(c.name);
    }
    return out;
}

// Skeleton tree text (302): bone index with parent index (-1 for root).
inline std::vector<std::string> skeletonLines(const std::vector<std::pair<std::string, int>>& bones) {
    std::vector<int> depth(bones.size(), 0);
    std::vector<std::string> out;
    for (std::size_t i = 0; i < bones.size(); ++i) {
        const int p = bones[i].second;
        depth[i] = p >= 0 && p < static_cast<int>(i) ? depth[static_cast<std::size_t>(p)] + 1 : 0;
        out.push_back(std::string(static_cast<std::size_t>(depth[i]) * 2, ' ') + bones[i].first);
    }
    return out;
}

// Retarget (303, 304): source bone names to a standard humanoid set.
inline std::string humanoidBone(const std::string& sourceName) {
    std::string s;
    for (char c : sourceName) {
        if (std::isalnum(static_cast<unsigned char>(c))) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    static const std::vector<std::pair<std::string, std::string>> map = {{"hips", "Hips"}, {"pelvis", "Hips"}, {"spine", "Spine"}, {"head", "Head"}, {"leftupperarm", "LeftUpperArm"}, {"upperarml", "LeftUpperArm"}, {"rightupperarm", "RightUpperArm"}, {"upperarmr", "RightUpperArm"}, {"leftupperleg", "LeftUpperLeg"}, {"thighl", "LeftUpperLeg"}, {"rightupperleg", "RightUpperLeg"}, {"thighr", "RightUpperLeg"}};
    for (const auto& [k, v] : map) {
        if (s.find(k) != std::string::npos) return v;
    }
    return {};
}

inline std::map<std::string, std::string> retargetProfile(const std::vector<std::string>& sourceBones) {
    std::map<std::string, std::string> out;
    for (const auto& b : sourceBones) {
        const std::string h = humanoidBone(b);
        if (!h.empty() && !out.count(h)) out[h] = b;
    }
    return out;
}

// State machine (305).
struct Transition {
    std::string from, to, parameter;
    float threshold;
    bool greater;
};

class AnimStateMachine {
public:
    explicit AnimStateMachine(std::string start) : state_(std::move(start)) {}
    void addTransition(Transition t) { transitions_.push_back(std::move(t)); }
    const std::string& update(const std::map<std::string, float>& params) {
        for (const auto& t : transitions_) {
            if (t.from != state_) continue;
            auto it = params.find(t.parameter);
            if (it == params.end()) continue;
            if (t.greater ? it->second > t.threshold : it->second < t.threshold) {
                state_ = t.to;
                break;
            }
        }
        return state_;
    }
    [[nodiscard]] const std::string& state() const { return state_; }

private:
    std::string state_;
    std::vector<Transition> transitions_;
};

// 1D blend tree (306): the two nearest clips by parameter get weights summing to 1.
struct BlendPoint {
    float at;
    std::string clip;
};

inline std::vector<std::pair<std::string, float>> blendTree(const std::vector<BlendPoint>& pts, float value) {
    if (pts.empty()) return {};
    if (value <= pts.front().at) return {{pts.front().clip, 1.0f}};
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (value <= pts[i].at) {
            const float t = (value - pts[i - 1].at) / (pts[i].at - pts[i - 1].at);
            return {{pts[i - 1].clip, 1 - t}, {pts[i].clip, t}};
        }
    }
    return {{pts.back().clip, 1.0f}};
}

// Layers and masks (307): per-bone blend of a base pose with a layer pose.
inline std::vector<float> blendLayer(const std::vector<float>& base, const std::vector<float>& layer, const std::vector<float>& mask, float weight) {
    std::vector<float> out = base;
    for (std::size_t i = 0; i < out.size() && i < layer.size() && i < mask.size(); ++i) out[i] = base[i] + (layer[i] - base[i]) * mask[i] * weight;
    return out;
}

// Two-bone IK (309): returns the two joint angles (radians) to reach a target at `distance` with bone lengths a and b.
inline bool twoBoneIk(float a, float b, float distance, float& shoulderAngle, float& elbowAngle) {
    const float d = std::clamp(distance, std::fabs(a - b) + 1e-4f, a + b - 1e-4f);
    shoulderAngle = std::acos(std::clamp((a * a + d * d - b * b) / (2 * a * d), -1.0f, 1.0f));
    elbowAngle = std::acos(std::clamp((a * a + b * b - d * d) / (2 * a * b), -1.0f, 1.0f));
    return distance >= std::fabs(a - b) && distance <= a + b;
}

// ---- Movie timeline (327, 328, 329, 332)
inline float snapTime(float t, float fps, bool snap) { return snap && fps > 0 ? std::round(t * fps) / fps : t; }

struct CameraCut {
    float time;
    std::string camera;
};

inline std::string activeCamera(std::vector<CameraCut> cuts, float t) {
    std::sort(cuts.begin(), cuts.end(), [](const CameraCut& a, const CameraCut& b) { return a.time < b.time; });
    std::string cur;
    for (const auto& c : cuts) {
        if (c.time <= t) cur = c.camera;
    }
    return cur;
}

inline float shutterExposureFraction(float shutterAngleDegrees) { return std::clamp(shutterAngleDegrees / 360.0f, 0.0f, 1.0f); }

inline float timelineZoomPixelsPerSecond(float zoom) { return 100.0f * std::clamp(zoom, 0.05f, 50.0f); }

} // namespace engine::core::gfxanim

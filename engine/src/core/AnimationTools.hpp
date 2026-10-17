#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace engine::core::anim {

constexpr float kPi = 3.14159265f;

// --- Root motion (308): per-frame movement of the root, and the clip with that movement removed. ---
struct V3 {
    float x = 0, y = 0, z = 0;
};

inline std::vector<V3> rootDeltas(const std::vector<V3>& rootPositions) {
    std::vector<V3> d;
    for (std::size_t i = 1; i < rootPositions.size(); ++i)
        d.push_back({rootPositions[i].x - rootPositions[i - 1].x, rootPositions[i].y - rootPositions[i - 1].y, rootPositions[i].z - rootPositions[i - 1].z});
    return d;
}

// Keeps the vertical motion (jumps) but removes the horizontal travel so the character moves by physics.
inline std::vector<V3> stripRootMotion(const std::vector<V3>& rootPositions) {
    std::vector<V3> out = rootPositions;
    if (out.empty()) return out;
    for (V3& p : out) {
        p.x = rootPositions.front().x;
        p.z = rootPositions.front().z;
    }
    return out;
}

// --- Two-bone IK for legs (310): returns the knee bend and hip angle (radians) to reach `distance`. ---
struct TwoBoneIk {
    float hipAngle = 0, kneeAngle = 0;
    bool reachable = true;
};

inline TwoBoneIk solveTwoBone(float upperLen, float lowerLen, float distance) {
    TwoBoneIk r;
    const float maxReach = upperLen + lowerLen - 1e-4f, minReach = std::fabs(upperLen - lowerLen) + 1e-4f;
    if (distance > maxReach || distance < minReach) r.reachable = false;
    const float d = std::clamp(distance, minReach, maxReach);
    const float cosKnee = (upperLen * upperLen + lowerLen * lowerLen - d * d) / (2 * upperLen * lowerLen);
    r.kneeAngle = kPi - std::acos(std::clamp(cosKnee, -1.0f, 1.0f));
    const float cosHip = (upperLen * upperLen + d * d - lowerLen * lowerLen) / (2 * upperLen * d);
    r.hipAngle = std::acos(std::clamp(cosHip, -1.0f, 1.0f));
    return r;
}

// How far to lower the pelvis when the lower of two feet is below the animated height (slope correction).
inline float pelvisDrop(float leftGroundDelta, float rightGroundDelta) { return std::min(0.0f, std::min(leftGroundDelta, rightGroundDelta)); }

// --- Markers and events (312, 341, 358) ---------------------------------------------------------------
struct Marker {
    float time = 0;
    std::string name;
};

class MarkerTrack {
public:
    void add(float time, const std::string& name) {
        markers_.push_back({time, name});
        std::stable_sort(markers_.begin(), markers_.end(), [](const Marker& a, const Marker& b) { return a.time < b.time; });
    }
    // Markers passed when playback moves from `from` to `to` (to inclusive); handles looping when to < from.
    [[nodiscard]] std::vector<Marker> crossed(float from, float to, float clipLength) const {
        std::vector<Marker> out;
        auto between = [&](float a, float b, bool includeA) {
            for (const auto& m : markers_) {
                if ((includeA ? m.time >= a : m.time > a) && m.time <= b) out.push_back(m);
            }
        };
        if (to >= from) between(from, to, false);
        else {
            between(from, clipLength, false);
            between(0.0f, to, true);
        }
        return out;
    }
    [[nodiscard]] const std::vector<Marker>& markers() const { return markers_; }

private:
    std::vector<Marker> markers_;
};

// --- Curves (313, 334) ---------------------------------------------------------------------------------
inline float cubicBezier1D(float p0, float p1, float p2, float p3, float t) {
    const float u = 1 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}

enum class Interp { Step, Linear, Smooth };

struct Key {
    float time = 0, value = 0;
    Interp interp = Interp::Linear;
};

inline float evaluateTrack(const std::vector<Key>& keys, float t) {
    if (keys.empty()) return 0;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (t < keys[i].time || t > keys[i + 1].time) continue;
        const float span = keys[i + 1].time - keys[i].time;
        float f = span > 0 ? (t - keys[i].time) / span : 0;
        if (keys[i].interp == Interp::Step) return keys[i].value;
        if (keys[i].interp == Interp::Smooth) f = f * f * (3 - 2 * f);
        return keys[i].value + (keys[i + 1].value - keys[i].value) * f;
    }
    return keys.back().value;
}

// --- Mocap cleanup (315) --------------------------------------------------------------------------------
inline std::vector<float> smoothSamples(const std::vector<float>& s, int radius) {
    std::vector<float> out(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        float sum = 0;
        int n = 0;
        for (int k = -radius; k <= radius; ++k) {
            const long j = static_cast<long>(i) + k;
            if (j >= 0 && j < static_cast<long>(s.size())) {
                sum += s[j];
                ++n;
            }
        }
        out[i] = sum / static_cast<float>(n);
    }
    return out;
}

// Replaces single-frame spikes: a sample that differs from the median of itself and its neighbours by more than `threshold`.
inline std::vector<float> removeSpikes(std::vector<float> s, float threshold) {
    const std::vector<float> src = s;
    for (std::size_t i = 1; i + 1 < src.size(); ++i) {
        std::array<float, 3> w{src[i - 1], src[i], src[i + 1]};
        std::sort(w.begin(), w.end());
        if (std::fabs(src[i] - w[1]) > threshold) s[i] = w[1];
    }
    return s;
}

// --- Pose mirroring (318) and retarget validation (323) ----------------------------------------------------
inline std::string mirrorBoneName(const std::string& name) {
    auto swap = [&](const std::string& a, const std::string& b) -> std::string {
        const std::size_t p = name.find(a);
        if (p == std::string::npos) return {};
        std::string s = name;
        s.replace(p, a.size(), b);
        return s;
    };
    for (const auto& pair : std::vector<std::pair<std::string, std::string>>{{"Left", "Right"}, {"Right", "Left"}, {"_L", "_R"}, {"_R", "_L"}, {".L", ".R"}, {".R", ".L"}}) {
        const std::string s = swap(pair.first, pair.second);
        if (!s.empty()) return s;
    }
    return name;
}

// A pose is bone -> rotation (x, y, z as degrees). Mirroring across X swaps sides and negates Y and Z rotations.
using Pose = std::map<std::string, std::array<float, 3>>;

inline Pose mirrorPose(const Pose& pose) {
    Pose out;
    for (const auto& [bone, r] : pose) out[mirrorBoneName(bone)] = {r[0], -r[1], -r[2]};
    return out;
}

struct RetargetReport {
    std::vector<std::string> unmapped; // source bones with no target
    std::vector<std::string> unused;   // target bones nothing drives
};

inline RetargetReport validateRetarget(const std::vector<std::string>& source, const std::vector<std::string>& target) {
    RetargetReport r;
    for (const auto& s : source) {
        if (std::find(target.begin(), target.end(), s) == target.end()) r.unmapped.push_back(s);
    }
    for (const auto& t : target) {
        if (std::find(source.begin(), source.end(), t) == source.end()) r.unused.push_back(t);
    }
    return r;
}

// --- Blending and LOD (320, 322) ----------------------------------------------------------------------------
// Ragdoll to animation: weight of the animation rises from 0 to 1 over `seconds` after recovery starts.
inline float recoverBlend(float secondsSinceRecovery, float seconds) { return seconds <= 0 ? 1.0f : std::clamp(secondsSinceRecovery / seconds, 0.0f, 1.0f); }

// How often to update a character's animation: every frame near, every Nth frame far.
inline int animationUpdateInterval(float distance, float nearDist = 15.0f, float farDist = 60.0f) {
    if (distance <= nearDist) return 1;
    if (distance <= farDist) return 2;
    return 4;
}

// --- Camera (330, 331, 338, 340) -----------------------------------------------------------------------------
inline float fovFromFocalLength(float focalMm, float sensorHeightMm = 24.0f) { return 2.0f * std::atan(sensorHeightMm / (2.0f * focalMm)) * 180.0f / kPi; }
inline float focalLengthFromFov(float fovDegrees, float sensorHeightMm = 24.0f) { return sensorHeightMm / (2.0f * std::tan(fovDegrees * kPi / 360.0f)); }

// Circle of confusion diameter (mm on the sensor) for an object at `distance` when focused at `focus` (metres).
inline float circleOfConfusion(float focalMm, float fNumber, float focus, float distance) {
    const float f = focalMm / 1000.0f;
    if (focus <= f || distance <= 0) return 0;
    const float c = (f * f / fNumber) * std::fabs(distance - focus) / (distance * (focus - f));
    return c * 1000.0f;
}

struct CameraPose {
    V3 position;
    float yawRadians = 0;
};

inline CameraPose dolly(V3 start, V3 end, float t) { return {{start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t, start.z + (end.z - start.z) * t}, 0}; }

inline CameraPose orbit(V3 target, float radius, float height, float turns, float t) {
    const float a = 2 * kPi * turns * t;
    return {{target.x + std::sin(a) * radius, target.y + height, target.z + std::cos(a) * radius}, a + kPi};
}

inline CameraPose crane(V3 base, float armLength, float startAngleDeg, float endAngleDeg, float t) {
    const float a = (startAngleDeg + (endAngleDeg - startAngleDeg) * t) * kPi / 180.0f;
    return {{base.x, base.y + std::sin(a) * armLength, base.z + std::cos(a) * armLength}, 0};
}

inline float rackFocus(float fromDistance, float toDistance, float t) {
    const float f = t * t * (3 - 2 * t);
    return fromDistance + (toDistance - fromDistance) * f;
}

// --- Time of day (264, 333) ---------------------------------------------------------------------------------
// Hour 0..24 -> sun elevation in degrees (-90 at midnight, +90 at noon).
inline float sunElevationDegrees(float hour) { return std::sin((hour - 6.0f) / 24.0f * 2.0f * kPi) * 90.0f; }

struct SkyPreset {
    const char* name;
    float hour, turbidity, fogDensity;
};

inline std::array<SkyPreset, 4> skyPresets() { return {{{"Clear morning", 8, 2.0f, 0.002f}, {"Noon", 12, 2.5f, 0.001f}, {"Golden hour", 18, 4.0f, 0.004f}, {"Overcast", 14, 8.0f, 0.01f}}}; }

// --- Light linking and budgets (268, 272) ---------------------------------------------------------------------
inline bool lightAffects(uint32_t lightMask, uint32_t objectMask) { return (lightMask & objectMask) != 0; }

struct LightBudget {
    int maxShadowLights = 4, maxLights = 64;
};

inline std::vector<int> pickShadowCasters(const std::vector<float>& lightImportance, const LightBudget& b) {
    std::vector<int> idx(lightImportance.size());
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<int>(i);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int c) { return lightImportance[a] > lightImportance[c]; });
    if (static_cast<int>(idx.size()) > b.maxShadowLights) idx.resize(b.maxShadowLights);
    return idx;
}

// Mean squared error and PSNR between two 8-bit images (lighting regression comparison, 275).
inline double imageMse(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size() || a.empty()) return -1.0;
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) sum += (static_cast<double>(a[i]) - b[i]) * (static_cast<double>(a[i]) - b[i]);
    return sum / static_cast<double>(a.size());
}

inline double imagePsnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    const double mse = imageMse(a, b);
    if (mse < 0) return -1.0;
    return mse == 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// --- Sequencer (337, 342, 344, 348, 350) ----------------------------------------------------------------------
struct Shot {
    std::string name;
    float seconds = 0;
    std::string camera, notes;
};

inline std::string shotListMarkdown(const std::vector<Shot>& shots) {
    std::ostringstream out;
    out << "| # | Shot | Seconds | Camera | Notes |\n|---|---|---|---|---|\n";
    float total = 0;
    for (std::size_t i = 0; i < shots.size(); ++i) {
        out << "| " << i + 1 << " | " << shots[i].name << " | " << shots[i].seconds << " | " << shots[i].camera << " | " << shots[i].notes << " |\n";
        total += shots[i].seconds;
    }
    out << "\nTotal: " << total << " s\n";
    return out.str();
}

struct Sequence {
    std::string name;
    float length = 0;
    struct Child {
        std::string sequence;
        float startAt = 0;
    };
    std::vector<Child> children;
};

// Flattens nested sequences into (name, absolute start); stops at `maxDepth` so a loop can't run forever.
inline void flattenSequences(const std::map<std::string, Sequence>& all, const std::string& root, float offset, int maxDepth,
                             std::vector<std::pair<std::string, float>>& out) {
    out.push_back({root, offset});
    auto it = all.find(root);
    if (it == all.end() || maxDepth <= 0) return;
    for (const auto& c : it->second.children) flattenSequences(all, c.sequence, offset + c.startAt, maxDepth - 1, out);
}

inline std::string frameFileName(const std::string& base, int frame, int digits, const std::string& ext) {
    std::string n = std::to_string(frame);
    if (static_cast<int>(n.size()) < digits) n = std::string(digits - n.size(), '0') + n;
    return base + "_" + n + "." + ext;
}

// Exact frame <-> time using integer ratios, so frame 90 at 30 fps is 3 s with no rounding drift.
inline long frameAtSeconds(double seconds, int fps) { return static_cast<long>(std::floor(seconds * fps + 1e-6)); }
inline double secondsAtFrame(long frame, int fps) { return static_cast<double>(frame) / fps; }

inline std::string timecode(long frame, int fps) {
    const long total = frame / fps, f = frame % fps;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60, f);
    return buf;
}

// A render queue that can be saved and resumed: jobs have a state and the next frame to render.
struct RenderJob {
    std::string name;
    long frames = 0, nextFrame = 0;
    [[nodiscard]] bool done() const { return nextFrame >= frames; }
};

inline std::string serializeQueue(const std::vector<RenderJob>& q) {
    std::ostringstream out;
    for (const auto& j : q) out << j.name << ' ' << j.frames << ' ' << j.nextFrame << '\n';
    return out.str();
}

inline std::vector<RenderJob> parseQueue(const std::string& text) {
    std::vector<RenderJob> q;
    std::istringstream in(text);
    RenderJob j;
    while (in >> j.name >> j.frames >> j.nextFrame) q.push_back(j);
    return q;
}

inline const RenderJob* nextJob(const std::vector<RenderJob>& q) {
    for (const auto& j : q) {
        if (!j.done()) return &j;
    }
    return nullptr;
}

} // namespace engine::core::anim

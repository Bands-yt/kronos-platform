#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace engine::core::viewport {

struct V3 {
    float x = 0, y = 0, z = 0;
};

// Snapping (85).
inline float snapValue(float v, float step) { return step <= 0.0f ? v : std::round(v / step) * step; }
inline float snapAngleDegrees(float degrees, float step) { return snapValue(degrees, step); }

// Ruler (93).
inline float distance(V3 a, V3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
inline std::string rulerLabel(V3 a, V3 b) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f studs", distance(a, b));
    return buf;
}

// Orthographic and isometric view directions (81): camera looks along `forward`, `up` is the screen's up.
struct ViewPreset {
    const char* name;
    V3 forward;
    V3 up;
    bool orthographic;
};

inline const std::vector<ViewPreset>& viewPresets() {
    static const std::vector<ViewPreset> list = {
        {"Top", {0, -1, 0}, {0, 0, -1}, true},   {"Front", {0, 0, -1}, {0, 1, 0}, true},
        {"Right", {-1, 0, 0}, {0, 1, 0}, true},  {"Left", {1, 0, 0}, {0, 1, 0}, true},
        {"Back", {0, 0, 1}, {0, 1, 0}, true},    {"Bottom", {0, 1, 0}, {0, 0, 1}, true},
        {"Isometric", {-0.577f, -0.577f, -0.577f}, {0, 1, 0}, true},
    };
    return list;
}

// Named camera bookmarks (83) saved as text lines "name x y z yaw pitch".
struct CameraBookmark {
    std::string name;
    V3 position;
    float yaw = 0, pitch = 0;
};

inline std::string serializeBookmarks(const std::vector<CameraBookmark>& marks) {
    std::ostringstream out;
    for (const auto& m : marks) {
        std::string name = m.name;
        std::replace(name.begin(), name.end(), ' ', '_');
        out << name << ' ' << m.position.x << ' ' << m.position.y << ' ' << m.position.z << ' ' << m.yaw << ' ' << m.pitch << '\n';
    }
    return out.str();
}

inline std::vector<CameraBookmark> parseBookmarks(const std::string& text) {
    std::vector<CameraBookmark> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        CameraBookmark m;
        if (row >> m.name >> m.position.x >> m.position.y >> m.position.z >> m.yaw >> m.pitch) {
            std::replace(m.name.begin(), m.name.end(), '_', ' ');
            out.push_back(m);
        }
    }
    return out;
}

// Selection (87): is a screen point inside a lasso polygon / a box?
struct P2 {
    float x = 0, y = 0;
};

inline bool pointInPolygon(P2 p, const std::vector<P2>& poly) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const bool crosses = (poly[i].y > p.y) != (poly[j].y > p.y);
        if (crosses && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) inside = !inside;
    }
    return inside;
}

inline bool pointInBox(P2 p, P2 a, P2 b) {
    return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

// Occlusion-aware picking (88): of the hits along a ray, return the nearest one that is visible (not hidden).
struct RayHit {
    int id = -1;
    float distance = 0;
    bool hiddenInEditor = false;
};

inline int pickNearestVisible(std::vector<RayHit> hits) {
    std::sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b) { return a.distance < b.distance; });
    for (const auto& h : hits) {
        if (!h.hiddenInEditor) return h.id;
    }
    return -1;
}

// Gizmo space toggle (90): converts a movement from the gizmo's local axes to world (yaw only) or leaves it.
inline V3 gizmoDeltaToWorld(V3 delta, float yawRadians, bool localSpace) {
    if (!localSpace) return delta;
    const float c = std::cos(yawRadians), s = std::sin(yawRadians);
    return {delta.x * c + delta.z * s, delta.y, -delta.x * s + delta.z * c};
}

// Gizmo hit target (98): minimum clickable size in pixels, bigger for accessibility.
inline float gizmoHitRadius(float drawnRadiusPx, bool largeTargets) { return std::max(drawnRadiusPx, largeTargets ? 18.0f : 10.0f); }

// Clip planes (96, 97).
struct ClipPlanes {
    float nearPlane = 0.1f, farPlane = 1000.0f;
};

inline ClipPlanes clampClipPlanes(ClipPlanes c) {
    c.nearPlane = std::clamp(c.nearPlane, 0.01f, 10.0f);
    c.farPlane = std::max(c.farPlane, c.nearPlane * 10.0f);
    // Depth precision gets bad past a 100000:1 ratio; pull the near plane out instead of shrinking the far plane.
    if (c.farPlane / c.nearPlane > 100000.0f) c.nearPlane = c.farPlane / 100000.0f;
    return c;
}

inline std::vector<int> nearClipped(const std::vector<float>& objectDistances, float nearPlane, float objectRadius) {
    std::vector<int> out;
    for (std::size_t i = 0; i < objectDistances.size(); ++i) {
        if (objectDistances[i] - objectRadius < nearPlane) out.push_back(static_cast<int>(i));
    }
    return out;
}

// Safe-area insets (909): screen rectangle left after notches/cutouts, as fractions.
struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
};

inline void applySafeArea(float screenW, float screenH, Insets in, float& x, float& y, float& w, float& h) {
    x = in.left;
    y = in.top;
    w = std::max(0.0f, screenW - in.left - in.right);
    h = std::max(0.0f, screenH - in.top - in.bottom);
}

} // namespace engine::core::viewport

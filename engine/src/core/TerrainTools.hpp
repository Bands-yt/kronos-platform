#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::core::terrain {

struct Heightfield {
    int width = 0, depth = 0;
    std::vector<float> h; // width * depth, row-major
    [[nodiscard]] float at(int x, int z) const { return h[static_cast<std::size_t>(z) * width + x]; }
    float& at(int x, int z) { return h[static_cast<std::size_t>(z) * width + x]; }
    [[nodiscard]] bool valid() const { return width >= 2 && depth >= 2 && h.size() == static_cast<std::size_t>(width) * depth; }
};

inline Heightfield makeFlat(int width, int depth, float height = 0.0f) {
    return {width, depth, std::vector<float>(static_cast<std::size_t>(width) * depth, height)};
}

// Digs a smooth round hole (negative depth) or bump; used for runtime carving (139) and stamps (135).
inline void carve(Heightfield& t, float cx, float cz, float radius, float amount) {
    for (int z = 0; z < t.depth; ++z) {
        for (int x = 0; x < t.width; ++x) {
            const float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz));
            if (d >= radius) continue;
            const float f = 1.0f - d / radius;
            t.at(x, z) += amount * f * f * (3.0f - 2.0f * f);
        }
    }
}

// Reusable brush stamp (135): a small heightfield added at a position, scaled.
inline void stamp(Heightfield& t, const Heightfield& brush, int ox, int oz, float scale) {
    for (int z = 0; z < brush.depth; ++z) {
        for (int x = 0; x < brush.width; ++x) {
            const int tx = ox + x, tz = oz + z;
            if (tx < 0 || tz < 0 || tx >= t.width || tz >= t.depth) continue;
            t.at(tx, tz) += brush.at(x, z) * scale;
        }
    }
}

// Collision mesh: two triangles per cell. Vertices are x, height, z with the given cell size.
struct TriMesh {
    std::vector<float> vertices; // xyz triples
    std::vector<uint32_t> indices;
};

inline TriMesh collisionMesh(const Heightfield& t, float cellSize) {
    TriMesh m;
    if (!t.valid()) return m;
    for (int z = 0; z < t.depth; ++z) {
        for (int x = 0; x < t.width; ++x) m.vertices.insert(m.vertices.end(), {x * cellSize, t.at(x, z), z * cellSize});
    }
    for (int z = 0; z + 1 < t.depth; ++z) {
        for (int x = 0; x + 1 < t.width; ++x) {
            const uint32_t i = static_cast<uint32_t>(z * t.width + x);
            m.indices.insert(m.indices.end(), {i, i + static_cast<uint32_t>(t.width), i + 1, i + 1, i + static_cast<uint32_t>(t.width), i + static_cast<uint32_t>(t.width) + 1});
        }
    }
    return m;
}

// Heightmap export (147): 16-bit little-endian samples scaled to [minH, maxH].
inline std::vector<uint8_t> exportHeightmap16(const Heightfield& t, float minH, float maxH) {
    std::vector<uint8_t> out;
    const float span = std::max(maxH - minH, 1e-6f);
    for (float v : t.h) {
        const float n = std::clamp((v - minH) / span, 0.0f, 1.0f);
        const uint16_t s = static_cast<uint16_t>(std::lround(n * 65535.0f));
        out.push_back(static_cast<uint8_t>(s & 0xFF));
        out.push_back(static_cast<uint8_t>(s >> 8));
    }
    return out;
}

inline Heightfield importHeightmap16(const std::vector<uint8_t>& bytes, int width, int depth, float minH, float maxH) {
    Heightfield t = makeFlat(width, depth, minH);
    for (std::size_t i = 0; i < t.h.size() && i * 2 + 1 < bytes.size(); ++i) {
        const uint16_t s = static_cast<uint16_t>(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
        t.h[i] = minH + (maxH - minH) * static_cast<float>(s) / 65535.0f;
    }
    return t;
}

// Stitching (149): makes the shared edge of two neighbouring chunks identical (average of both).
// `a`'s right column (x = width-1) meets `b`'s left column (x = 0).
inline void stitchRightLeft(Heightfield& a, Heightfield& b) {
    const int rows = std::min(a.depth, b.depth);
    for (int z = 0; z < rows; ++z) {
        const float avg = (a.at(a.width - 1, z) + b.at(0, z)) * 0.5f;
        a.at(a.width - 1, z) = b.at(0, z) = avg;
    }
}

// Streaming diagnostics (144).
struct ChunkStats {
    int loaded = 0, loading = 0, unloaded = 0;
};

inline std::string streamingSummary(const ChunkStats& s) {
    return std::to_string(s.loaded) + " loaded, " + std::to_string(s.loading) + " loading, " + std::to_string(s.unloaded) +
           " unloaded" + (s.loading > 8 ? " (many chunks loading at once: expect hitches)" : "");
}

// Spline roads and rivers (134): Catmull-Rom through control points (x, z); returns left/right edge points.
struct P2 {
    float x = 0, z = 0;
    bool operator==(const P2& o) const { return x == o.x && z == o.z; }
};

inline P2 catmullRom(P2 p0, P2 p1, P2 p2, P2 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    auto f = [&](float a, float b, float c, float d) {
        return 0.5f * ((2 * b) + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
    };
    return {f(p0.x, p1.x, p2.x, p3.x), f(p0.z, p1.z, p2.z, p3.z)};
}

inline std::vector<P2> sampleSpline(const std::vector<P2>& pts, int perSegment) {
    std::vector<P2> out;
    if (pts.size() < 2 || perSegment < 1) return out;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const P2 p0 = pts[i == 0 ? 0 : i - 1], p1 = pts[i], p2 = pts[i + 1], p3 = pts[std::min(i + 2, pts.size() - 1)];
        for (int s = 0; s < perSegment; ++s) out.push_back(catmullRom(p0, p1, p2, p3, static_cast<float>(s) / static_cast<float>(perSegment)));
    }
    out.push_back(pts.back());
    return out;
}

inline std::vector<std::pair<P2, P2>> roadEdges(const std::vector<P2>& centre, float halfWidth) {
    std::vector<std::pair<P2, P2>> out;
    for (std::size_t i = 0; i < centre.size(); ++i) {
        const P2 a = centre[i == 0 ? 0 : i - 1], b = centre[std::min(i + 1, centre.size() - 1)];
        float dx = b.x - a.x, dz = b.z - a.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1e-9f) {
            dx = 1;
            dz = 0;
        } else {
            dx /= len;
            dz /= len;
        }
        out.push_back({{centre[i].x - dz * halfWidth, centre[i].z + dx * halfWidth}, {centre[i].x + dz * halfWidth, centre[i].z - dx * halfWidth}});
    }
    return out;
}

// Scatter (137, 138): jittered grid points over an area, kept only where slope and height allow.
struct ScatterRule {
    float minHeight = -1e9f, maxHeight = 1e9f, maxSlopeDegrees = 90.0f;
    float spacing = 2.0f;
    float density = 1.0f; // 0..1 chance a candidate is kept
};

inline float slopeDegreesAt(const Heightfield& t, int x, int z, float cellSize) {
    const int x0 = std::max(x - 1, 0), x1 = std::min(x + 1, t.width - 1), z0 = std::max(z - 1, 0), z1 = std::min(z + 1, t.depth - 1);
    const float dx = (t.at(x1, z) - t.at(x0, z)) / (std::max(x1 - x0, 1) * cellSize);
    const float dz = (t.at(x, z1) - t.at(x, z0)) / (std::max(z1 - z0, 1) * cellSize);
    return std::atan(std::sqrt(dx * dx + dz * dz)) * 57.29578f;
}

inline uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline std::vector<P2> scatter(const Heightfield& t, float cellSize, const ScatterRule& rule, uint32_t seed) {
    std::vector<P2> out;
    if (!t.valid() || rule.spacing <= 0) return out;
    const float width = (t.width - 1) * cellSize, depth = (t.depth - 1) * cellSize;
    for (float z = 0; z < depth; z += rule.spacing) {
        for (float x = 0; x < width; x += rule.spacing) {
            const uint32_t h = hash2(static_cast<uint32_t>(x / rule.spacing), static_cast<uint32_t>(z / rule.spacing), seed);
            const float jx = (h & 0xFFFF) / 65535.0f * rule.spacing * 0.8f, jz = ((h >> 16) & 0xFFFF) / 65535.0f * rule.spacing * 0.8f;
            const float px = std::min(x + jx, width), pz = std::min(z + jz, depth);
            const int gx = std::min(static_cast<int>(px / cellSize), t.width - 1), gz = std::min(static_cast<int>(pz / cellSize), t.depth - 1);
            const float height = t.at(gx, gz);
            if (height < rule.minHeight || height > rule.maxHeight) continue;
            if (slopeDegreesAt(t, gx, gz, cellSize) > rule.maxSlopeDegrees) continue;
            if (((h >> 8) & 0xFF) / 255.0f > rule.density) continue;
            out.push_back({px, pz});
        }
    }
    return out;
}

} // namespace engine::core::terrain

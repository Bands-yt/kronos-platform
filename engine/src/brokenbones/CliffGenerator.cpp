#include "brokenbones/CliffGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "brokenbones/RockMesh.hpp"

namespace engine::brokenbones {

namespace {

constexpr float kCliffWidth = 110.0f;
constexpr float kFaceStep = 1.0f;
constexpr float kRockBoundScale = 1.41f; // max rock-mesh displacement, see generateRockMesh()
constexpr float kObstacleGap = 0.5f;
constexpr float kBeamClearance = 0.35f;

float smooth01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float noise01(glm::vec3 p, uint32_t seed, int octaves) { return 0.5f + 0.5f * rockNoise3D(p, seed, octaves); }

struct RockPalette {
    const char* name;
    glm::vec3 tint;
    glm::vec3 light;
    glm::vec3 dark;
    glm::vec3 shelf;
};

constexpr MapThemeInfo kThemes[kMapThemeCount] = {
    {"COAST", 1.0f, 1.0f, "SPLASHDOWN", {0.78f, 0.86f, 0.96f}, {0.18f, 0.40f, 0.86f}, {0.84f, 0.90f, 0.97f},
     {1.0f, 1.0f, 1.0f}, {0.015f, 0.075f, 0.085f}, {0.0f, 0.0f, 0.0f}},
    {"GLACIER", 1.25f, 1.0f, "ICE BATH", {0.86f, 0.92f, 0.98f}, {0.30f, 0.52f, 0.90f}, {0.92f, 0.95f, 1.0f},
     {1.6f, 1.7f, 1.85f}, {0.03f, 0.12f, 0.16f}, {0.0f, 0.0f, 0.0f}},
    {"CANYON", 1.5f, 1.8f, "RIVER DIVE", {0.92f, 0.78f, 0.62f}, {0.25f, 0.45f, 0.80f}, {0.98f, 0.86f, 0.70f},
     {1.25f, 0.85f, 0.6f}, {0.05f, 0.08f, 0.05f}, {0.0f, 0.0f, 0.0f}},
    {"VOLCANO", 2.0f, 1.4f, "LAVA DIP", {0.58f, 0.40f, 0.32f}, {0.24f, 0.14f, 0.13f}, {0.58f, 0.40f, 0.32f},
     {0.35f, 0.3f, 0.28f}, {0.35f, 0.08f, 0.01f}, {1.0f, 0.35f, 0.05f}},
};

constexpr RockPalette kGlacierPalette = {"glacier ice", {0.82f, 0.9f, 1.0f}, {0.95f, 0.98f, 1.0f}, {0.55f, 0.68f, 0.82f},
                                         {1.0f, 1.0f, 1.0f}};
constexpr RockPalette kCanyonPalette = {"canyon sandstone", {1.0f, 0.62f, 0.42f}, {1.0f, 0.78f, 0.6f},
                                        {0.68f, 0.34f, 0.22f}, {0.92f, 0.74f, 0.5f}};
constexpr RockPalette kVolcanoPalette = {"volcanic basalt", {0.32f, 0.3f, 0.3f}, {0.55f, 0.5f, 0.48f},
                                         {0.16f, 0.14f, 0.14f}, {0.42f, 0.36f, 0.32f}};

constexpr RockPalette kPalettes[] = {
    {"granite", {0.92f, 0.92f, 0.95f}, {1.0f, 0.98f, 0.95f}, {0.62f, 0.60f, 0.62f}, {0.80f, 0.76f, 0.58f}},
    {"sandstone", {1.0f, 0.80f, 0.62f}, {1.0f, 0.93f, 0.82f}, {0.76f, 0.56f, 0.44f}, {0.88f, 0.82f, 0.58f}},
    {"basalt", {0.58f, 0.58f, 0.63f}, {0.92f, 0.92f, 0.95f}, {0.48f, 0.48f, 0.52f}, {0.66f, 0.68f, 0.48f}},
    {"limestone", {1.0f, 0.98f, 0.90f}, {1.0f, 1.0f, 0.97f}, {0.80f, 0.78f, 0.72f}, {0.82f, 0.84f, 0.62f}},
    {"red rock", {0.92f, 0.58f, 0.44f}, {1.0f, 0.90f, 0.84f}, {0.70f, 0.50f, 0.44f}, {0.86f, 0.78f, 0.56f}},
};

glm::vec3 closestOnSegment(glm::vec3 a, glm::vec3 b, glm::vec3 p) {
    glm::vec3 ab = b - a;
    float lengthSq = glm::dot(ab, ab);
    float t = lengthSq > 1e-8f ? std::clamp(glm::dot(p - a, ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
    return a + ab * t;
}

float segmentDistance(glm::vec3 p1, glm::vec3 q1, glm::vec3 p2, glm::vec3 q2) {
    glm::vec3 d1 = q1 - p1;
    glm::vec3 d2 = q2 - p2;
    glm::vec3 r = p1 - p2;
    float a = glm::dot(d1, d1);
    float e = glm::dot(d2, d2);
    float f = glm::dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    if (a <= 1e-8f && e <= 1e-8f) return glm::length(r);
    if (a <= 1e-8f) {
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        float c = glm::dot(d1, r);
        if (e <= 1e-8f) {
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            float b = glm::dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > 1e-8f ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    return glm::length((p1 + d1 * s) - (p2 + d2 * t));
}

// Zones obstacles must stay out of: the lagoon (so splashdowns are fair)
// and the walkway from spawn to the diving board.
bool blocksLagoon(const CliffLayout& layout, const CliffCollider& c) {
    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        glm::vec3 p = glm::mix(c.a, c.b, t);
        bool overWater = p.x > layout.lagoonMin.x - c.radius - 1.0f && p.x < layout.lagoonMax.x + c.radius + 1.0f &&
                         p.z > layout.lagoonMin.y - c.radius - 1.0f && p.z < layout.lagoonMax.y + c.radius + 1.0f;
        if (overWater && p.y - c.radius < 6.0f) return true;
    }
    return false;
}

CliffCollider walkwayCollider(const CliffLayout& layout) {
    float x = layout.divingBoard.center.x;
    float y = layout.height + 1.0f;
    float tip = layout.divingBoard.center.z + layout.divingBoard.halfExtents.z;
    return {glm::vec3(x, y, layout.spawnPoint.z - 4.0f), glm::vec3(x, y, tip + 2.0f), 4.0f};
}

bool beamBuried(const CliffLayout& layout, const CliffBeam& beam) {
    glm::vec3 dir = beam.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
    float length = 2.0f * beam.halfExtents.z;
    glm::vec3 base = beam.position - dir * beam.halfExtents.z;
    for (float s = 1.2f; s <= length; s += 0.5f) {
        glm::vec3 p = base + dir * s;
        if (p.z - kBeamClearance <= layout.faceZ(p.x, p.y)) return true;
    }
    return false;
}

} // namespace

const MapThemeInfo& mapThemeInfo(MapTheme theme) { return kThemes[std::min(static_cast<size_t>(theme), kMapThemeCount - 1)]; }

float cliffHeightForLevel(int level) {
    float n = static_cast<float>(std::max(level, 1) - 1);
    return std::min(80.0f + 70.0f * n + 10.0f * n * std::max(n - 1.0f, 0.0f), kMaxCliffHeight);
}

CliffCollider boulderCollider(const CliffBoulder& boulder) {
    glm::vec3 s = boulder.stretch * (boulder.radius * kRockBoundScale);
    float across = std::max(s.x, s.z);
    CliffCollider c;
    c.a = c.b = boulder.position;
    if (s.y > across) {
        glm::vec3 axis = boulder.rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        c.a = boulder.position - axis * (s.y - across);
        c.b = boulder.position + axis * (s.y - across);
        c.radius = across;
    } else {
        c.radius = across;
    }
    return c;
}

CliffCollider beamCollider(const CliffBeam& beam) {
    glm::vec3 half = beam.rotation * glm::vec3(0.0f, 0.0f, beam.halfExtents.z);
    return {beam.position - half, beam.position + half, glm::length(glm::vec2(beam.halfExtents.x, beam.halfExtents.y))};
}

float colliderGap(const CliffCollider& a, const CliffCollider& b) {
    return segmentDistance(a.a, a.b, b.a, b.b) - a.radius - b.radius;
}

float CliffLayout::faceZ(float x, float y) const {
    float topFade = smooth01((height - y) / 6.0f);
    float z = lean * (height - y);
    for (const CliffLedge& ledge : ledges) {
        float lateral = smooth01((ledge.halfWidth - std::abs(x - ledge.xCenter)) / 4.0f);
        if (lateral <= 0.0f) continue;
        float lipY = ledge.y + 2.5f * rockNoise3D(glm::vec3(x * 0.04f, ledge.y * 0.1f, 0.0f), seed + 17u, 2);
        float depth = ledge.depth * (0.55f + 0.9f * noise01(glm::vec3(x * 0.07f, ledge.y * 0.13f, 3.1f), seed + 29u, 2));
        z += lateral * depth * smooth01((lipY - y) / 1.5f);
    }
    if (y < screeTop) z += (screeTop - y) * screeSlope;

    // The sides curve out into a cove; every large-scale shape fades out near
    // the top so the lip stays flush with the plateau.
    float edge = std::max(0.0f, std::abs(x) - coveHalfWidth);
    z += edge * edge * coveCurve * topFade;

    for (const CliffFeature& feature : features) {
        float lateral = smooth01((feature.halfWidth - std::abs(x - feature.x)) / (0.6f * feature.halfWidth));
        float vertical = smooth01((y - feature.yMin) / 4.0f) * smooth01((feature.yMax - y) / 4.0f);
        z += feature.amount * lateral * vertical * topFade;
    }
    if (overhangDepth > 0.0f) {
        z -= overhangDepth * smooth01(1.0f - std::abs(y - overhangY) / overhangHalfHeight) * topFade;
    }

    float detailFade = smooth01((height - y) / 3.0f);
    float crag = noise01(glm::vec3(x * cragFrequency, y * cragFrequency, 0.5f), seed, 4);
    float grit = noise01(glm::vec3(x * 0.4f, y * 0.4f, 1.7f), seed + 5u, 3);
    z += detailFade * (cragAmplitude * crag * crag + 0.4f * grit);
    return std::max(z, -6.0f);
}

CliffLayout generateCliffLayout(uint32_t seed, int level, MapOptions options) {
    CliffLayout layout;
    layout.seed = seed;
    layout.level = std::max(level, 1);
    layout.theme = options.theme;
    layout.height = cliffHeightForLevel(layout.level) * std::max(options.heightScale, 1.0f);
    layout.width = kCliffWidth;
    const float H = layout.height;
    const float levelF = static_cast<float>(layout.level);

    std::mt19937 rng(seed * 2654435761u + static_cast<uint32_t>(layout.level));
    auto uniform = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    auto chance = [&](float p) { return uniform(0.0f, 1.0f) < p; };
    auto count = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    // --- Overall character ------------------------------------------------------
    const RockPalette& randomPalette = kPalettes[count(0, static_cast<int>(std::size(kPalettes)) - 1)];
    const RockPalette& palette = options.theme == MapTheme::Glacier  ? kGlacierPalette
                                 : options.theme == MapTheme::Canyon  ? kCanyonPalette
                                 : options.theme == MapTheme::Volcano ? kVolcanoPalette
                                                                      : randomPalette;
    const float density = mapThemeInfo(options.theme).obstacleDensity;
    auto dense = [&](int n) { return static_cast<int>(std::round(static_cast<float>(n) * density)); };
    layout.rockName = palette.name;
    layout.rockTint = glm::clamp(palette.tint * uniform(0.92f, 1.06f), glm::vec3(0.0f), glm::vec3(1.0f));
    // Taller cliffs stay sheer so the extra height is a real drop, not a longer staircase.
    const float tallness = std::max(1.0f, H / 200.0f);
    layout.lean = uniform(0.02f, 0.16f) / tallness;
    layout.screeTop = std::clamp(uniform(0.10f, 0.22f) * H, 7.0f, 50.0f);
    layout.screeSlope = uniform(1.0f, 1.8f);
    layout.coveHalfWidth = uniform(16.0f, 28.0f);
    layout.coveCurve = uniform(0.04f, 0.09f);
    layout.cragAmplitude = uniform(1.2f, 3.2f);
    layout.cragFrequency = uniform(0.05f, 0.12f);

    for (float y = H - uniform(8.0f, 18.0f); y > layout.screeTop + 8.0f; y -= uniform(10.0f, 30.0f) * tallness) {
        CliffLedge ledge;
        ledge.y = y;
        ledge.depth = uniform(2.0f, 7.0f) + 0.15f * std::min(levelF, 8.0f);
        if (chance(0.35f)) {
            ledge.xCenter = uniform(-25.0f, 25.0f);
            ledge.halfWidth = uniform(10.0f, 25.0f);
        }
        layout.ledges.push_back(ledge);
    }

    auto addFeature = [&](float amountLo, float amountHi, float widthLo, float widthHi) {
        CliffFeature feature;
        feature.x = uniform(-22.0f, 22.0f);
        feature.halfWidth = uniform(widthLo, widthHi);
        feature.amount = uniform(amountLo, amountHi);
        feature.yMin = uniform(layout.screeTop, std::max(layout.screeTop + 1.0f, 0.5f * H));
        feature.yMax = uniform(feature.yMin + 15.0f, std::max(feature.yMin + 16.0f, H - 8.0f));
        if (feature.yMax <= H - 8.0f) layout.features.push_back(feature);
    };
    for (int g = count(0, 2); g > 0; --g) addFeature(-5.0f, -2.0f, 3.0f, 7.0f);
    for (int b = count(0, 2); b > 0; --b) addFeature(2.0f, 5.0f, 5.0f, 12.0f);
    if (H > 90.0f && chance(options.theme == MapTheme::Glacier ? 0.9f : 0.35f)) {
        layout.overhangY = uniform(layout.screeTop + 15.0f, H - 25.0f);
        layout.overhangHalfHeight = uniform(4.0f, 9.0f);
        layout.overhangDepth = uniform(2.0f, 5.0f);
    }

    // --- Face mesh -------------------------------------------------------------
    const int nx = static_cast<int>(kCliffWidth / kFaceStep) + 1;
    // Rows stay at 1 m up to kMaxCliffHeight, then stretch so altitude upgrades don't balloon the mesh.
    const float rowStep = std::max(kFaceStep, H / kMaxCliffHeight);
    const int ny = static_cast<int>(std::ceil(H / rowStep)) + 1;
    layout.faceVertices.resize(static_cast<size_t>(nx) * ny);
    for (int j = 0; j < ny; ++j) {
        float y = std::min(static_cast<float>(j) * rowStep, H);
        for (int i = 0; i < nx; ++i) {
            float x = -0.5f * kCliffWidth + static_cast<float>(i) * kFaceStep;
            core::Vertex& v = layout.faceVertices[static_cast<size_t>(j) * nx + i];
            v.position = glm::vec3(x, y, layout.faceZ(x, y));
            v.uv = glm::vec2(x, y) * 0.12f;
        }
    }
    auto faceAt = [&](int i, int j) -> const glm::vec3& {
        i = std::clamp(i, 0, nx - 1);
        j = std::clamp(j, 0, ny - 1);
        return layout.faceVertices[static_cast<size_t>(j) * nx + i].position;
    };
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            core::Vertex& v = layout.faceVertices[static_cast<size_t>(j) * nx + i];
            glm::vec3 dx = faceAt(i + 1, j) - faceAt(i - 1, j);
            glm::vec3 dy = faceAt(i, j + 1) - faceAt(i, j - 1);
            v.normal = glm::normalize(glm::cross(dx, dy));
            float x = v.position.x;
            float y = v.position.y;

            float strata = 0.5f + 0.5f * std::sin(y * 0.9f + 3.0f * rockNoise3D(glm::vec3(x * 0.03f, y * 0.02f, 7.0f), seed, 2));
            glm::vec3 tint = glm::mix(palette.light, palette.dark, strata * 0.45f);
            float upness = smooth01((v.normal.y - 0.45f) / 0.35f);
            v.color = glm::vec4(glm::mix(tint, palette.shelf, upness * 0.6f), 1.0f);
        }
    }
    layout.faceIndices.reserve(static_cast<size_t>(nx - 1) * (ny - 1) * 6);
    for (int j = 0; j + 1 < ny; ++j) {
        for (int i = 0; i + 1 < nx; ++i) {
            uint32_t a = static_cast<uint32_t>(j * nx + i);
            uint32_t b = a + 1;
            uint32_t c = a + static_cast<uint32_t>(nx) + 1;
            uint32_t d = a + static_cast<uint32_t>(nx);
            // Counter-clockwise seen from +Z so Jolt treats +Z as the front face.
            layout.faceIndices.insert(layout.faceIndices.end(), {a, b, c, a, c, d});
        }
    }
    core::computeTangents(layout.faceVertices, layout.faceIndices);

    // --- Plateau, diving board, spawn ------------------------------------------
    float boardX = uniform(-8.0f, 8.0f);
    float boardHalfLength = uniform(2.0f, 3.2f);
    layout.plateau = {glm::vec3(0.0f, H - 6.0f, -30.0f), glm::vec3(0.5f * kCliffWidth, 6.0f, 30.0f)};
    layout.divingBoard = {glm::vec3(boardX, H + 0.12f, boardHalfLength - 1.2f), glm::vec3(0.9f, 0.12f, boardHalfLength)};
    layout.spawnPoint = glm::vec3(boardX, H + 1.2f, -14.0f);

    // --- Lagoon ------------------------------------------------------------------
    float lagoonHalfWidth = uniform(14.0f, 24.0f);
    float lagoonReach = std::max(0.0f, layout.coveHalfWidth + 6.0f - lagoonHalfWidth);
    float lagoonX = uniform(-std::min(lagoonReach, 14.0f), std::min(lagoonReach, 14.0f));
    float footZ = -1e9f;
    for (float x = lagoonX - lagoonHalfWidth - 1.0f; x <= lagoonX + lagoonHalfWidth + 1.0f; x += 0.5f) {
        footZ = std::max(footZ, layout.faceZ(x, 0.0f));
    }
    float lagoonStart = footZ + uniform(2.0f, 8.0f);
    layout.lagoonMin = glm::vec2(lagoonX - lagoonHalfWidth, lagoonStart);
    layout.lagoonMax = glm::vec2(lagoonX + lagoonHalfWidth, lagoonStart + uniform(22.0f, 40.0f));

    // --- Obstacles, placed so none overlap -----------------------------------------
    std::vector<CliffCollider> placed{walkwayCollider(layout)};
    auto fits = [&](const CliffCollider& candidate) {
        if (blocksLagoon(layout, candidate)) return false;
        for (const CliffCollider& other : placed) {
            if (colliderGap(candidate, other) < kObstacleGap) return false;
        }
        return true;
    };
    const float obstacleHalfSpan = layout.coveHalfWidth + 4.0f;
    uint32_t rockSeed = seed * 7919u + 1u;

    int beamTarget = dense(static_cast<int>(H / 22.0f) + 2);
    for (int b = 0; b < beamTarget; ++b) {
        for (int attempt = 0; attempt < 40; ++attempt) {
            float x = uniform(-obstacleHalfSpan, obstacleHalfSpan);
            float y = uniform(layout.screeTop + 5.0f, H - 8.0f);
            float length = uniform(3.0f, 7.0f);
            CliffBeam beam;
            beam.halfExtents = glm::vec3(0.22f, 0.22f, 0.5f * length);
            beam.rotation = glm::angleAxis(uniform(-0.35f, 0.35f), glm::vec3(0.0f, 1.0f, 0.0f)) *
                            glm::angleAxis(uniform(-0.3f, 0.25f), glm::vec3(1.0f, 0.0f, 0.0f));
            glm::vec3 dir = beam.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 base = glm::vec3(x, y, layout.faceZ(x, y)) - dir * 0.6f;
            beam.position = base + dir * beam.halfExtents.z;
            CliffCollider collider = beamCollider(beam);
            if (beamBuried(layout, beam) || !fits(collider)) continue;
            layout.beams.push_back(beam);
            placed.push_back(collider);
            break;
        }
    }

    auto tryBoulder = [&](auto&& propose, int attempts) {
        for (int attempt = 0; attempt < attempts; ++attempt) {
            CliffBoulder boulder = propose();
            boulder.seed = rockSeed;
            CliffCollider collider = boulderCollider(boulder);
            if (!fits(collider)) continue;
            layout.boulders.push_back(boulder);
            placed.push_back(collider);
            ++rockSeed;
            return;
        }
    };
    auto roundish = [&]() { return glm::vec3(uniform(0.8f, 1.3f), uniform(0.6f, 1.0f), uniform(0.8f, 1.3f)); };
    auto yaw = [&]() { return glm::angleAxis(uniform(0.0f, 6.2831f), glm::vec3(0.0f, 1.0f, 0.0f)); };

    int outcrops = dense(std::min(static_cast<int>(H / 12.0f), 100) + count(2, 6));
    for (int b = 0; b < outcrops; ++b) {
        tryBoulder([&] {
            CliffBoulder boulder;
            boulder.radius = uniform(1.2f, 3.2f);
            boulder.stretch = roundish();
            boulder.rotation = yaw();
            float x = uniform(-obstacleHalfSpan, obstacleHalfSpan);
            float y = uniform(layout.screeTop, H - 8.0f);
            boulder.position = glm::vec3(x, y, layout.faceZ(x, y) + boulder.radius * 0.25f);
            return boulder;
        }, 30);
    }
    for (int b = dense(count(6, 12)); b > 0; --b) {
        tryBoulder([&] {
            CliffBoulder boulder;
            boulder.radius = uniform(0.8f, 2.2f);
            boulder.stretch = roundish();
            boulder.rotation = yaw();
            float x = uniform(-obstacleHalfSpan, obstacleHalfSpan);
            float y = uniform(1.0f, layout.screeTop);
            boulder.position = glm::vec3(x, y, layout.faceZ(x, y) + boulder.radius * 0.4f);
            return boulder;
        }, 30);
    }

    // Sea stacks: tall pillars standing on the ground in front of the cliff.
    for (int s = dense(count(0, 3)); s > 0; --s) {
        tryBoulder([&] {
            CliffBoulder stack;
            stack.radius = uniform(2.0f, 3.2f);
            stack.stretch = glm::vec3(uniform(0.8f, 1.1f), uniform(2.2f, 3.5f), uniform(0.8f, 1.1f));
            stack.rotation = yaw();
            float x = uniform(-45.0f, 45.0f);
            float footprint = stack.radius * std::max(stack.stretch.x, stack.stretch.z) * kRockBoundScale;
            float z = uniform(layout.faceZ(x, 0.5f) + footprint, layout.lagoonMax.y + 25.0f);
            stack.position = glm::vec3(x, 0.45f * stack.radius * stack.stretch.y, z);
            return stack;
        }, 30);
    }

    for (int b = dense(count(12, 24)); b > 0; --b) {
        tryBoulder([&] {
            CliffBoulder boulder;
            boulder.radius = uniform(1.2f, 3.0f);
            boulder.stretch = roundish();
            boulder.rotation = yaw();
            float x = uniform(-50.0f, 50.0f);
            float footprint = boulder.radius * std::max(boulder.stretch.x, boulder.stretch.z) * kRockBoundScale;
            float z = uniform(layout.faceZ(x, 0.5f) + footprint, layout.lagoonMax.y + 50.0f);
            boulder.position = glm::vec3(x, boulder.radius * 0.35f, z);
            return boulder;
        }, 30);
    }

    // --- Sun ------------------------------------------------------------------------
    // Always on the lagoon side so the face is lit; high enough that the rim can't shade the basin.
    const float sunAzimuth = uniform(-0.9f, 0.9f);
    const float sunElevation = uniform(0.70f, 1.0f);
    layout.sunDirection = glm::vec3(std::sin(sunAzimuth) * std::cos(sunElevation), std::sin(sunElevation),
                                    std::cos(sunAzimuth) * std::cos(sunElevation));

    // --- Basin and rim -------------------------------------------------------------
    float farthestGroundZ = layout.lagoonMax.y;
    float widestGroundX = std::max(std::abs(layout.lagoonMin.x), std::abs(layout.lagoonMax.x));
    for (const CliffBoulder& boulder : layout.boulders) {
        if (boulder.position.y > 12.0f) continue;
        float reach = boulder.radius * std::max({boulder.stretch.x, boulder.stretch.z}) * kRockBoundScale;
        farthestGroundZ = std::max(farthestGroundZ, boulder.position.z + reach);
        widestGroundX = std::max(widestGroundX, std::abs(boulder.position.x) + reach);
    }
    layout.basinMin = glm::vec2(-std::max(0.5f * kCliffWidth + 6.0f, widestGroundX + 8.0f) - uniform(0.0f, 30.0f), -10.0f);
    layout.basinMax = glm::vec2(std::max(0.5f * kCliffWidth + 6.0f, widestGroundX + 8.0f) + uniform(0.0f, 30.0f),
                                farthestGroundZ + uniform(20.0f, 60.0f));

    const uint32_t rimSeed = seed * 31u + 101u;
    const float rimPeak = std::min(H * uniform(0.3f, 0.7f), 520.0f);
    const float rimRise = uniform(35.0f, 90.0f);
    const float rimRoughness = uniform(0.6f, 1.4f);
    const glm::vec2 basinCenter = 0.5f * (layout.basinMin + layout.basinMax);
    const glm::vec2 sunFlat = glm::normalize(glm::vec2(layout.sunDirection.x, layout.sunDirection.z));
    const float sunSlope = std::tan(sunElevation);
    auto rimHeight = [&](float x, float z) {
        glm::vec2 q = glm::abs(glm::vec2(x, z) - basinCenter) - 0.5f * (layout.basinMax - layout.basinMin);
        float outside = glm::length(glm::max(q, 0.0f)) + std::min(std::max(q.x, q.y), 0.0f) - 12.0f;
        glm::vec2 outward = glm::vec2(x, z) - basinCenter;
        float angle = std::atan2(outward.y, outward.x);
        glm::vec3 ring(std::cos(angle), std::sin(angle), 0.0f);
        outside -= 18.0f * noise01(ring * 1.7f + glm::vec3(0.0f, 0.0f, 3.0f), rimSeed, 2);
        if (outside <= 0.0f) return -0.6f;

        float ridge = rimPeak * (0.45f + 0.75f * noise01(ring * 1.3f, rimSeed + 1u, 3));
        float rise = rimRise * (0.6f + 0.9f * noise01(ring * 2.1f + glm::vec3(5.0f, 0.0f, 0.0f), rimSeed + 2u, 2));
        float t = smooth01(outside / rise);
        float peaks = 1.0f - std::abs(rockNoise3D(glm::vec3(x, z, 0.0f) * 0.004f, rimSeed + 3u, 4));
        float rolling = noise01(glm::vec3(x, z, 9.0f) * 0.0015f, rimSeed + 4u, 2);
        float h = ridge * t * (0.55f + 0.35f * peaks * peaks + 0.25f * rolling);
        h += t * rimRoughness * (6.0f * rockNoise3D(glm::vec3(x, z, 2.0f) * 0.03f, rimSeed + 5u, 3) +
                                 0.012f * ridge * rockNoise3D(glm::vec3(x, z, 4.0f) * 0.012f, rimSeed + 6u, 2));

        // Shadows cast from here must not reach far into the basin.
        float towardsSun = glm::dot(glm::normalize(outward), sunFlat);
        if (towardsSun > 0.05f) h = std::min(h, (outside + 12.0f) * sunSlope / towardsSun);
        // Soft ceiling below the plateau so tall ridges round off instead of flattening.
        const float ceiling = H - 14.0f;
        if (h > 0.7f * ceiling) h = 0.7f * ceiling + 0.3f * ceiling * std::tanh((h - 0.7f * ceiling) / (0.3f * ceiling));
        return std::max(h, -0.6f);
    };

    const float rimExtent = std::max(520.0f, 1.4f * H);
    constexpr int kRimCells = 176;
    const float rimCell = 2.0f * rimExtent / static_cast<float>(kRimCells);
    const glm::vec2 rimOrigin = basinCenter - glm::vec2(rimExtent);
    const int rimRow = kRimCells + 1;
    layout.rimVertices.resize(static_cast<size_t>(rimRow) * rimRow);
    for (int j = 0; j < rimRow; ++j) {
        for (int i = 0; i < rimRow; ++i) {
            float x = rimOrigin.x + static_cast<float>(i) * rimCell;
            float z = rimOrigin.y + static_cast<float>(j) * rimCell;
            core::Vertex& v = layout.rimVertices[static_cast<size_t>(j) * rimRow + i];
            v.position = glm::vec3(x, rimHeight(x, z), z);
            v.uv = glm::vec2(x, z) * 0.05f;
        }
    }
    auto rimAt = [&](int i, int j) -> const glm::vec3& {
        i = std::clamp(i, 0, rimRow - 1);
        j = std::clamp(j, 0, rimRow - 1);
        return layout.rimVertices[static_cast<size_t>(j) * rimRow + i].position;
    };
    const float rimTop = std::max(rimPeak, 1.0f);
    for (int j = 0; j < rimRow; ++j) {
        for (int i = 0; i < rimRow; ++i) {
            core::Vertex& v = layout.rimVertices[static_cast<size_t>(j) * rimRow + i];
            v.normal = glm::normalize(glm::cross(rimAt(i, j + 1) - rimAt(i, j - 1), rimAt(i + 1, j) - rimAt(i - 1, j)));
            float strata = 0.5f + 0.5f * std::sin(v.position.y * 0.35f +
                                                  2.0f * rockNoise3D(v.position * 0.02f, rimSeed + 7u, 2));
            glm::vec3 rock = glm::mix(palette.light, palette.dark, 0.25f + 0.45f * strata);
            float flat = smooth01((v.normal.y - 0.72f) / 0.18f);
            glm::vec3 color = glm::mix(rock, palette.shelf, flat * 0.75f);
            if (options.theme == MapTheme::Glacier || options.theme == MapTheme::Coast) {
                float snow = smooth01((v.position.y / rimTop - 0.78f) / 0.12f) * smooth01((v.normal.y - 0.5f) / 0.2f);
                color = glm::mix(color, glm::vec3(0.96f, 0.97f, 1.0f), snow);
            }
            v.color = glm::vec4(color, 1.0f);
        }
    }
    layout.rimIndices.reserve(static_cast<size_t>(kRimCells) * kRimCells * 6);
    for (int j = 0; j < kRimCells; ++j) {
        for (int i = 0; i < kRimCells; ++i) {
            uint32_t a = static_cast<uint32_t>(j * rimRow + i);
            uint32_t b = a + 1;
            uint32_t c = a + static_cast<uint32_t>(rimRow) + 1;
            uint32_t d = a + static_cast<uint32_t>(rimRow);
            layout.rimIndices.insert(layout.rimIndices.end(), {a, d, c, a, c, b});
        }
    }
    core::computeTangents(layout.rimVertices, layout.rimIndices);

    // --- Ground ------------------------------------------------------------------
    constexpr float kGroundThickness = 8.0f;
    constexpr float kGroundExtent = 150.0f;
    auto groundSlab = [&](float x0, float x1, float z0, float z1, float top) {
        layout.groundPieces.push_back({glm::vec3(0.5f * (x0 + x1), top - 0.5f * kGroundThickness, 0.5f * (z0 + z1)),
                                       glm::vec3(0.5f * (x1 - x0), 0.5f * kGroundThickness, 0.5f * (z1 - z0))});
    };
    const glm::vec2 lo = layout.lagoonMin;
    const glm::vec2 hi = layout.lagoonMax;
    groundSlab(-kGroundExtent, kGroundExtent, -80.0f, lo.y, 0.0f);
    groundSlab(-kGroundExtent, kGroundExtent, hi.y, hi.y + 220.0f, 0.0f);
    groundSlab(-kGroundExtent, lo.x, lo.y, hi.y, 0.0f);
    groundSlab(hi.x, kGroundExtent, lo.y, hi.y, 0.0f);
    groundSlab(lo.x, hi.x, lo.y, hi.y, layout.lagoonFloorY);

    return layout;
}

std::vector<std::string> findLayoutProblems(const CliffLayout& layout) {
    std::vector<std::string> problems;
    char text[160];
    std::vector<CliffCollider> colliders;
    std::vector<std::string> names;
    for (size_t i = 0; i < layout.beams.size(); ++i) {
        colliders.push_back(beamCollider(layout.beams[i]));
        names.push_back("beam " + std::to_string(i));
        if (beamBuried(layout, layout.beams[i])) problems.push_back(names.back() + " runs into the cliff");
    }
    for (size_t i = 0; i < layout.boulders.size(); ++i) {
        colliders.push_back(boulderCollider(layout.boulders[i]));
        names.push_back("boulder " + std::to_string(i));
    }
    CliffCollider walkway = walkwayCollider(layout);
    for (size_t i = 0; i < colliders.size(); ++i) {
        if (blocksLagoon(layout, colliders[i])) problems.push_back(names[i] + " sits in the lagoon");
        if (colliderGap(colliders[i], walkway) < 0.0f) problems.push_back(names[i] + " blocks the diving board");
        for (size_t j = i + 1; j < colliders.size(); ++j) {
            float gap = colliderGap(colliders[i], colliders[j]);
            if (gap < 0.0f) {
                std::snprintf(text, sizeof(text), "%s overlaps %s by %.2f m", names[i].c_str(), names[j].c_str(), -gap);
                problems.emplace_back(text);
            }
        }
    }
    for (float x = layout.lagoonMin.x; x <= layout.lagoonMax.x; x += 0.5f) {
        if (layout.faceZ(x, 0.0f) >= layout.lagoonMin.y) {
            std::snprintf(text, sizeof(text), "cliff foot at x=%.1f reaches into the lagoon", x);
            problems.emplace_back(text);
            break;
        }
    }
    if (layout.rimVertices.empty()) problems.emplace_back("no rim terrain around the basin");
    for (const core::Vertex& v : layout.rimVertices) {
        glm::vec2 p(v.position.x, v.position.z);
        bool inBasin = p.x > layout.basinMin.x && p.x < layout.basinMax.x && p.y > layout.basinMin.y && p.y < layout.basinMax.y;
        if (inBasin && v.position.y > 0.0f) {
            std::snprintf(text, sizeof(text), "rim rises to %.1f m inside the basin at (%.0f, %.0f)", v.position.y, p.x, p.y);
            problems.emplace_back(text);
            break;
        }
        if (v.position.y > layout.height - 12.0f) {
            problems.emplace_back("rim pokes through the plateau");
            break;
        }
    }
    float tip = layout.divingBoard.center.z + layout.divingBoard.halfExtents.z;
    if (tip <= layout.faceZ(layout.divingBoard.center.x, layout.height - 0.5f)) {
        problems.emplace_back("diving board does not reach past the lip");
    }
    return problems;
}

} // namespace engine::brokenbones

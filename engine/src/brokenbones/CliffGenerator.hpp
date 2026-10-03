#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/Mesh.hpp"

namespace engine::brokenbones {

struct CliffLedge {
    float y = 0.0f;     // height of the shelf lip
    float depth = 0.0f; // how far the shelf steps out
    float xCenter = 0.0f;
    float halfWidth = 1e6f; // partial ledges only span part of the face
};

// A vertical band that bulges out (buttress, amount > 0) or cuts in (gully, amount < 0).
struct CliffFeature {
    float x = 0.0f;
    float halfWidth = 4.0f;
    float amount = 0.0f;
    float yMin = 0.0f;
    float yMax = 0.0f;
};

struct CliffBoulder {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
    glm::vec3 stretch{1.0f};
    uint32_t seed = 0;
};

struct CliffBeam {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents{0.25f};
};

struct AxisBox {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.5f};
};

// Conservative capsule (a sphere when a == b) around an obstacle.
struct CliffCollider {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    float radius = 0.0f;
};

enum class MapTheme { Coast, Glacier, Canyon, Volcano };
inline constexpr size_t kMapThemeCount = 4;

struct MapThemeInfo {
    const char* name;
    float cashMultiplier;
    float obstacleDensity;
    const char* splashText;
    glm::vec3 fogColor;
    glm::vec3 skyZenith;
    glm::vec3 skyHorizon;
    glm::vec3 groundTint;
    glm::vec3 waterColor;
    glm::vec3 waterGlow; // emissive; zero for plain water
};
[[nodiscard]] const MapThemeInfo& mapThemeInfo(MapTheme theme);

struct CliffLayout {
    uint32_t seed = 0;
    int level = 1;
    float height = 0.0f;
    float width = 0.0f;

    std::vector<CliffLedge> ledges;
    std::vector<CliffFeature> features;
    float screeTop = 0.0f;
    float screeSlope = 1.35f;
    float lean = 0.0f;
    float coveHalfWidth = 22.0f;
    float coveCurve = 0.06f;
    float cragAmplitude = 2.2f;
    float cragFrequency = 0.08f;
    float overhangY = 0.0f;
    float overhangHalfHeight = 0.0f;
    float overhangDepth = 0.0f;
    glm::vec3 rockTint{1.0f};
    const char* rockName = "granite";
    MapTheme theme = MapTheme::Coast;

    std::vector<core::Vertex> faceVertices;
    std::vector<uint32_t> faceIndices;

    std::vector<CliffBoulder> boulders;
    std::vector<CliffBeam> beams;

    AxisBox plateau;
    AxisBox divingBoard;
    std::vector<AxisBox> groundPieces;

    glm::vec2 lagoonMin{0.0f}; // xz
    glm::vec2 lagoonMax{0.0f};
    float waterY = -0.4f;
    float lagoonFloorY = -5.0f;

    glm::vec3 spawnPoint{0.0f};
    float spawnYawDegrees = 90.0f; // camera yaw looking towards +Z

    // Outward offset of the face at (x, y), including detail noise.
    [[nodiscard]] float faceZ(float x, float y) const;
    [[nodiscard]] bool inLagoon(glm::vec3 p) const {
        return p.x >= lagoonMin.x && p.x <= lagoonMax.x && p.z >= lagoonMin.y && p.z <= lagoonMax.y;
    }
};

inline constexpr float kMaxCliffHeight = 1200.0f;
[[nodiscard]] float cliffHeightForLevel(int level);

struct MapOptions {
    MapTheme theme = MapTheme::Coast;
    float heightScale = 1.0f;
};
[[nodiscard]] CliffLayout generateCliffLayout(uint32_t seed, int level, MapOptions options = {});

[[nodiscard]] CliffCollider boulderCollider(const CliffBoulder& boulder);
[[nodiscard]] CliffCollider beamCollider(const CliffBeam& beam);
[[nodiscard]] float colliderGap(const CliffCollider& a, const CliffCollider& b);

// Every overlap or blocked area in the layout, described for test output; empty means clean.
[[nodiscard]] std::vector<std::string> findLayoutProblems(const CliffLayout& layout);

} // namespace engine::brokenbones

#pragma once

#include <array>

#include <glm/glm.hpp>

#include "core/SceneTypes.hpp"

// Stable cascaded-shadow-map fitting (pure math, no GPU dependency).
//
// Each cascade covers one depth slice of the view frustum with the minimal
// bounding sphere of that slice. A sphere is invariant under camera
// rotation, so the ortho extent (and therefore texel size) never changes
// while the camera turns. The light basis is fixed (built at the world
// origin), so snapping the sphere centre to the texel grid in that basis
// makes the rasterised shadow texels world-stable while translating.
// Together these remove both rotational and translational shimmer.

namespace engine::core::render {

// Must match kCascadeBlendFraction in shaders/kronos/shadows.glsl: each
// cascade is fitted to also cover the blend band of the previous one.
inline constexpr float kCascadeBlendFraction = 0.12f;

struct CascadeSettings {
    float nearPlane = 0.05f;
    float maxDistance = 80.0f;
    float splitLambda = 0.75f;
    float verticalFovRadians = 1.0f;
    float aspectRatio = 16.0f / 9.0f;
    uint32_t resolution = 2048;
    // Extra depth toward the light so casters outside the view slice still
    // land in the map; with depth clamp enabled they pancake onto the near
    // plane instead of being clipped.
    float casterPadding = 20.0f;
};

struct CascadeFit {
    glm::mat4 viewProj{1.0f};
    glm::vec3 centerWS{0.0f};
    float radius = 0.0f;
    float splitFar = 0.0f;
    float texelWorld = 0.0f;
    float depthRange = 0.0f;
};

using CascadeFits = std::array<CascadeFit, kShadowCascadeCount>;

// Minimal bounding sphere of the frustum slice [sliceNear, sliceFar] of a
// symmetric perspective frustum; returns (distance of centre along the
// view axis, radius). tanSq = tan(halfFovX)^2 + tan(halfFovY)^2.
[[nodiscard]] glm::vec2 frustumSliceBoundingSphere(float sliceNear, float sliceFar, float tanSq);

// `cameraView` is the (unjittered) world->view matrix; `lightDirWS` points
// from the light toward the scene.
[[nodiscard]] CascadeFits fitCascades(const glm::mat4& cameraView, glm::vec3 lightDirWS,
                                      const CascadeSettings& settings);

// Conservative sphere-vs-cascade test for caster culling: the cascade's
// ortho volume is unbounded toward the light (pancaking), so only the
// light-space XY footprint and the far side matter.
[[nodiscard]] bool sphereIntersectsCascade(const CascadeFit& cascade, glm::vec3 lightDirWS, glm::vec3 center,
                                           float radius);

} // namespace engine::core::render

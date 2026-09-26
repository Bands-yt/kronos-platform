#include "core/render/ShadowCascades.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "core/CascadeSplitMath.hpp"

namespace engine::core::render {

namespace {

glm::mat4 fixedLightView(glm::vec3 lightDirWS) {
    glm::vec3 up = std::abs(lightDirWS.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(glm::vec3(0.0f), lightDirWS, up);
}

} // namespace

glm::vec2 frustumSliceBoundingSphere(float sliceNear, float sliceFar, float tanSq) {
    // A sphere centred on the view axis at distance c must reach the corner
    // rings at both ends: r^2 = (c-n)^2 + n^2 t^2 = (c-f)^2 + f^2 t^2, which
    // solves to c = (n+f)(1+t^2)/2. Wide or thin slices push c past the far
    // plane; the far ring alone then bounds the slice.
    float center = 0.5f * (sliceNear + sliceFar) * (1.0f + tanSq);
    if (center >= sliceFar) return {sliceFar, sliceFar * std::sqrt(tanSq)};
    float dz = sliceFar - center;
    return {center, std::sqrt(dz * dz + sliceFar * sliceFar * tanSq)};
}

CascadeFits fitCascades(const glm::mat4& cameraView, glm::vec3 lightDirWS, const CascadeSettings& settings) {
    CascadeFits fits{};
    glm::vec3 lightDir = glm::normalize(lightDirWS);

    glm::mat4 invView = glm::inverse(cameraView);
    glm::vec3 cameraPos = glm::vec3(invView[3]);
    glm::vec3 forward = -glm::normalize(glm::vec3(invView[2]));

    float tanV = std::tan(settings.verticalFovRadians * 0.5f);
    float tanH = tanV * settings.aspectRatio;
    float tanSq = tanH * tanH + tanV * tanV;

    auto splits = computeCascadeSplitDepths<kShadowCascadeCount>(settings.nearPlane, settings.maxDistance,
                                                                  settings.splitLambda);
    glm::mat4 lightView = fixedLightView(lightDir);
    float resolution = static_cast<float>(settings.resolution);

    float previousNear = settings.nearPlane;
    float previousFar = settings.nearPlane;
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        float sliceFar = splits[i];
        float sliceNear = i == 0 ? settings.nearPlane
                                 : previousFar - (previousFar - previousNear) * kCascadeBlendFraction;

        glm::vec2 sphere = frustumSliceBoundingSphere(sliceNear, sliceFar, tanSq);
        // Quantising the radius keeps the texel size bit-identical across
        // frames even with float noise in the camera's projection inputs.
        float radius = std::ceil(sphere.y * 16.0f) / 16.0f;
        glm::vec3 centerWS = cameraPos + forward * sphere.x;

        float texelWorld = 2.0f * radius / resolution;
        glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(centerWS, 1.0f));
        centerLS.x = std::floor(centerLS.x / texelWorld) * texelWorld;
        centerLS.y = std::floor(centerLS.y / texelWorld) * texelWorld;

        // lookAt's view space faces -Z, so forward distance is -z.
        float depth = -centerLS.z;
        float orthoNear = depth - radius - settings.casterPadding;
        float orthoFar = depth + radius;
        // Bottom and top are swapped for Vulkan's downward Y. Negating [1][1]
        // alone flips the scale but not the offset, which pushes the cascade
        // off its fitted sphere away from the world origin.
        glm::mat4 lightProj = glm::ortho(centerLS.x - radius, centerLS.x + radius, centerLS.y + radius,
                                         centerLS.y - radius, orthoNear, orthoFar);

        CascadeFit& fit = fits[i];
        fit.viewProj = lightProj * lightView;
        fit.centerWS = centerWS;
        fit.radius = radius;
        fit.splitFar = sliceFar;
        fit.texelWorld = texelWorld;
        fit.depthRange = orthoFar - orthoNear;

        previousNear = i == 0 ? settings.nearPlane : splits[i - 1];
        previousFar = sliceFar;
    }
    return fits;
}

bool sphereIntersectsCascade(const CascadeFit& cascade, glm::vec3 lightDirWS, glm::vec3 center, float radius) {
    glm::vec3 lightDir = glm::normalize(lightDirWS);
    glm::vec3 offset = center - cascade.centerWS;
    float along = glm::dot(offset, lightDir);
    if (along - radius > cascade.radius) return false;
    glm::vec3 lateral = offset - lightDir * along;
    // Circumscribing the square ortho footprint (half-extent r) by a
    // circle of radius r*sqrt(2) keeps the test conservative.
    float reach = cascade.radius * 1.41421356f + radius;
    return glm::dot(lateral, lateral) <= reach * reach;
}

} // namespace engine::core::render

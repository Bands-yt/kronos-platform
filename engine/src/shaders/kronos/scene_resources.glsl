#ifndef KRONOS_SCENE_RESOURCES_GLSL
#define KRONOS_SCENE_RESOURCES_GLSL

// Set 0 resources shared by every forward-shaded pass. Binding numbers
// mirror Renderer::createSceneDescriptorResources().

#include "scene_ubo.glsl"

layout(set = 0, binding = 1) uniform sampler2DArray shadowMapDepth;
layout(set = 0, binding = 4) uniform sampler2DArrayShadow shadowMapCompare;
layout(set = 0, binding = 12) uniform sampler2DArrayShadow spotShadowMaps;
layout(set = 0, binding = 5) uniform samplerCube envPrefiltered;
layout(set = 0, binding = 6) uniform sampler2D dfgLut;

layout(std430, set = 0, binding = 7) readonly buffer EnvironmentSH {
    vec4 shIrradiance[9]; // cosine-convolved, already divided by PI (evaluates to radiance-equivalent irradiance)
} envSH;

struct GpuLight {
    vec4 positionRange;  // xyz world position, w range (influence ends here)
    vec4 colorIntensity;
    vec4 directionType;  // xyz direction light travels (spot), w type: 0 point, 1 spot
    vec4 spotParams;     // x cos(outer), y 1/(cos(inner) - cos(outer)), z softening radius^2, w shadow slot (< 0: none)
};

layout(std430, set = 0, binding = 8) readonly buffer LightBuffer {
    GpuLight lights[];
} lightBuffer;

layout(std430, set = 0, binding = 9) readonly buffer ClusterCounts {
    uint counts[];
} clusterCounts;

layout(std430, set = 0, binding = 10) readonly buffer ClusterLightIndices {
    uint indices[];
} clusterIndices;

#include "object_records.glsl"

#endif

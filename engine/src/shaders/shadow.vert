#version 450

// Depth-only shadow pass (no fragment stage). Run once per sun cascade and
// once per shadowed spot light; the push-constant viewIndex selects which
// SceneUBO matrix to project through.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // unused, kept only so the vertex input layout matches scene.vert's
layout(location = 2) in vec2 inUV;     // unused, same reason

#include "kronos/scene_ubo.glsl"

// Deliberately NOT ObjectPushConstants -- this pipeline has its own,
// smaller layout (shadowPipelineLayout_, see Renderer.cpp) since this pass
// needs viewIndex, which the main pass has no use for, and never reads
// baseColor/metallicRoughness/emissive at all.
layout(push_constant) uniform ShadowPushConstants {
    mat4 model;
    int viewIndex; // cascade, or KRONOS_CASCADE_COUNT + spot shadow slot
} object;

void main() {
    mat4 lightViewProj = object.viewIndex < KRONOS_CASCADE_COUNT
        ? scene.lightViewProj[object.viewIndex]
        : scene.spotShadowViewProj[object.viewIndex - KRONOS_CASCADE_COUNT];
    gl_Position = lightViewProj * object.model * vec4(inPosition, 1.0);
}

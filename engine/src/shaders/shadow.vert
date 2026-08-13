#version 450

// Depth-only cascaded shadow pass -- vertex stage only, no fragment shader
// (valid and standard for a depth/stencil-only render with no color
// attachments; see Renderer::createShadowPipeline()'s comment). Consumes
// the same vertex buffers as scene.vert (position/normal/uv) but only
// reads position -- normal/uv are irrelevant to "how far is this surface
// from the light". Run once per cascade (see Renderer::drawShadowPass's
// loop), each time with a different push-constant cascadeIndex selecting
// which of SceneUBO's lightViewProj[] matrices to project through.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal; // unused, kept only so the vertex input layout matches scene.vert's
layout(location = 2) in vec2 inUV;     // unused, same reason

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj[3];
    vec4 cascadeSplitsView;
    vec4 lightDirectionWS;
    vec4 lightColorIntensity;
    vec4 viewPositionWS;
    vec4 ambientColor;
} scene;

// Deliberately NOT ObjectPushConstants -- this pipeline has its own,
// smaller layout (shadowPipelineLayout_, see Renderer.cpp) since this pass
// needs cascadeIndex, which the main pass has no use for, and never reads
// baseColor/metallicRoughness/emissive at all.
layout(push_constant) uniform ShadowPushConstants {
    mat4 model;
    int cascadeIndex;
} object;

void main() {
    gl_Position = scene.lightViewProj[object.cascadeIndex] * object.model * vec4(inPosition, 1.0);
}

#version 450

// Billboarded particle rendering -- one shared unit quad (Mesh::createQuad,
// binding 0) instanced once per live core::Particle (binding 1, see
// core::ParticleInstanceData), rebuilt to face the camera here rather than
// baking a fixed orientation into the mesh. See Renderer::drawParticles().

layout(location = 0) in vec3 inPosition; // local quad corner, -halfSize..halfSize on XY, Z=0
layout(location = 1) in vec3 inNormal;   // unused
layout(location = 2) in vec2 inUV;
// location = 3 (Vertex::tangent) intentionally not declared here -- this
// shader doesn't need it, and Vulkan allows a pipeline's vertex input
// state to describe more attributes than the shader consumes. See
// core::ParticleInstanceData::attributeDescriptions()'s comment for why
// this struct's own locations start at 4, not 3.

layout(location = 4) in vec4 inInstancePositionSize; // xyz: world position, w: current size
layout(location = 5) in vec4 inInstanceColor;
layout(location = 6) in vec4 inInstancePrevPositionSize;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;
layout(location = 2) out vec4 outClipPos;
layout(location = 3) out vec4 outPrevClipPos;

#include "kronos/scene_ubo.glsl"

void main() {
    // Camera-facing billboard: a pure-rotation view matrix's inverse is
    // its transpose, so the view matrix's rows already ARE the world-
    // space camera right/up/forward axes -- extracting them here means
    // one less piece of camera state to pass in and keep in sync.
    vec3 right = vec3(scene.view[0][0], scene.view[1][0], scene.view[2][0]);
    vec3 up    = vec3(scene.view[0][1], scene.view[1][1], scene.view[2][1]);

    vec3 corner = right * inPosition.x + up * inPosition.y;
    vec3 worldPos = inInstancePositionSize.xyz + corner * inInstancePositionSize.w;
    // The previous corner reuses this frame's billboard axes; the error under
    // camera rotation scales with particle size and stays sub-pixel for sparks.
    vec3 prevWorldPos = inInstancePrevPositionSize.xyz + corner * inInstancePrevPositionSize.w;

    outUV = inUV;
    outColor = inInstanceColor;
    outClipPos = scene.viewProjNoJitter * vec4(worldPos, 1.0);
    outPrevClipPos = scene.prevViewProjNoJitter * vec4(prevWorldPos, 1.0);
    gl_Position = scene.proj * scene.view * vec4(worldPos, 1.0);
}

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

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj[3];
    mat4 invViewProj; // only shaders/sky.frag reads this
    vec4 cascadeSplitsView;
    vec4 cascadeBiasScale;
    vec4 lightDirectionWS;
    vec4 lightColorIntensity;
    vec4 viewPositionWS;
    vec4 ambientColor;
    vec4 ambientGroundColor;
    vec4 fogColorDensity; // only shaders/scene.frag reads this
    vec4 skyZenithColor;  // only shaders/sky.frag reads this
    vec4 skyHorizonColor; // only shaders/sky.frag reads this
} scene;

void main() {
    // Camera-facing billboard: a pure-rotation view matrix's inverse is
    // its transpose, so the view matrix's rows already ARE the world-
    // space camera right/up/forward axes -- extracting them here means
    // one less piece of camera state to pass in and keep in sync.
    vec3 right = vec3(scene.view[0][0], scene.view[1][0], scene.view[2][0]);
    vec3 up    = vec3(scene.view[0][1], scene.view[1][1], scene.view[2][1]);

    vec3 worldPos = inInstancePositionSize.xyz
        + right * inPosition.x * inInstancePositionSize.w
        + up    * inPosition.y * inInstancePositionSize.w;

    outUV = inUV;
    outColor = inInstanceColor;
    gl_Position = scene.proj * scene.view * vec4(worldPos, 1.0);
}

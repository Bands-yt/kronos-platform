#version 450

// GPU-driven instanced path -- the batched equivalent of scene.vert. Same
// per-vertex inputs (binding 0), plus per-instance inputs (binding 1,
// VK_VERTEX_INPUT_RATE_INSTANCE -- see core::InstanceData's binding/
// attribute descriptions) instead of a push-constant model/material per
// draw call. Produces identical outputs, at the same locations, as
// scene.vert -- both feed the same shaders/scene.frag, which has no idea
// (and no need to know) which of the two ran. Used for entities with
// Renderable::instanced set (Components.hpp): many copies of the same
// mesh (ores, rocks, foliage) drawn in one vkCmdDrawIndexed(instanceCount
// = N) instead of N individual draws + push-constant updates.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent; // xyz: tangent, w: handedness -- see Mesh.hpp's Vertex::tangent

// Per-instance attributes -- glm::mat4 consumes 4 consecutive locations
// (one per column; see core::InstanceData::attributeDescriptions). Start
// at 4, not 3, since location 3 (above) is now Vertex's own tangent.
layout(location = 4) in mat4 inInstanceModel;
layout(location = 8) in vec4 inInstanceBaseColor;
layout(location = 9) in vec4 inInstanceMetallicRoughness;
layout(location = 10) in vec4 inInstanceEmissive;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out flat vec4 outBaseColor;
layout(location = 4) out flat vec4 outMetallicRoughness;
layout(location = 5) out flat vec4 outEmissive;
// World-space tangent + handedness, at the same location scene.vert uses
// -- see that file's comment on why this is appended at 6 rather than
// renumbering the outputs above.
layout(location = 6) out vec4 outWorldTangent;

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
    vec4 worldPos = inInstanceModel * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(inInstanceModel)));
    outWorldNormal = normalize(normalMatrix * inNormal);
    // Tangent transforms by the model matrix directly (not the inverse-
    // transpose normal matrix -- tangent is a direction embedded in the
    // surface, not a covector like the normal), handedness passes through
    // unchanged.
    outWorldTangent = vec4(normalize(mat3(inInstanceModel) * inTangent.xyz), inTangent.w);

    outUV = inUV;
    outBaseColor = inInstanceBaseColor;
    outMetallicRoughness = inInstanceMetallicRoughness;
    outEmissive = inInstanceEmissive;
    gl_Position = scene.proj * scene.view * worldPos;
}

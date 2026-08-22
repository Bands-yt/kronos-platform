#version 450

// The GPU-skinned counterpart to shaders/scene.vert -- same per-object
// push constants and SceneUBO (set 0), same output locations (so
// shaders/scene.frag runs completely unchanged for either vertex
// shader), but reads a second vertex buffer (binding 1, ivec4 joint
// indices + vec4 weights -- core::GpuSkinVertex, RiggedMesh.hpp) and a
// third descriptor set (set 2, this frame's bone matrices for whichever
// rigged entity is being drawn -- core::Renderer::FrameSync's skinning
// slots, one real, independent slot per concurrently-drawn skinned
// entity so two rigged entities in the same frame never share a bone
// buffer -- see AuxiliarySceneHandle's doc comment in Renderer.hpp for
// the exact class of same-frame GPU resource collision this pattern was
// built to avoid).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;
layout(location = 4) in ivec4 inJointIndices; // -1 = unused influence slot
layout(location = 5) in vec4 inJointWeights;  // 0.0 for an unused slot -- see main()'s clamp comment
// Kronos ("Avatar Visual Silhouette Pass" -- real per-vertex color):
// location 11, same real reasoning as scene.vert's own inColor -- past
// this pipeline's own binding-1 skin data (locations 4-5).
layout(location = 11) in vec4 inColor;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out flat vec4 outBaseColor;
layout(location = 4) out flat vec4 outMetallicRoughness;
layout(location = 5) out flat vec4 outEmissive;
layout(location = 8) out flat uvec4 outTextureIndices;
layout(location = 6) out vec4 outWorldTangent;
// Kronos ("Avatar Visual Silhouette Pass" -- real per-vertex color): not
// flat -- see scene.vert's own comment on outVertexColor.
layout(location = 7) out vec4 outVertexColor;

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

layout(push_constant) uniform ObjectPushConstants {
    mat4 model;
    vec4 baseColor;
    vec4 metallicRoughness;
    vec4 emissive;
    // Packed bindless slots, two 16-bit indices per component -- see
    // core::ObjectPushConstants and scene.frag. Present unconditionally so
    // the push-constant block keeps matching the C++ struct byte-for-byte
    // whichever fragment variant is bound.
    uvec4 textureIndices;
} object;

// Must match core::Renderer::kMaxJointsPerSkeleton exactly (Renderer.hpp).
#define MAX_JOINTS 64
layout(set = 2, binding = 0) uniform SkinningUBO {
    mat4 boneMatrices[MAX_JOINTS]; // already composed jointWorldMatrix * inverseBindMatrix -- see AnimationPlayer::computeSkinningMatrices()
} skinning;

void main() {
    // -1 (unused slot) must never reach the array index below --
    // negative/out-of-range indexing into boneMatrices[] is undefined
    // behavior in GLSL/SPIR-V (real GPU drivers can hard-fault on it,
    // not just return garbage), regardless of the fact that its weight
    // is 0.0 and would otherwise contribute nothing. Clamping to 0 keeps
    // the *index* always in-bounds; the zero weight already guarantees
    // that clamped read contributes nothing to the blend.
    ivec4 safeJointIndices = max(inJointIndices, ivec4(0));

    mat4 skinMatrix = inJointWeights.x * skinning.boneMatrices[safeJointIndices.x] +
                       inJointWeights.y * skinning.boneMatrices[safeJointIndices.y] +
                       inJointWeights.z * skinning.boneMatrices[safeJointIndices.z] +
                       inJointWeights.w * skinning.boneMatrices[safeJointIndices.w];

    vec4 skinnedLocalPos = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedLocalNormal = mat3(skinMatrix) * inNormal;
    vec3 skinnedLocalTangent = mat3(skinMatrix) * inTangent.xyz;

    vec4 worldPos = object.model * skinnedLocalPos;
    outWorldPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(object.model)));
    outWorldNormal = normalize(normalMatrix * skinnedLocalNormal);
    outWorldTangent = vec4(normalize(mat3(object.model) * skinnedLocalTangent), inTangent.w);

    outUV = inUV;
    outBaseColor = object.baseColor;
    outMetallicRoughness = object.metallicRoughness;
    outEmissive = object.emissive;
    outTextureIndices = object.textureIndices;
    outVertexColor = inColor;
    gl_Position = scene.proj * scene.view * worldPos;
}

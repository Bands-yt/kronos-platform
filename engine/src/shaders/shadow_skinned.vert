#version 450

// Depth-only shadow pass for skinned characters: shadow.vert plus the
// skinning palette (set 1 here; the scene pipeline binds it at set 2).

layout(location = 0) in vec3 inPosition;
layout(location = 4) in ivec4 inJointIndices;
layout(location = 5) in vec4 inJointWeights;

#include "kronos/scene_ubo.glsl"

layout(push_constant) uniform ShadowPushConstants {
    mat4 model;
    int viewIndex;
} object;

#define MAX_JOINTS 64
layout(set = 1, binding = 0) uniform SkinningUBO {
    mat4 boneMatrices[MAX_JOINTS];
    mat4 prevBoneMatrices[MAX_JOINTS];
} skinning;

void main() {
    ivec4 joints = clamp(inJointIndices, ivec4(0), ivec4(MAX_JOINTS - 1));
    vec4 weights = max(inJointWeights, vec4(0.0));
    mat4 skin = weights.x * skinning.boneMatrices[joints.x] + weights.y * skinning.boneMatrices[joints.y] +
                weights.z * skinning.boneMatrices[joints.z] + weights.w * skinning.boneMatrices[joints.w];
    mat4 lightViewProj = object.viewIndex < KRONOS_CASCADE_COUNT
        ? scene.lightViewProj[object.viewIndex]
        : scene.spotShadowViewProj[object.viewIndex - KRONOS_CASCADE_COUNT];
    gl_Position = lightViewProj * object.model * skin * vec4(inPosition, 1.0);
}

#version 450

// shadow.vert for automatically instanced casters: the model matrix comes
// per instance; ShadowPushConstants::model is ignored.

layout(location = 0) in vec3 inPosition;
layout(location = 4) in mat4 inInstanceModel;

#include "kronos/scene_ubo.glsl"

layout(push_constant) uniform ShadowPushConstants {
    mat4 model;
    int viewIndex;
} object;

void main() {
    mat4 lightViewProj = object.viewIndex < KRONOS_CASCADE_COUNT
        ? scene.lightViewProj[object.viewIndex]
        : scene.spotShadowViewProj[object.viewIndex - KRONOS_CASCADE_COUNT];
    gl_Position = lightViewProj * inInstanceModel * vec4(inPosition, 1.0);
}

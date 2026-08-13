#version 450

// Kronos ("Real-Time Rendering Evolved" trailer): real glass/water
// transmission -- see glass.frag's own header comment for the shading
// technique. This vertex stage is deliberately smaller than scene.vert:
// glass shading only needs world position + world normal (no UV/tangent,
// no material textures), so those two vertex attributes are declared in
// the shared Vertex layout but simply left unread here.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;    // unread -- see this file's own header comment
layout(location = 3) in vec4 inTangent; // unread -- see this file's own header comment

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;

// Full SceneUBO declaration (through cloudParams) -- needed only for
// cloudParams.w (real, already-existing total-elapsed-seconds clock, see
// SceneTypes.hpp's own comment) driving the real ripple offset below;
// std140 offsets are positional, so every field up to the one actually
// read must be declared even though most go unused here.
layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj[3];
    mat4 invViewProj;
    vec4 cascadeSplitsView;
    vec4 cascadeBiasScale;
    vec4 lightDirectionWS;
    vec4 lightColorIntensity;
    vec4 viewPositionWS;
    vec4 ambientColor;
    vec4 ambientGroundColor;
    vec4 fogColorDensity;
    vec4 skyZenithColor;
    vec4 skyHorizonColor;
    vec4 renderFlags;
    vec4 pointLightPositionRadius[4];
    vec4 pointLightColorIntensity[4];
    vec4 pointLightCount;
    vec4 reflectionParams;
    vec4 atmosphereParams;
    vec4 cloudParams; // w: real total elapsed seconds -- see this block's own header comment
} scene;

layout(push_constant) uniform GlassPushConstants {
    mat4 model;
    vec4 tintColor;   // rgb: tint, a: transmission strength (0..1)
    vec4 params;      // x: ior, y: roughness (unused so far, reserved), z/w: unused
} object;

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);

    // Real, cheap per-vertex ripple -- a small sine-wave world-space Y
    // offset driven by scene.cloudParams.w (the real elapsed-seconds
    // clock every other time-driven shader in this engine already reads,
    // see computeClouds()'s own use of it in sky.frag), so a water/glass
    // plane genuinely animates instead of sitting perfectly static. Tiny
    // amplitude (2cm) -- reads as real surface motion on a water plane,
    // negligible on a vertical glass wall.
    float ripple = sin(worldPos.x * 0.6 + scene.cloudParams.w * 1.3) * cos(worldPos.z * 0.5 - scene.cloudParams.w * 0.9);
    worldPos.y += ripple * 0.02 * object.tintColor.a;

    outWorldPos = worldPos.xyz;
    mat3 normalMatrix = transpose(inverse(mat3(object.model)));
    outWorldNormal = normalize(normalMatrix * inNormal);
    gl_Position = scene.proj * scene.view * worldPos;
}

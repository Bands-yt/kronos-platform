#version 450

// Thin transparent surfaces (windows, water sheets). Reflection and
// transmission both read the prefiltered environment, so glass matches the
// sky and IBL the opaque pass uses; the sun highlight is the same
// energy-normalised GGX lobe as opaque surfaces.

#include "kronos/scene_resources.glsl"
#include "kronos/material.glsl"
#include "kronos/ibl.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform GlassPushConstants {
    mat4 model;
    vec4 tintColor;
    vec4 params; // x: ior, y: roughness, z/w: unused
} object;

vec3 applyFog(vec3 color, float viewDepth) {
    float density = scene.fogColorDensity.a;
    float fogFactor = clamp(exp(-pow(density * viewDepth, 2.0)), 0.0, 1.0);
    return mix(scene.fogColorDensity.rgb, color, fogFactor);
}

vec3 environment(vec3 dir, float perceptualRoughness, vec3 N) {
    if (!iblAvailable()) return hemisphereAmbient(dir);
    return prefilteredRadiance(dir, perceptualRoughness) * reflectionNormalization(N) * scene.iblParams.x;
}

void main() {
    vec3 N = normalize(inWorldNormal);
    vec3 V = normalize(scene.viewPositionWS.xyz - inWorldPos);
    if (dot(N, V) < 0.0) N = -N;

    float ior = max(object.params.x, 1.01);
    float perceptualRoughness = clamp(object.params.y, kMinPerceptualRoughness, 1.0);
    float roughness = perceptualRoughness * perceptualRoughness;
    float f0 = pow((1.0 - ior) / (1.0 + ior), 2.0);
    float NoV = max(dot(N, V), 1e-4);

    // Split-sum directional albedo is the correctly roughness-aware
    // replacement for a bare Schlick term at NoV.
    vec2 dfg = sampleDFG(NoV, perceptualRoughness).xy;
    float reflectance = f0 * dfg.x + dfg.y;

    vec3 reflected = environment(reflect(-V, N), perceptualRoughness, N);
    vec3 refractDir = refract(-V, N, 1.0 / ior);
    vec3 transmitted = dot(refractDir, refractDir) < 1e-6
                           ? reflected
                           : environment(refractDir, perceptualRoughness, N) * object.tintColor.rgb;

    float transmission = saturate(object.tintColor.a);
    vec3 body = mix(object.tintColor.rgb * hemisphereAmbient(N), transmitted, transmission);
    vec3 color = mix(body, reflected, reflectance);

    vec3 L = normalize(-scene.lightDirectionWS.xyz);
    vec3 H = normalize(L + V);
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float spec = D_GGX(NoH, roughness) * V_SmithGGXCorrelated(NoV, NoL, roughness) *
                 F_Schlick(f0, 1.0, saturate(dot(L, H)));
    color += scene.lightColorIntensity.rgb * scene.lightColorIntensity.a * spec * NoL;

    color = applyFog(color, length(inWorldPos - scene.viewPositionWS.xyz));
    outColor = vec4(color, 1.0);
}

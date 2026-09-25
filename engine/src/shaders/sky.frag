#version 450

// Procedural sky background, drawn before opaque geometry with depth
// test/write off. The radiance model lives in kronos/sky.glsl so the IBL
// capture sees exactly the sky that is on screen.

#include "kronos/scene_ubo.glsl"
#include "kronos/common.glsl"
#include "kronos/sky.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

void main() {
    vec2 ndc = inUV * 2.0 - 1.0;
    vec4 farPoint = scene.invViewProj * vec4(ndc, 1.0, 1.0);
    farPoint /= farPoint.w;
    vec3 rayDir = normalize(farPoint.xyz - scene.viewPositionWS.xyz);

    SkyInputs inputs;
    inputs.zenith = scene.skyZenithColor.rgb;
    inputs.horizon = scene.skyHorizonColor.rgb;
    inputs.sunDir = normalize(-scene.lightDirectionWS.xyz);
    inputs.origin = scene.viewPositionWS.xyz;
    inputs.atmosphere = scene.atmosphereParams;
    inputs.clouds = scene.cloudParams;

    vec3 sky = skyBackground(inputs, rayDir);

    // Stylised sun marker in a saturated warm tone so it separates from a
    // pale sky after tonemapping. Suppressed in preview/aux scenes
    // (atmosphereParams.w), where a close orbit camera can fill the frame with it.
    if (scene.atmosphereParams.w < 0.5) {
        const vec3 kSunDiskColor = vec3(1.0, 0.75, 0.35);
        float sunDot = saturate(dot(rayDir, inputs.sunDir));
        sky += kSunDiskColor * pow(sunDot, 256.0) * 0.6;
        sky = mix(sky, kSunDiskColor * 2.2, smoothstep(0.9994, 0.9998, sunDot));
    }
    sky = compositeClouds(inputs, rayDir, sky);

    // Points at infinity: w = 0 drops camera translation, leaving the
    // rotation-only motion that sky pixels actually have.
    vec4 cur = scene.viewProjNoJitter * vec4(rayDir, 0.0);
    vec4 prev = scene.prevViewProjNoJitter * vec4(rayDir, 0.0);
    outVelocity = prev.w > 1e-4 ? (cur.xy / cur.w - prev.xy / prev.w) * 0.5 : vec2(0.0);
    outColor = vec4(sky, 1.0);
}

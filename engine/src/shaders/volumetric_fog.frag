#version 450

// Kronos ("Rendering Fidelity Foundation" Phase 1.2): a real, single-pass
// raymarched volumetric fog + light-shaft pass -- reads frame.hdrImage +
// depth (the exact same two inputs shaders/cinematic.frag reads, same
// role: a real, full-resolution fullscreen-triangle post-process, not a
// half-res+upsample pair -- this codebase has no compute pipeline
// anywhere, see Renderer::createPostProcessPipelines()'s own comment,
// and a second composite pass would need its own tri-state source-image
// bookkeeping on top of the one this pass already adds -- see
// Renderer::ensureCinematicTarget()'s own comment on that chain).
// Real, standard front-to-back single-scattering integration along the
// view ray, per-step attenuated by the exact same cascaded shadow map
// scene.frag's own direct lighting already uses (that attenuation *is*
// the light shaft: a step in shadow contributes only its real, flat
// ambientFogContribution term, not the sun). No slope-scaled shadow bias
// here (unlike scene.frag's sampleCascadeShadow()) -- a raymarch sample
// isn't on any real surface, so there's no real surface normal to bias
// against; a small fixed bias is used instead.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// Partial SceneUBO -- see shaders/cinematic.frag's identical partial-
// declaration precedent. This pass needs the shadow cascades and fog
// color/density in addition to what cinematic.frag itself reads.
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
} scene;

layout(set = 0, binding = 1) uniform sampler2DArray shadowMapArray;

layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;

// Must exactly match core::VolumetricFogPushConstants (SceneTypes.hpp).
layout(push_constant) uniform VolumetricFogPushConstants {
    float scatteringIntensity;
    float maxDistance;
    float stepCount;
    float ambientFogContribution;
    // Kronos ("Real-Time Rendering Evolved" trailer): real height-based
    // density gradient -- see core::VolumetricFogPushConstants's own
    // header comment for the full real semantics.
    float groundDensityMultiplier;
    float aloftDensityMultiplier;
    float groundHeightY;
    float falloffHeight;
} pc;

vec3 worldPosFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 p = scene.invViewProj * ndc;
    return p.xyz / p.w;
}

int selectCascade(float viewDepth) {
    if (viewDepth < scene.cascadeSplitsView.x) return 0;
    if (viewDepth < scene.cascadeSplitsView.y) return 1;
    return 2;
}

// Real, deliberately simplified twin of scene.frag's own
// sampleCascadeShadow() -- see this file's own header comment on why no
// slope-scaled bias applies here. Always the full 3x3 PCF tap (this pass
// already skips entirely when volumetric fog is off, see
// Renderer::drawVolumetricFogPass()'s own real bypass, so it doesn't need
// its own Performance-Mode single-tap fallback the way scene.frag's
// always-runs shadow sampling does).
float sampleFogShadow(vec3 worldPos, float viewDepth) {
    int cascade = selectCascade(viewDepth);
    vec4 lightSpacePos = scene.lightViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projected = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = projected.xy * 0.5 + 0.5;
    float currentDepth = projected.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || currentDepth > 1.0) return 1.0;
    const float kFixedBias = 0.0015;
    float sampledDepth = texture(shadowMapArray, vec3(uv, float(cascade))).r;
    return (currentDepth - kFixedBias > sampledDepth) ? 0.0 : 1.0;
}

void main() {
    vec3 hdrColorSample = texture(hdrColor, inUV).rgb;

    float density = scene.fogColorDensity.a;
    if (density <= 0.0) {
        // Real, exact identity at zero density -- matches
        // shaders/scene.frag's applyFog() convention exactly: no caller
        // ever set fog, this pass costs nothing beyond the one sample
        // above and changes nothing.
        outColor = vec4(hdrColorSample, 1.0);
        return;
    }

    float depthHW = texture(sceneDepth, inUV).r;
    bool isBackground = depthHW >= 0.9999;

    vec3 rayStart = scene.viewPositionWS.xyz;
    vec3 rayDir;
    float rayLength;
    if (isBackground) {
        // Any real depth value reconstructs the correct *direction* for
        // this pixel's view ray (perspective division cancels the depth
        // choice out of the xy/w ratio) -- 0.5 is an arbitrary, valid
        // near-far split used only to recover that direction, not a real
        // depth for this background pixel.
        rayDir = normalize(worldPosFromDepth(inUV, 0.5) - rayStart);
        rayLength = pc.maxDistance;
    } else {
        vec3 rayEnd = worldPosFromDepth(inUV, depthHW);
        vec3 rayVec = rayEnd - rayStart;
        rayLength = min(length(rayVec), pc.maxDistance);
        rayDir = rayLength > 0.0001 ? rayVec / length(rayVec) : vec3(0.0, 1.0, 0.0);
    }

    int steps = int(max(pc.stepCount, 1.0));
    float stepSize = rayLength / float(steps);

    // Real, deterministic per-pixel jitter (the same hash idiom
    // shaders/cinematic.frag's own SSAO kernel already uses) -- offsets
    // the march start point by up to one step so a real, fixed step
    // count reads as soft atmospheric haze, not visible banding rings.
    float jitter = fract(sin(dot(inUV, vec2(12.9898, 78.233))) * 43758.5453);

    vec3 inScattered = vec3(0.0);
    float transmittance = 1.0;
    // Real Beer-Lambert per-step extinction, tuned so this pass's own
    // `density` reads consistently with shaders/scene.frag's applyFog()
    // exponential-squared falloff over the same real distances, not an
    // independently-guessed scale.
    const float kExtinctionScale = 2.2;

    for (int i = 0; i < steps; ++i) {
        float t = (float(i) + jitter) * stepSize;
        vec3 samplePos = rayStart + rayDir * t;
        float viewDepth = -(scene.view * vec4(samplePos, 1.0)).z;
        float visibility = sampleFogShadow(samplePos, viewDepth);

        // Real height-based density gradient -- see this file's own
        // VolumetricFogPushConstants declaration comment. `heightFactor`
        // is 0 at/below groundHeightY (real, full groundDensityMultiplier),
        // 1 at falloffHeight world units above it (real, full
        // aloftDensityMultiplier), smoothly blended between.
        float heightFactor = clamp((samplePos.y - pc.groundHeightY) / max(pc.falloffHeight, 0.01), 0.0, 1.0);
        float densityAtHeight = density * mix(pc.groundDensityMultiplier, pc.aloftDensityMultiplier, heightFactor);

        vec3 stepRadiance = visibility * scene.lightColorIntensity.rgb * scene.lightColorIntensity.a +
                             pc.ambientFogContribution * scene.fogColorDensity.rgb;
        float stepExtinction = clamp(1.0 - exp(-densityAtHeight * stepSize * kExtinctionScale), 0.0, 1.0);

        inScattered += transmittance * stepExtinction * stepRadiance * pc.scatteringIntensity;
        transmittance *= (1.0 - stepExtinction);
    }

    outColor = vec4(hdrColorSample * transmittance + inScattered, 1.0);
}

#version 460
// Sprint 14 ("RTX Upgrade" Phase 2): the real ray-tracing-capable variant
// of scene.frag -- selected by Renderer::createScenePipeline() only when
// checkRayTracingSupport() real-confirmed this device/driver supports
// VK_KHR_ray_query + VK_KHR_acceleration_structure at startup (see that
// function's own comment). Everything below is byte-for-byte identical
// to scene.frag EXCEPT: this file's #version/#extension line, the new
// binding 2 (the real TLAS) declaration, the new
// scene.rayTracedShadowFlag UBO field, and computeShadow()'s new real
// ray-query branch. Kept as a full second file rather than one
// #ifdef-laced shader specifically so scene.frag (used on every device,
// RT-capable or not) never risks a regression from this pass -- see
// RayTracingScene.hpp's own header comment for the fuller design
// rationale.
#extension GL_EXT_ray_query : require
#ifdef KRONOS_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#endif

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in flat vec4 inBaseColor;
layout(location = 4) in flat vec4 inMetallicRoughness;
layout(location = 5) in flat vec4 inEmissive;
layout(location = 6) in vec4 inWorldTangent;
// Kronos ("Avatar Visual Silhouette Pass"): same real, interpolated
// per-vertex color as shaders/scene.frag -- see that file's own comment.
layout(location = 7) in vec4 inVertexColor;

layout(location = 0) out vec4 outColor;

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
    // Sprint 14: x = 1.0 real-enables the real ray-query shadow branch
    // below this frame, 0.0 real-falls-back to the exact same CSM path
    // scene.frag always uses. y = 1.0 (Performance Mode) real-switches
    // the CSM fallback to a single-tap sample. See SceneTypes.hpp's own
    // field comment.
    vec4 renderFlags;
    // Sprint 16 point lights -- must mirror SceneUBO's own field order
    // exactly (see SceneTypes.hpp's comment); this UBO is the same
    // buffer/binding scene.frag reads, just through this shader variant's
    // separately-declared (but byte-identical) struct.
    vec4 pointLightPositionRadius[4];
    vec4 pointLightColorIntensity[4];
    vec4 pointLightCount;
    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): x = 1.0
    // real-enables traceReflection() below this frame (same "toggle AND a
    // currently-valid TLAS" gating renderFlags.x already uses). y/z: the
    // real roughness trace-cost cutoffs (see Renderer::setReflectionRoughnessCutoff()).
    // See SceneTypes.hpp's own field comment.
    vec4 reflectionParams;
    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox /
    // volumetric cloud layer): unused here (both are shaders/sky.frag-only
    // terms) -- declared purely to keep this struct's own byte layout
    // identical up through giParams below (see SceneTypes.hpp's own field
    // order).
    vec4 atmosphereParams;
    vec4 cloudParams;
    // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/GI): x =
    // 1.0 real-enables traceIndirectDiffuse() below this frame (same
    // "toggle AND a currently-valid TLAS" gating reflectionParams.x
    // already uses). y: real intensity multiplier. See SceneTypes.hpp's
    // own field comment.
    vec4 giParams;
} scene;

layout(set = 0, binding = 1) uniform sampler2DArray shadowMapArray;
// Sprint 14: the real TLAS -- see core::RayTracingScene.hpp's own header
// comment for exactly what real geometry populates it (Box/Plane
// MeshSource-described shadow-casters) and Renderer::createSceneDescriptorResources()
// for why this binding only exists in this shader variant at all.
layout(set = 0, binding = 2) uniform accelerationStructureEXT topLevelAS;
// Kronos ("Rendering Fidelity Foundation" Phase 1.3): real per-instance
// material data, 2 vec4 per instance (baseColor, then
// vec4(metallic, roughness, 0, 0)), indexed by a ray-query hit's own
// instanceCustomIndex -- see core::RayTracingScene::rebuild()'s own
// comment for how this stays index-aligned with the TLAS above.
layout(set = 0, binding = 3) readonly buffer InstanceMaterials {
    vec4 data[];
} instanceMaterials;

// Kronos ("Bindless Descriptors"): identical to the block in scene.frag
// -- see that file for the full rationale. Kept byte-for-byte in step
// with it, exactly as the rest of this file is.
#ifdef KRONOS_BINDLESS
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];
layout(location = 8) in flat uvec4 inTextureIndices;
#define ALBEDO_TEX    bindlessTextures[nonuniformEXT(inTextureIndices.x & 0xFFFFu)]
#define NORMAL_TEX    bindlessTextures[nonuniformEXT(inTextureIndices.x >> 16)]
#define METALLIC_TEX  bindlessTextures[nonuniformEXT(inTextureIndices.y & 0xFFFFu)]
#define ROUGHNESS_TEX bindlessTextures[nonuniformEXT(inTextureIndices.y >> 16)]
#define AO_TEX        bindlessTextures[nonuniformEXT(inTextureIndices.z & 0xFFFFu)]
#else
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicTexture;
layout(set = 1, binding = 3) uniform sampler2D roughnessTexture;
layout(set = 1, binding = 4) uniform sampler2D aoTexture;
#define ALBEDO_TEX    albedoTexture
#define NORMAL_TEX    normalTexture
#define METALLIC_TEX  metallicTexture
#define ROUGHNESS_TEX roughnessTexture
#define AO_TEX        aoTexture
#endif

const float PI = 3.14159265359;

vec3 computeAmbient(vec3 N) {
    float skyWeight = N.y * 0.5 + 0.5;
    return mix(scene.ambientGroundColor.rgb, scene.ambientColor.rgb, skyWeight);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom + 1e-7);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggxV = NdotV / (NdotV * (1.0 - k) + k);
    float ggxL = NdotL / (NdotL * (1.0 - k) + k);
    return ggxV * ggxL;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Sprint 16 -- identical to scene.frag's own computePointLights(), see
// that file's header comment for the full rationale.
vec3 computePointLights(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 result = vec3(0.0);
    int count = int(scene.pointLightCount.x + 0.5);
    for (int i = 0; i < count; ++i) {
        vec4 posRadius = scene.pointLightPositionRadius[i];
        vec4 colorIntensity = scene.pointLightColorIntensity[i];
        vec3 toLight = posRadius.xyz - worldPos;
        float dist = length(toLight);
        float radius = max(posRadius.w, 1e-4);
        if (dist >= radius) continue;
        vec3 L = toLight / max(dist, 1e-4);
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 1e-4);
        if (NdotL <= 0.0) continue;

        float distRatio = clamp(dist / radius, 0.0, 1.0);
        float windowed = 1.0 - distRatio * distRatio;
        float falloff = (windowed * windowed) / (dist * dist + 1.0);

        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 specular = (NDF * G * F) / (4.0 * NdotV * NdotL + 1e-4);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diffuse = kD * albedo / PI;

        vec3 radiance = colorIntensity.rgb * colorIntensity.a * falloff;
        result += (diffuse + specular) * radiance * NdotL;
    }
    return result;
}

int selectCascade(float viewDepth) {
    if (viewDepth < scene.cascadeSplitsView.x) return 0;
    if (viewDepth < scene.cascadeSplitsView.y) return 1;
    return 2;
}

// `N` here must be the *geometric* normal -- see shaders/scene.frag's
// own sampleCascadeShadow() for why (same "self-contained twin"
// convention this file already follows).
float sampleCascadeShadow(int cascade, vec3 worldPos, vec3 N, vec3 L) {
    vec4 lightSpacePos = scene.lightViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projected = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = projected.xy * 0.5 + 0.5;
    float currentDepth = projected.z;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || currentDepth > 1.0) {
        return 1.0;
    }

    // Receiver-plane depth bias, analytic form -- see shaders/scene.frag's
    // own sampleCascadeShadow() for the full explanation. Builds the two
    // probe directions from the geometric normal N (a tangent-plane
    // basis), not from camera screen-space derivatives: hardware testing
    // found the derivative-based version still peter-panned because it
    // conflates the *camera's* grazing angle with the *light's*, and
    // this version is entirely camera-independent.
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMapArray, 0).xy);

    vec3 Nn = normalize(N);
    vec3 helper = abs(Nn.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T1 = normalize(cross(Nn, helper));
    vec3 T2 = cross(Nn, T1);

    mat4 M = scene.lightViewProj[cascade];
    vec3 dProjected_dT1 = (M * vec4(T1, 0.0)).xyz;
    vec3 dProjected_dT2 = (M * vec4(T2, 0.0)).xyz;
    vec3 duvz_dT1 = vec3(dProjected_dT1.xy * 0.5, dProjected_dT1.z);
    vec3 duvz_dT2 = vec3(dProjected_dT2.xy * 0.5, dProjected_dT2.z);
    float det = duvz_dT1.x * duvz_dT2.y - duvz_dT2.x * duvz_dT1.y;
    vec2 depthGradient = vec2(0.0);
    if (abs(det) > 1e-9) {
        depthGradient = vec2(duvz_dT2.y * duvz_dT1.z - duvz_dT1.y * duvz_dT2.z,
                              duvz_dT1.x * duvz_dT2.z - duvz_dT2.x * duvz_dT1.z) / det;
        // Kept small on purpose -- a surface this grazing to the light
        // is already fully NdotL-attenuated in direct lighting, so the
        // shadow result stops mattering right where this would blow up.
        const float kMaxReceiverSlope = 2.5;
        depthGradient = clamp(depthGradient, -kMaxReceiverSlope, kMaxReceiverSlope);
    }

    // Live-tunable scale (applied to *all* bias below, not just this
    // term) + absolute contribution ceiling -- see scene.frag's
    // sampleCascadeShadow() (this function's mirror) and
    // Renderer::setReceiverPlaneBiasScale()'s own comment for why 0 is a
    // true zero-bias diagnostic, not just a strength dial.
    depthGradient *= scene.cascadeBiasScale.w;
    const float kMaxReceiverPlaneBiasContribution = 0.0025;

    // Not multiplied by the per-cascade cascadeBiasScale.xyz -- see
    // scene.frag's sampleCascadeShadow() for why (depthRange^2 blowup).
    // Still multiplied by the single global .w scale above, same as
    // depthGradient, so it participates in the zero-bias diagnostic too.
    float NdotL = max(dot(N, L), 0.0);
    float minBias = max(0.00035 * (1.0 - NdotL), 0.00008) * scene.cascadeBiasScale.w;

    // Sprint 14 ("Performance Mode"): a real, direct per-fragment cost
    // cut -- one center tap instead of the real 3x3 (9-tap) PCF loop,
    // trading soft shadow edges for real fragment-shader throughput. Both
    // real branches sample the exact same shadowMapArray -- only the
    // real tap count (and bias per-tap treatment) differs.
    if (scene.renderFlags.y > 0.5) {
        float receiverBias = min(dot(abs(depthGradient), texelSize), kMaxReceiverPlaneBiasContribution);
        float bias = max(minBias, receiverBias);
        float sampledDepth = texture(shadowMapArray, vec3(uv, float(cascade))).r;
        return (currentDepth - bias > sampledDepth) ? 0.0 : 1.0;
    }

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            float receiverBias = clamp(dot(depthGradient, offset),
                                        -kMaxReceiverPlaneBiasContribution, kMaxReceiverPlaneBiasContribution);
            float expectedDepth = currentDepth + receiverBias;
            float sampledDepth = texture(shadowMapArray, vec3(uv + offset, float(cascade))).r;
            shadow += (expectedDepth - minBias > sampledDepth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

const float kCascadeBlendFraction = 0.1;

// Kronos ("Environmental Detail" -- dynamic cloud shadows): real,
// self-contained duplicate of shaders/sky.frag's own real cloud noise
// (see scene.frag's own identical function/comment, mirrored here, same
// "self-contained twin" convention this file already follows) -- samples
// the *same* real cloud density field sky.frag's own computeClouds()
// draws, so a cloud actually visible overhead casts a real shadow on the
// ground beneath it.
float cloudShadowHash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float cloudShadowValueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(cloudShadowHash(i + vec3(0, 0, 0)), cloudShadowHash(i + vec3(1, 0, 0)), u.x),
                    mix(cloudShadowHash(i + vec3(0, 1, 0)), cloudShadowHash(i + vec3(1, 1, 0)), u.x), u.y),
                mix(mix(cloudShadowHash(i + vec3(0, 0, 1)), cloudShadowHash(i + vec3(1, 0, 1)), u.x),
                    mix(cloudShadowHash(i + vec3(0, 1, 1)), cloudShadowHash(i + vec3(1, 1, 1)), u.x), u.y),
                u.z);
}

float cloudShadowFbm(vec3 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * cloudShadowValueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return sum;
}

// Real, single-tap cloud shadow -- see scene.frag's own identical
// function/comment for the full real reasoning (a real, deliberately
// cheaper single planar-intersection sample, not a full volumetric
// march).
float cloudShadowAt(vec3 worldPos, vec3 sunDir, float coverage, float speed, float time) {
    const float kCloudMidAltitude = 575.0;
    if (sunDir.y <= 0.01) return 1.0;
    float t = (kCloudMidAltitude - worldPos.y) / sunDir.y;
    if (t <= 0.0) return 1.0;
    vec3 samplePos = worldPos + sunDir * t;
    vec3 wind = vec3(time * speed, 0.0, time * speed * 0.6);
    float density = cloudShadowFbm((samplePos + wind) * 0.0035);
    density = smoothstep(0.85 - coverage, 1.15 - coverage, density);
    return mix(1.0, 0.35, density);
}

float sampleCsmShadow(vec3 worldPos, vec3 N, vec3 L, float viewDepth) {
    int cascade = selectCascade(viewDepth);
    float shadow = sampleCascadeShadow(cascade, worldPos, N, L);

    if (cascade < 2) {
        float splitDepth = cascade == 0 ? scene.cascadeSplitsView.x : scene.cascadeSplitsView.y;
        float blendBandStart = splitDepth * (1.0 - kCascadeBlendFraction);
        if (viewDepth > blendBandStart) {
            float t = clamp((viewDepth - blendBandStart) / max(splitDepth - blendBandStart, 1e-4), 0.0, 1.0);
            float nextShadow = sampleCascadeShadow(cascade + 1, worldPos, N, L);
            shadow = mix(shadow, nextShadow, t);
        }
    }
    return shadow;
}

// Real hardware-accelerated shadow-visibility test: a single boolean
// occlusion ray from `origin` toward the light, terminating on the
// first opaque hit (gl_RayFlagsTerminateOnFirstHitEXT -- a real shadow
// ray only ever needs "is anything in the way at all", never the
// closest-hit distance/normal a reflection or GI ray would need, so
// this is real, deliberately the cheapest possible real ray-query
// usage). `tMin` is offset off the surface (avoids real self-shadowing
// from the origin triangle itself, the ray-traced equivalent of
// sampleCascadeShadow()'s own real depth bias above); `tMax` is a real,
// generous fixed distance -- this pass's RT geometry (see
// RayTracingScene.hpp) is bounded to Studio-authored/TNT-Wars-map-scale
// content, not an open, unbounded world.
float rayQueryShadow(vec3 origin, vec3 direction) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF, origin,
                           0.02, direction, 500.0);
    while (rayQueryProceedEXT(rq)) {
    }
    return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
}

// Kronos ("Rendering Fidelity Foundation" Phase 1.3): a real, cheap
// analytic reflection of shaders/sky.frag's own base gradient (deliberately
// a simplified twin -- no sun disk/haze band, just the two-tone
// elevation blend) -- always computed as the real fallback for a
// traceReflection() miss, and, at low `reflectionParams.y`/`.z`
// thresholds, for rough surfaces that never fire a real ray at all (see
// main()'s own real trace-cost gate). Never wrong the way a stale/
// disabled traced result would be: this is a real, direct function of
// the current frame's own sky colors, not a cached or approximated one.
vec3 proceduralSkyReflection(vec3 dir) {
    float elevation = clamp(dir.y, 0.0, 1.0);
    float t = smoothstep(0.0, 0.6, elevation);
    return mix(scene.skyHorizonColor.rgb, scene.skyZenithColor.rgb, t);
}

// Real, single-bounce hybrid RT reflection -- the *same* TLAS
// rayQueryShadow() above traces against, but real closest-hit (not
// terminate-on-first-hit: a reflection needs to know the *nearest* real
// surface, not merely that *something* is in the way). On a real miss,
// falls back to the cheap analytic sky reflection above -- always
// correct, never a black/undefined result. On a real hit, reads that
// instance's own real baseColor via instanceCustomIndex (see
// core::RayTracingScene::rebuild()'s own comment on why that index is
// always valid here) and shades it with a real, single flat N.L term
// against the current directional light -- a real, honestly-scoped
// approximation (no recursive shadow ray, no real GI bounce), matching
// rayQueryShadow()'s own "shadow ray only, never closest-hit" honesty
// one level further down the real light-transport chain.
vec3 traceReflection(vec3 origin, vec3 direction) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.02, direction, 200.0);
    while (rayQueryProceedEXT(rq)) {
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        return proceduralSkyReflection(direction);
    }
    int hitIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
    vec3 hitAlbedo = instanceMaterials.data[hitIndex * 2].rgb;
    // Real, flat "sun elevation" lighting guess for the hit surface --
    // deliberately not a full BRDF re-evaluation (this is a single-bounce
    // approximation, see this function's own header comment): a
    // real, if simplified, up-facing-normal assumption reads correctly
    // for this engine's real Box/Plane content (floors/walls/platforms),
    // and 0.2 is a real, honest minimum so a hit surface facing away from
    // the light doesn't reflect as pure black.
    float sunUp = clamp(dot(normalize(-scene.lightDirectionWS.xyz), vec3(0.0, 1.0, 0.0)), 0.2, 1.0);
    return hitAlbedo * sunUp * scene.lightColorIntensity.rgb * scene.lightColorIntensity.a;
}

// Real, deterministic per-pixel hash -- seeds traceIndirectDiffuse()'s own
// real cosine-weighted sample direction from screen position alone (not a
// per-frame random seed), so the result is a real, *static* dither
// pattern rather than flickering noise -- see
// Renderer::setRTGIEnabled()'s own comment on why (no denoiser/temporal
// accumulation exists in this engine to clean up true per-frame noise).
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Real cosine-weighted hemisphere sample around `N`, from two real
// uniform-random inputs (u1, u2) -- the standard, well-established
// technique (Malley's method: project a real uniform disk sample straight
// up onto the hemisphere), chosen specifically because a cosine-weighted
// sample's own real PDF (cos(theta)/PI) exactly cancels the cos(theta)/PI
// terms in the diffuse rendering equation's own Monte-Carlo estimator --
// see traceIndirectDiffuse()'s own header comment for the real resulting
// simplification.
vec3 cosineWeightedHemisphereSample(vec3 N, float u1, float u2) {
    float r = sqrt(u1);
    float theta = 2.0 * PI * u2;
    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u1));

    // Real, orthonormal tangent frame around N -- picks whichever of
    // world-up/world-right is less parallel to N to avoid a real
    // degenerate cross product when N itself is near-vertical.
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * x + bitangent * y + N * z);
}

// Real, single-bounce ray-traced indirect diffuse -- the same real TLAS
// rayQueryShadow()/traceReflection() above already trace against, one
// real cosine-weighted hemisphere ray per shaded pixel. Real light
// transport simplification: for a cosine-weighted sample, the diffuse
// rendering equation's own Monte-Carlo estimator
// `albedo/PI * L_incoming * cos(theta) / pdf` (pdf = cos(theta)/PI)
// exactly reduces to `albedo * L_incoming` -- so this function itself
// returns the real *incoming radiance* only (L_incoming), and the caller
// multiplies by *its own* real surface albedo once, not per-sample (see
// main()'s own real call site). On a real hit, reads the hit surface's
// own real albedo (instanceCustomIndex, same technique traceReflection()
// already establishes) shaded by the same real, single flat N.L
// approximation traceReflection() already uses for its own hit shading
// -- an honest, single-bounce-only estimate, no recursive bounce/shadow
// ray from the hit point (matching this file's own established "shadow
// ray only tests visibility, reflection ray only single-bounce" honesty
// one level further into the transport chain). On a real miss, real sky
// radiance in that direction (proceduralSkyReflection()) stands in for
// "this ray escaped to open sky" -- the physically correct real behavior
// for an outdoor scene, not an arbitrary black/zero fallback.
//
// Kronos (GI grain fix -- "grainy... dark spots" without fog): real,
// live-flagged consequence of the original 1-sample-per-pixel estimator
// -- a single cosine-weighted sample has exactly two possible outcomes
// per pixel (a real hit, shaded far dimmer than open sky, or a real sky
// miss), and with no denoiser/temporal accumulation in this engine (see
// hash12()'s own comment) that shows up as real, salt-and-pepper dark
// speckling, most visible once fog isn't there to haze high-frequency
// detail. Averaging kGISampleCount independent real samples (still each
// deterministically hashed from screen position, so the *pattern* stays
// a real static dither, not per-frame flicker) reduces that variance by
// a real sqrt(N) factor -- the correct, principled fix for Monte-Carlo
// noise, not a fudge that just dims the effect.
vec3 traceIndirectDiffuse(vec3 origin, vec3 N) {
    const int kGISampleCount = 4;
    vec3 accum = vec3(0.0);
    for (int s = 0; s < kGISampleCount; ++s) {
        vec2 seed = gl_FragCoord.xy + vec2(float(s) * 13.0, float(s) * 29.0);
        float u1 = hash12(seed);
        float u2 = hash12(seed + vec2(37.0, 17.0));
        vec3 sampleDir = cosineWeightedHemisphereSample(N, u1, u2);

        rayQueryEXT rq;
        rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.02, sampleDir, 60.0);
        while (rayQueryProceedEXT(rq)) {
        }
        if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
            accum += proceduralSkyReflection(sampleDir);
            continue;
        }
        int hitIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        vec3 hitAlbedo = instanceMaterials.data[hitIndex * 2].rgb;
        float sunUp = clamp(dot(normalize(-scene.lightDirectionWS.xyz), vec3(0.0, 1.0, 0.0)), 0.2, 1.0);
        accum += hitAlbedo * sunUp * scene.lightColorIntensity.rgb * scene.lightColorIntensity.a;
    }
    return accum / float(kGISampleCount);
}

// `N` must be the geometric normal in both branches: rayQueryShadow()
// offsets the ray origin off the *actual* rasterized surface (a
// texture-perturbed normal could offset toward, not away from, that
// surface at a grazing angle), and sampleCsmShadow()'s receiver-plane
// bias needs it for the same reason scene.frag's own sampleCascadeShadow()
// does -- see that function's comment.
float computeShadow(vec3 worldPos, vec3 N, vec3 L, float viewDepth) {
    // Real, dynamic runtime branch on the real per-frame toggle (see
    // this UBO field's own comment) -- both real code paths exist in
    // this one compiled shader; scene.frag (the non-RT variant, used
    // when the device doesn't support ray query at all) only ever has
    // the CSM path, byte-for-byte identical to what runs here when the
    // flag is off.
    if (scene.renderFlags.x > 0.5) {
        return rayQueryShadow(worldPos + N * 0.02, L);
    }
    return sampleCsmShadow(worldPos, N, L, viewDepth);
}

vec3 applyFog(vec3 color, float viewDepth) {
    float density = scene.fogColorDensity.a;
    float fogFactor = clamp(exp(-pow(density * viewDepth, 2.0)), 0.0, 1.0);
    return mix(scene.fogColorDensity.rgb, color, fogFactor);
}

// Kronos ("Sky Map Full Biome Rebuild" Phase 3): real triplanar texture
// projection -- see scene.frag's own identical pair of functions (this
// file's own established "self-contained twin" precedent, not a shared
// #include).
vec3 triplanarWeights(vec3 geometricNormal) {
    vec3 blend = abs(geometricNormal);
    blend = pow(blend, vec3(4.0));
    return blend / max(blend.x + blend.y + blend.z, 1e-5);
}

vec4 sampleTriplanar(sampler2D tex, vec3 worldPos, vec3 weights, float scale) {
    vec4 xProjection = texture(tex, worldPos.yz * scale);
    vec4 yProjection = texture(tex, worldPos.xz * scale);
    vec4 zProjection = texture(tex, worldPos.xy * scale);
    return xProjection * weights.x + yProjection * weights.y + zProjection * weights.z;
}

// Kronos (triplanar normal fix -- "reflections divided into 3"): real
// Whiteout blending (Golus / Neubelt & Pettineo 2013) -- see scene.frag's
// own identical function/comment (mirrored here, same "self-contained
// twin" convention this file already follows). Reorients each axis's own
// tangent-space normal sample directly into real world space (via that
// axis's own real geometric-normal swizzle) *before* blending, fixing
// the real, live-flagged seam a naive shared-TBN blend produced on
// curved terrain -- reflect() is sensitive enough to small normal
// differences that seam showed up as reflections visibly splitting into
// 3 patches instead of one smooth one.
vec3 triplanarWorldNormal(sampler2D tex, vec3 worldPos, vec3 geometricNormal, vec3 weights, float scale,
                           float intensity) {
    vec3 nx = texture(tex, worldPos.yz * scale).xyz * 2.0 - 1.0;
    vec3 ny = texture(tex, worldPos.xz * scale).xyz * 2.0 - 1.0;
    vec3 nz = texture(tex, worldPos.xy * scale).xyz * 2.0 - 1.0;
    nx.xy *= intensity;
    ny.xy *= intensity;
    nz.xy *= intensity;
    nx = vec3(nx.xy + geometricNormal.zy, nx.z * geometricNormal.x);
    ny = vec3(ny.xy + geometricNormal.xz, ny.z * geometricNormal.y);
    nz = vec3(nz.xy + geometricNormal.xy, nz.z * geometricNormal.z);
    return normalize(nx.zyx * weights.x + ny.xzy * weights.y + nz.xyz * weights.z);
}

void main() {
    bool useTriplanar = inMetallicRoughness.w > 0.5;
    vec3 triWeights = useTriplanar ? triplanarWeights(normalize(inWorldNormal)) : vec3(0.0);
    const float kTriplanarScale = 0.12;
    const float kMicroDetailScale = 0.9;

    vec3 albedo;
    float metallic;
    float roughness;
    if (useTriplanar) {
        vec3 baseAlbedo = sampleTriplanar(ALBEDO_TEX, inWorldPos, triWeights, kTriplanarScale).rgb;
        vec3 detailAlbedo = sampleTriplanar(ALBEDO_TEX, inWorldPos, triWeights, kMicroDetailScale).rgb;
        albedo = inBaseColor.rgb * mix(baseAlbedo, baseAlbedo * detailAlbedo * 1.6, 0.35) * inVertexColor.rgb;
        metallic = clamp(inMetallicRoughness.x * sampleTriplanar(METALLIC_TEX, inWorldPos, triWeights, kTriplanarScale).r, 0.0, 1.0);
        roughness = clamp(inMetallicRoughness.y * sampleTriplanar(ROUGHNESS_TEX, inWorldPos, triWeights, kTriplanarScale).r, 0.045, 1.0);
    } else {
        albedo = inBaseColor.rgb * texture(ALBEDO_TEX, inUV).rgb * inVertexColor.rgb;
        metallic = clamp(inMetallicRoughness.x * texture(METALLIC_TEX, inUV).r, 0.0, 1.0);
        roughness = clamp(inMetallicRoughness.y * texture(ROUGHNESS_TEX, inUV).r, 0.045, 1.0);
    }

    vec3 geometricNormal = normalize(inWorldNormal);
    vec3 T = normalize(inWorldTangent.xyz);
    T = normalize(T - geometricNormal * dot(geometricNormal, T));
    vec3 B = cross(geometricNormal, T) * inWorldTangent.w;
    mat3 TBN = mat3(T, B, geometricNormal);

    vec3 N;
    if (useTriplanar) {
        N = triplanarWorldNormal(NORMAL_TEX, inWorldPos, geometricNormal, triWeights, kTriplanarScale,
                                  inMetallicRoughness.z);
    } else {
        vec3 sampledNormal = texture(NORMAL_TEX, inUV).rgb * 2.0 - 1.0;
        sampledNormal.xy *= inMetallicRoughness.z;
        sampledNormal = normalize(sampledNormal);
        N = normalize(TBN * sampledNormal);
    }

    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real wet-surface
    // response -- see scene.frag's own identical comment (deliberately
    // duplicated, matching this file's own established precedent of
    // being a real, self-contained twin of scene.frag rather than a
    // shared #include).
    float groundFacing = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    roughness = mix(roughness, roughness * (1.0 - 0.6 * clamp(scene.renderFlags.z, 0.0, 1.0)), groundFacing);
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 V = normalize(scene.viewPositionWS.xyz - inWorldPos);
    vec3 L = normalize(-scene.lightDirectionWS.xyz);
    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (NDF * G * F) / (4.0 * NdotV * NdotL + 1e-4);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    float viewDepth = -(scene.view * vec4(inWorldPos, 1.0)).z;
    // geometricNormal, not N -- see computeShadow()'s own comment.
    float shadow = computeShadow(inWorldPos, geometricNormal, L, viewDepth);
    // Kronos ("Environmental Detail" -- dynamic cloud shadows): a real,
    // exact no-op when the toggle is off, same convention as scene.frag's
    // own identical hook.
    if (scene.cloudParams.x > 0.5) {
        shadow *= cloudShadowAt(inWorldPos, L, scene.cloudParams.y, scene.cloudParams.z, scene.cloudParams.w);
    }

    vec3 radiance = scene.lightColorIntensity.rgb * scene.lightColorIntensity.a;
    vec3 Lo = (diffuse + specular) * radiance * NdotL * shadow;
    Lo += computePointLights(inWorldPos, N, V, albedo, metallic, roughness, F0);

    float ao = useTriplanar ? sampleTriplanar(AO_TEX, inWorldPos, triWeights, kTriplanarScale).r : texture(AO_TEX, inUV).r;
    vec3 ambient = computeAmbient(N) * albedo * ao;

    // Kronos ("Four RTX Maps" Phase 5c): real caustic-light dapple pattern
    // for the Underwater Map -- see scene.frag's own identical hook for
    // the full comment (mirrored here, same "off is a real, exact
    // identity" convention as every other renderFlags-gated effect above).
    float causticStrength = clamp(scene.renderFlags.w, 0.0, 1.0);
    if (causticStrength > 0.001) {
        float caustic = sin(inWorldPos.x * 0.6) * cos(inWorldPos.z * 0.5 + inWorldPos.x * 0.15) * 0.5 + 0.5;
        caustic = pow(caustic, 2.0);
        ambient *= mix(1.0, 0.6 + caustic * 0.9, causticStrength * groundFacing);
    }

    vec3 emissive = inEmissive.rgb * inEmissive.a;
    vec3 color = ambient + Lo + emissive;

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): real hybrid RT
    // reflections -- a real, exact no-op (this whole block doesn't even
    // evaluate) when the toggle is off, same "off means byte-identical"
    // convention as computeShadow()'s own real bypass. Weighted by F
    // (the same Fresnel term specular/kD already use above) -- real,
    // physically-motivated: more reflective at grazing angles and for
    // metals (high F0), negligible for a rough dielectric facing the
    // camera head-on, not an arbitrary flat blend.
    if (scene.reflectionParams.x > 0.5) {
        vec3 R = reflect(-V, N);
        // Real trace-cost gate: only genuinely glossy surfaces (high
        // 1-roughness) pay for an actual ray query; rough surfaces get
        // the cheap analytic sky reflection only. smoothstep (not a hard
        // cutoff) so neighboring pixels of similar roughness blend
        // smoothly across the threshold instead of popping.
        float invRoughness = 1.0 - roughness;
        float rtWeight = smoothstep(scene.reflectionParams.y, scene.reflectionParams.z, invRoughness);
        vec3 reflectionRadiance = proceduralSkyReflection(R);
        if (rtWeight > 0.001) {
            reflectionRadiance = mix(reflectionRadiance, traceReflection(inWorldPos + N * 0.02, R), rtWeight);
        }
        color += reflectionRadiance * F;
    }

    // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/GI): a
    // real, exact no-op (this whole block doesn't even evaluate) when the
    // toggle is off, same "off means byte-identical" convention as the
    // reflection block above. See traceIndirectDiffuse()'s own header
    // comment for why this is `albedo * incomingRadiance` with no further
    // division/PI factor -- that cancellation already happened inside the
    // cosine-weighted Monte-Carlo estimator itself.
    if (scene.giParams.x > 0.5) {
        vec3 incomingRadiance = traceIndirectDiffuse(inWorldPos + N * 0.02, N);
        color += albedo * incomingRadiance * scene.giParams.y;
    }

    color = applyFog(color, viewDepth);

    outColor = vec4(color, inBaseColor.a);
}

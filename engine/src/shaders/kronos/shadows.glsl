#ifndef KRONOS_SHADOWS_GLSL
#define KRONOS_SHADOWS_GLSL

// Cascaded sun shadows. Requires scene_resources.glsl.
//
// Filtering: receiver-plane depth bias + normal offset keep every tap on
// the receiver's own plane; PCSS estimates the penumbra from the average
// blocker depth converted to world units (cascadeDepthRange), so the
// softness is identical in every cascade and contact-hardens naturally.
// Taps come from a Vogel disk rotated per pixel with interleaved gradient
// noise, which TAA integrates into a smooth result.

#include "common.glsl"

const float kCascadeBlendFraction = 0.12;
const int kPcfTaps = 12;
const int kPcssBlockerTaps = 16;
const int kPcssFilterTaps = 24;
const float kMaxFilterTexels = 24.0;
const float kNormalOffsetTexels = 1.4;

int selectCascade(float viewDepth) {
    for (int i = 0; i < KRONOS_CASCADE_COUNT - 1; ++i) {
        if (viewDepth < scene.cascadeSplitsView[i]) return i;
    }
    return KRONOS_CASCADE_COUNT - 1;
}

vec2 vogelDiskSample(int index, int count, float phi) {
    const float kGoldenAngle = 2.39996323;
    float r = sqrt((float(index) + 0.5) / float(count));
    float theta = float(index) * kGoldenAngle + phi;
    return r * vec2(cos(theta), sin(theta));
}

// d(depth)/d(uv) of the receiver plane in shadow space, derived
// analytically from the geometric normal rather than screen derivatives,
// so it is stable at silhouettes and under minification.
vec2 receiverPlaneDepthGradient(mat4 lightViewProj, vec3 N) {
    vec3 T1, T2;
    orthonormalBasis(N, T1, T2);
    vec3 d1 = (lightViewProj * vec4(T1, 0.0)).xyz;
    vec3 d2 = (lightViewProj * vec4(T2, 0.0)).xyz;
    vec3 a = vec3(d1.xy * 0.5, d1.z);
    vec3 b = vec3(d2.xy * 0.5, d2.z);
    float det = a.x * b.y - b.x * a.y;
    if (abs(det) < 1e-9) return vec2(0.0);
    vec2 g = vec2(b.y * a.z - a.y * b.z, a.x * b.z - b.x * a.z) / det;
    const float kMaxReceiverSlope = 2.5;
    return clamp(g, -kMaxReceiverSlope, kMaxReceiverSlope);
}

float shadowCompare(vec2 uv, float depth, int cascade) {
    return texture(shadowMapCompare, vec4(uv, float(cascade), depth));
}

float sampleCascadeShadow(int cascade, vec3 worldPos, vec3 Ngeo, float NoL, float noise) {
    float texelWorld = scene.cascadeTexelWorld[cascade];
    float sinTheta = sqrt(saturate(1.0 - NoL * NoL));
    vec3 offsetPos = worldPos + Ngeo * (texelWorld * kNormalOffsetTexels * sinTheta);

    vec4 lightClip = scene.lightViewProj[cascade] * vec4(offsetPos, 1.0);
    vec3 p = lightClip.xyz / lightClip.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    float depth = min(p.z, 1.0);

    float resolution = scene.shadowParams.z;
    float texel = 1.0 / resolution;
    vec2 gradient = receiverPlaneDepthGradient(scene.lightViewProj[cascade], Ngeo) * scene.shadowParams.x;
    const float kMaxPlaneBias = 0.004;
    float constantBias = texel * 0.5 * dot(abs(gradient), vec2(1.0)) + 2e-5;
    float filterMode = scene.shadowParams.w;

    if (filterMode < 0.5 || scene.renderFlags.y > 0.5) {
        return shadowCompare(uv, depth - min(constantBias, kMaxPlaneBias), cascade);
    }

    float phi = noise * 2.0 * PI;
    float radiusTexels = 1.5;

    if (filterMode > 1.5) {
        // Blocker search over the largest penumbra we are willing to draw.
        float cascadeWorldSize = texelWorld * resolution;
        float searchRadiusUV = kMaxFilterTexels * texel;
        float blockerSum = 0.0;
        float blockerCount = 0.0;
        for (int i = 0; i < kPcssBlockerTaps; ++i) {
            vec2 offset = vogelDiskSample(i, kPcssBlockerTaps, phi) * searchRadiusUV;
            float expected = depth + clamp(dot(gradient, offset), -kMaxPlaneBias, kMaxPlaneBias) - constantBias;
            float d = texture(shadowMapDepth, vec3(uv + offset, float(cascade))).r;
            if (d < expected) {
                blockerSum += d;
                blockerCount += 1.0;
            }
        }
        if (blockerCount < 0.5) return 1.0;
        float avgBlocker = blockerSum / blockerCount;
        float blockerDistanceWorld = max(depth - avgBlocker, 0.0) * scene.cascadeDepthRange[cascade];
        float penumbraWorld = 2.0 * blockerDistanceWorld * scene.shadowParams.y;
        radiusTexels = clamp(penumbraWorld / cascadeWorldSize * resolution * 0.5, 1.0, kMaxFilterTexels);
    }

    int taps = filterMode > 1.5 ? kPcssFilterTaps : kPcfTaps;
    float radiusUV = radiusTexels * texel;
    float sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        vec2 offset = vogelDiskSample(i, taps, phi) * radiusUV;
        float expected = depth + clamp(dot(gradient, offset), -kMaxPlaneBias, kMaxPlaneBias) - constantBias;
        sum += shadowCompare(uv + offset, expected, cascade);
    }
    return sum / float(taps);
}

// Spot shadows use the same Vogel-disk PCF as the cascades. The bias is
// applied in world space before projecting: a normal offset plus a pull
// toward the light, both sized to the texel footprint at the receiver's
// distance, which a perspective map makes grow linearly with range.
float spotShadow(int slot, vec3 lightPos, vec3 lightDir, vec3 worldPos, vec3 Ngeo, vec3 L, float noise) {
    float axialDistance = max(dot(worldPos - lightPos, lightDir), 1e-3);
    float texelWorld = scene.spotShadowTexelScale[slot] * axialDistance;
    float NoL = saturate(dot(Ngeo, L));
    float sinTheta = sqrt(1.0 - NoL * NoL);
    vec3 offsetPos = worldPos + Ngeo * (texelWorld * kNormalOffsetTexels * sinTheta) + L * (texelWorld * 0.5);

    vec4 lightClip = scene.spotShadowViewProj[slot] * vec4(offsetPos, 1.0);
    if (lightClip.w <= 0.0) return 1.0;
    vec3 p = lightClip.xyz / lightClip.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || p.z >= 1.0) return 1.0;

    if (scene.renderFlags.y > 0.5) return texture(spotShadowMaps, vec4(uv, float(slot), p.z));

    float radiusUV = 1.5 / float(textureSize(spotShadowMaps, 0).x);
    float phi = noise * 2.0 * PI;
    float sum = 0.0;
    for (int i = 0; i < kPcfTaps; ++i) {
        vec2 offset = vogelDiskSample(i, kPcfTaps, phi) * radiusUV;
        sum += texture(spotShadowMaps, vec4(uv + offset, float(slot), p.z));
    }
    return sum / float(kPcfTaps);
}

float cascadedShadow(vec3 worldPos, vec3 Ngeo, float NoL, float viewDepth, float noise) {
    int cascade = selectCascade(viewDepth);
    if (viewDepth > scene.cascadeSplitsView[KRONOS_CASCADE_COUNT - 1]) return 1.0;
    float shadow = sampleCascadeShadow(cascade, worldPos, Ngeo, NoL, noise);

    if (cascade < KRONOS_CASCADE_COUNT - 1) {
        float splitFar = scene.cascadeSplitsView[cascade];
        float splitNear = cascade == 0 ? 0.0 : scene.cascadeSplitsView[cascade - 1];
        float bandStart = splitFar - (splitFar - splitNear) * kCascadeBlendFraction;
        if (viewDepth > bandStart) {
            float t = smoothstep(bandStart, splitFar, viewDepth);
            shadow = mix(shadow, sampleCascadeShadow(cascade + 1, worldPos, Ngeo, NoL, noise), t);
        }
    } else {
        // Fade the last cascade out instead of cutting to fully lit.
        float farEnd = scene.cascadeSplitsView[cascade];
        float fadeStart = farEnd * 0.9;
        shadow = mix(shadow, 1.0, smoothstep(fadeStart, farEnd, viewDepth));
    }
    return shadow;
}

#endif

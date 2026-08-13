#version 450

// Sprint 16 ("Cinematic Graphics"): a single consolidated full-screen
// post-process pass combining screen-space ambient occlusion (SSAO),
// depth-of-field (circle-of-confusion blur), and camera-based motion blur
// -- three separate named deliverables in the brief, deliberately built as
// one real pipeline instead of three near-duplicate ones, since all three
// need the exact same inputs (the just-rendered HDR color + depth
// buffers) and none of them need each other's *output* as an input (no
// real ordering dependency forces them apart). Runs between the opaque/
// instanced/particle scene pass and bloom_extract/composite -- see
// Renderer::drawCinematicPass()'s own comment for exactly where it slots
// into the frame, and Renderer::setCinematicMode() for the real, direct
// bypass (this whole pass doesn't even record, let alone run, when
// Cinematic Mode is off -- the existing frame.hdrImage flows straight
// into bloom_extract instead, byte-for-byte the pre-Sprint-16 path).
//
// No G-buffer exists in this renderer (a single forward opaque pass, see
// scene.frag's own header comment) -- SSAO here derives its per-pixel
// normal from screen-space derivatives of a depth-reconstructed world
// position (dFdx/dFdy) rather than sampling a stored normal target, and
// the AO result multiplies the *entire* lit pixel (direct + indirect),
// not just the ambient term the way a deferred renderer's AO pass
// normally would -- a real, honestly-documented simplification of what a
// G-buffer-backed AO pass would do, not a different algorithm in
// disguise.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// Partial SceneUBO -- only the prefix this pass actually reads (through
// viewPositionWS); see shaders/sky.frag's identical partial-declaration
// precedent and SceneTypes.hpp's SceneUBO comment for why a prefix match
// is enough under std140 (trailing, undeclared members don't need to
// appear here at all).
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
} scene;

layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;

// Must exactly match core::CinematicPushConstants (SceneTypes.hpp).
layout(push_constant) uniform CinematicPushConstants {
    mat4 previousViewProj;
    float focusDistance;
    float focusRange;
    float maxCoCRadiusPx;
    float motionBlurStrength;
    float ssaoRadius;
    float ssaoStrength;
    float dofEnabled;
    float ssaoEnabled;
    float heatDistortionStrength;
    float time;
} pc;

// Cheap, texture-free pseudo-random hash -- rotates the SSAO kernel
// per-pixel so the fixed 8-sample kernel reads as noise, not banding,
// without needing a separate noise texture asset (none exist in this
// renderer, see core/Texture.cpp's own header note on real asset scope).
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 worldPosFromDepth(vec2 uv, float depth) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 p = scene.invViewProj * ndc;
    return p.xyz / p.w;
}

// Fixed hemisphere kernel (unit-radius, biased toward the origin so more
// samples land close to the surface, the standard SSAO sample
// distribution) -- see computeSSAO() below.
const int kSsaoKernelSize = 8;
const vec3 kSsaoKernel[8] = vec3[](
    vec3( 0.045, -0.088,  0.062), vec3(-0.081,  0.035,  0.147),
    vec3( 0.112,  0.098,  0.241), vec3(-0.152, -0.117,  0.351),
    vec3( 0.061,  0.211,  0.462), vec3(-0.243,  0.089,  0.573),
    vec3( 0.189, -0.231,  0.681), vec3(-0.087, -0.199,  0.792)
);

float computeSSAO(vec2 uv, vec3 worldPos, mat4 viewProj) {
    vec3 worldNormal = normalize(cross(dFdx(worldPos), dFdy(worldPos)));
    if (dot(worldNormal, scene.viewPositionWS.xyz - worldPos) < 0.0) {
        worldNormal = -worldNormal;
    }

    float randomAngle = hash12(uv) * 6.28318530718;
    vec3 rvec = vec3(cos(randomAngle), sin(randomAngle), 0.0);
    vec3 tangent = normalize(rvec - worldNormal * dot(rvec, worldNormal));
    vec3 bitangent = cross(worldNormal, tangent);
    mat3 tbn = mat3(tangent, bitangent, worldNormal);

    float occlusion = 0.0;
    for (int i = 0; i < kSsaoKernelSize; ++i) {
        vec3 sampleWorld = worldPos + (tbn * kSsaoKernel[i]) * pc.ssaoRadius;
        vec4 sampleClip = viewProj * vec4(sampleWorld, 1.0);
        sampleClip.xyz /= sampleClip.w;
        vec2 sampleUV = sampleClip.xy * 0.5 + 0.5;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        float storedDepthHW = texture(sceneDepth, sampleUV).r;
        vec3 storedWorld = worldPosFromDepth(sampleUV, storedDepthHW);

        float sampleDist = length(sampleWorld - scene.viewPositionWS.xyz);
        float storedDist = length(storedWorld - scene.viewPositionWS.xyz);
        float rangeCheck = smoothstep(0.0, 1.0, pc.ssaoRadius / max(abs(sampleDist - storedDist), 1e-4));
        occlusion += (storedDist <= sampleDist - 0.02) ? rangeCheck : 0.0;
    }
    return clamp(1.0 - (occlusion / float(kSsaoKernelSize)), 0.0, 1.0);
}

vec3 blurDisk(vec2 uv, float radiusPx) {
    vec2 texel = 1.0 / vec2(textureSize(hdrColor, 0));
    vec3 sum = texture(hdrColor, uv).rgb;
    float weight = 1.0;
    const int kTaps = 8;
    for (int i = 0; i < kTaps; ++i) {
        float angle = (float(i) / float(kTaps)) * 6.28318530718;
        vec2 offset = vec2(cos(angle), sin(angle)) * radiusPx * texel;
        sum += texture(hdrColor, uv + offset).rgb;
        weight += 1.0;
    }
    return sum / weight;
}

vec3 blurDirectional(vec2 uv, vec2 velocityUV) {
    const int kTaps = 8;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < kTaps; ++i) {
        float t = (float(i) / float(kTaps - 1)) - 0.5;
        vec2 sampleUV = clamp(uv + velocityUV * t, vec2(0.0), vec2(1.0));
        sum += texture(hdrColor, sampleUV).rgb;
    }
    return sum / float(kTaps);
}

void main() {
    // Real heat-haze shimmer (Volcano Map, Kronos "Four RTX Maps" Phase
    // 5b): displaces only the *color* sample's own UV, via two
    // independently-scrolling hash12() lookups (one per axis, offset by a
    // constant so they don't correlate) -- depth/worldPos below still read
    // the real, undistorted inUV, so SSAO/DOF/motion blur keep operating
    // on real geometry; only the final visible color wavers, matching how
    // real heat haze bends light without moving the object it's in front
    // of. A real, exact identity when heatDistortionStrength is 0 (every
    // map that isn't Volcano).
    vec2 colorUV = inUV;
    if (pc.heatDistortionStrength > 0.0005) {
        float n1 = hash12(inUV * 9.0 + vec2(pc.time * 0.55, 0.0));
        float n2 = hash12(inUV * 9.0 + vec2(0.0, pc.time * 0.55) + 31.7);
        colorUV = clamp(inUV + (vec2(n1, n2) - 0.5) * pc.heatDistortionStrength, vec2(0.001), vec2(0.999));
    }

    float depth = texture(sceneDepth, inUV).r;
    vec3 color = texture(hdrColor, colorUV).rgb;

    // depth >= ~1.0 is the far plane -- nothing was drawn there this
    // frame (sky.frag's own background pass, or simply outside every
    // model's bounds) -- see this file's header comment on scope: none of
    // SSAO/DOF/motion blur make sense applied to "no real surface here".
    bool isBackground = depth >= 0.9999;
    if (isBackground) {
        outColor = vec4(color, 1.0);
        return;
    }

    vec3 worldPos = worldPosFromDepth(inUV, depth);

    if (pc.ssaoEnabled > 0.5) {
        mat4 viewProj = scene.proj * scene.view;
        float ao = computeSSAO(inUV, worldPos, viewProj);
        color *= mix(1.0, ao, clamp(pc.ssaoStrength, 0.0, 1.0));
    }

    if (pc.dofEnabled > 0.5) {
        float viewDist = length(worldPos - scene.viewPositionWS.xyz);
        float coc = clamp(abs(viewDist - pc.focusDistance) / max(pc.focusRange, 1e-4), 0.0, 1.0);
        float radiusPx = coc * pc.maxCoCRadiusPx;
        if (radiusPx > 0.5) {
            color = blurDisk(colorUV, radiusPx);
        }
    }

    if (pc.motionBlurStrength > 0.001) {
        vec4 prevClip = pc.previousViewProj * vec4(worldPos, 1.0);
        prevClip.xyz /= prevClip.w;
        vec2 prevUV = prevClip.xy * 0.5 + 0.5;
        // Clamped so a first-frame/camera-cut/no-real-previous-VP
        // discontinuity produces a bounded, real blur instead of an
        // unbounded runaway smear across the whole image.
        const float kMaxMotionBlurUV = 0.05;
        vec2 velocity = clamp((colorUV - prevUV) * pc.motionBlurStrength, vec2(-kMaxMotionBlurUV), vec2(kMaxMotionBlurUV));
        if (length(velocity) > 0.0005) {
            color = blurDirectional(colorUV, velocity);
        }
    }

    outColor = vec4(color, 1.0);
}

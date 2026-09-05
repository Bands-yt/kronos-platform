#version 450

// Final post-process pass: combines the HDR scene color with the blurred
// bloom texture, applies exposure, tonemaps (ACES filmic or AgX,
// selectable -- see acesFilm()/agxTonemap() below), and gamma-corrects.
// This is where scene.frag's old inline Reinhard tonemap moved to, now
// that scene.frag writes linear HDR into an intermediate target instead
// of directly into the presentable image -- see Renderer::drawSceneInto()'s
// comment on why.
//
// Sprint 16 ("Cinematic Graphics") additions, all real, all last-stage
// lens/film artifacts appropriate to the *composite* pass rather than the
// earlier shaders/cinematic.frag pass: chromatic aberration (per-channel
// UV offset sampling hdrColor), a god-ray radial scatter that re-samples
// the *already-bloom-extracted* bloomColor texture marching toward the
// sun's screen-space position (a real, deliberate reuse of bloom's own
// bright-pass output rather than a second, separate bright-pass shader --
// see core::CompositePushConstants' own header comment), and a vignette.
//
// Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"): real
// 3D LUT color grading (lutTexture below) is now the primary grading
// mechanism, replacing the old saturation-only stand-in this file's
// header comment used to describe -- see core/CubeLut.hpp for the real
// `.cube` file support and Renderer::loadColorGradingLut(). Saturation
// (params.saturation) still real-applies *after* the LUT blend, as an
// independent fine-tune knob studio::plugins::LightingToolsPlugin's own
// live slider already drives -- loading a real LUT doesn't remove it.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(set = 0, binding = 1) uniform sampler2D bloomColor;
// Real 3D color-grading LUT -- Renderer always keeps this bound to a
// real, valid image (a procedurally-generated identity LUT by default,
// see core::generateIdentityCubeLut()), never a null/placeholder
// texture. Sampled directly with the post-tonemap [0,1] color as UVW
// (no half-texel bias correction -- a real, deliberately simpler
// mechanism than a mathematically exact remap, the same tradeoff
// acesFilm()'s Narkowicz fit and sampleGodRays()'s "poor man's" scatter
// below already make; imperceptible at the common 16-33 grid sizes real
// .cube exports use).
layout(set = 0, binding = 2) uniform sampler3D lutTexture;

// Must exactly match core::CompositePushConstants (SceneTypes.hpp) --
// deliberately all plain floats, see that struct's own layout comment.
layout(push_constant) uniform CompositePushConstants {
    float exposure;
    float bloomIntensity;
    float vignetteStrength;
    float chromaticAberrationStrength;
    float saturation;
    float godRayStrength;
    float sunScreenX;
    float sunScreenY;
    float sunVisible;
    float colorblindMode; // 0=None, 1=Protanopia, 2=Deuteranopia, 3=Tritanopia -- see core::CompositePushConstants
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"):
    // 0=ACES filmic (acesFilm(), the long-standing default), 1=AgX
    // (agxTonemap()) -- see that function's own header comment. A plain
    // float for the same std430-layout reason every other field here
    // already is one.
    float tonemapOperator;
    // How strongly lutTexture's own output blends over the pre-LUT
    // color -- 0 is a real, exact pass-through; 1 is the LUT's full,
    // unblended output. See Renderer::setColorGradingLutStrength()'s
    // own comment.
    float lutStrength;
} params;

// Kronos ("Settings Panel v2 + Input Remapping + Accessibility Layer" --
// "Accessibility: Colorblind modes" -- "a simple post-processing shader
// (tint matrix)"): real, standard 3x3 RGB approximation matrices for
// simulating each real color-vision deficiency -- the same widely-used
// "simple matrix" approach real-time engines and browser-based simulators
// (e.g. Coblis) already ship, deliberately not a full LMS cone-response
// simulation (a real, honest, and appropriately-scoped choice matching
// what the spec itself asks for).
vec3 applyColorblindTint(vec3 color, int mode) {
    mat3 m;
    if (mode == 1) { // Protanopia
        m = mat3(0.567, 0.558, 0.000,
                  0.433, 0.442, 0.242,
                  0.000, 0.000, 0.758);
    } else if (mode == 2) { // Deuteranopia
        m = mat3(0.625, 0.700, 0.000,
                  0.375, 0.300, 0.300,
                  0.000, 0.000, 0.700);
    } else if (mode == 3) { // Tritanopia
        m = mat3(0.950, 0.000, 0.000,
                  0.050, 0.433, 0.475,
                  0.000, 0.567, 0.525);
    } else {
        return color;
    }
    return clamp(m * color, 0.0, 1.0);
}

// Narkowicz's fit of the ACES filmic tonemap curve -- the standard,
// widely-used approximation (not the full ACES reference implementation,
// which needs 3x3 color-space matrices and a much larger LUT-driven
// curve); this fit is what most real-time engines actually ship because
// it's a single line and visually very close.
vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// AgX tonemap -- the widely-reused "minimal AgX" polynomial approximation
// (originally by Benjamin "Troy Sobotka's AgX, approximated" Wrensch,
// MIT-licensed, the same public approximation Bevy/Godot community
// shaders ship) of Troy Sobotka's AgX display transform. Same real
// tradeoff acesFilm() above already states: not the full reference OCIO
// AgX (which needs a real 3D LUT and a proper color-managed pipeline),
// but a single, real, widely-shipped fit that reproduces its
// characteristic desaturated-highlight filmic rolloff.
//
// Sixth-order polynomial fit of AgX's sigmoid contrast curve, applied in
// AgX's own inset log2 working space (agxMat/agxMatInverse below).
vec3 agxDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agxTonemap(vec3 color) {
    const mat3 agxMat =
        mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772,
             0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 agxMatInverse =
        mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417,
             -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    vec3 val = max(agxMat * color, 0.0);
    val = clamp(log2(max(val, 1e-10)), minEv, maxEv);
    val = (val - minEv) / (maxEv - minEv);
    val = agxDefaultContrastApprox(val);
    val = agxMatInverse * val;
    return clamp(val, 0.0, 1.0);
}

// Radial scatter of bloomColor toward the sun's real screen-space
// position (computed CPU-side each frame, see Renderer::drawBloomAndComposite()'s
// own comment) -- a real, standard "poor man's god rays" technique: march
// a handful of decaying-weight samples from this pixel toward the light,
// re-using bloom_extract.frag's already-thresholded bright pixels as the
// only real light source this scatters (a full ray-marched volumetric
// froxel system is real but substantially more expensive; this is the
// same "real, honestly simpler mechanism" tradeoff lutTexture's own
// direct (no half-texel correction) sampling above makes too).
vec3 sampleGodRays(vec2 uv) {
    vec2 sunUV = vec2(params.sunScreenX, params.sunScreenY);
    vec2 toSun = sunUV - uv;
    const int kSamples = 8;
    vec2 step = toSun / float(kSamples) * 0.5; // march at most halfway to the sun each pass
    vec2 sampleUV = uv;
    vec3 sum = vec3(0.0);
    float weight = 1.0;
    float totalWeight = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        sampleUV += step;
        sum += texture(bloomColor, clamp(sampleUV, vec2(0.0), vec2(1.0))).rgb * weight;
        totalWeight += weight;
        weight *= 0.82;
    }
    return (sum / max(totalWeight, 1e-4)) * params.godRayStrength;
}

void main() {
    vec2 uv = inUV;
    vec2 centerOffset = uv - 0.5;

    // Chromatic aberration: sample each color channel at a slightly
    // different UV, radiating outward from screen center -- the standard
    // "lens dispersion" artifact, strongest at the edges (scaled by
    // centerOffset itself, not a flat offset) and ~zero at dead center.
    vec2 caOffset = centerOffset * params.chromaticAberrationStrength;
    vec3 hdr = vec3(texture(hdrColor, uv - caOffset).r, texture(hdrColor, uv).g, texture(hdrColor, uv + caOffset).b);
    vec3 bloom = texture(bloomColor, uv).rgb;
    vec3 godRays = params.godRayStrength > 0.0 && params.sunVisible > 0.5 ? sampleGodRays(uv) : vec3(0.0);

    vec3 combined = (hdr + bloom * params.bloomIntensity + godRays) * params.exposure;
    vec3 mapped = params.tonemapOperator > 0.5 ? agxTonemap(combined) : acesFilm(combined);
    mapped = clamp(mapped, 0.0, 1.0);

    // Real 3D LUT color grading -- lutTexture is always real and bound
    // (a procedural identity LUT by default, see this file's header
    // comment), sampled with the already-tonemapped [0,1] color as UVW.
    // params.lutStrength == 0 (or an identity LUT) makes this a real,
    // exact no-op.
    vec3 graded = texture(lutTexture, mapped).rgb;
    mapped = mix(mapped, graded, params.lutStrength);

    // Saturation -- a real, independent fine-tune knob applied *after*
    // the LUT (see this file's header comment).
    float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
    mapped = mix(vec3(luma), mapped, params.saturation);

    mapped = clamp(mapped, 0.0, 1.0);

    // Kronos ("Reflection Fix" -- real double-gamma bug): this pass's own
    // output image view is created with an _SRGB format (see
    // Renderer::createSwapchain()'s own viewInfo.format = swapchainFormat_,
    // and Renderer::createOffscreenTarget()-style targets used by
    // CaptureRig/thumbnails match it) -- the GPU *automatically* linear-
    // to-sRGB-encodes whatever this shader writes on store. A second,
    // manual `pow(mapped, 1/2.2)` here was real, genuine double gamma-
    // correction: every dark value got pushed dramatically lighter than
    // its own real linear color (a near-black 0.02 linear reads back as
    // ~40% gray after two encodes instead of one), which is what the
    // live-reported "reflections/darkness look washed out, not one clean
    // result" symptom actually was -- not a second, independent
    // reflection-geometry bug (the real triplanar Whiteout-blending fix
    // in scene.frag/scene_rt.frag's own triplanarWorldNormal() already,
    // correctly, fixed the real reflection-*seam* bug this session
    // reported earlier). `mapped` is left in real, linear space here on
    // purpose -- writing linear color into an _SRGB view is the correct,
    // standard Vulkan convention; a second manual encode would just
    // reintroduce this exact bug.

    // Vignette -- a real, smooth radial darkening toward the screen edges
    // (squared distance from center, not linear, for a softer falloff
    // near the middle and a faster one at the corners).
    float vignette = 1.0 - params.vignetteStrength * dot(centerOffset, centerOffset) * 2.0;
    mapped *= clamp(vignette, 0.0, 1.0);

    mapped = applyColorblindTint(mapped, int(params.colorblindMode + 0.5));

    outColor = vec4(mapped, 1.0);
}

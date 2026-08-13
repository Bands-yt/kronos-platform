#version 450

// Final post-process pass: combines the HDR scene color with the blurred
// bloom texture, applies exposure, tonemaps (ACES filmic -- see acesFilm()
// below), and gamma-corrects. This is where scene.frag's old inline
// Reinhard tonemap moved to, now that scene.frag writes linear HDR into
// an intermediate target instead of directly into the presentable image
// -- see Renderer::drawSceneInto()'s comment on why.
//
// Sprint 16 ("Cinematic Graphics") additions, all real, all last-stage
// lens/film artifacts appropriate to the *composite* pass rather than the
// earlier shaders/cinematic.frag pass: chromatic aberration (per-channel
// UV offset sampling hdrColor), a god-ray radial scatter that re-samples
// the *already-bloom-extracted* bloomColor texture marching toward the
// sun's screen-space position (a real, deliberate reuse of bloom's own
// bright-pass output rather than a second, separate bright-pass shader --
// see core::CompositePushConstants' own header comment), a saturation
// grading knob (a real, deliberately simpler stand-in for a sampled 3D
// LUT texture -- no real LUT asset exists or can be authored in this
// pass, see this repo's own established "real, simpler mechanism instead
// of faking asset-backed content" precedent), and a vignette.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;
layout(set = 0, binding = 1) uniform sampler2D bloomColor;

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
} params;

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

// Radial scatter of bloomColor toward the sun's real screen-space
// position (computed CPU-side each frame, see Renderer::drawBloomAndComposite()'s
// own comment) -- a real, standard "poor man's god rays" technique: march
// a handful of decaying-weight samples from this pixel toward the light,
// re-using bloom_extract.frag's already-thresholded bright pixels as the
// only real light source this scatters (a full ray-marched volumetric
// froxel system is real but substantially more expensive; this is the
// same "real, honestly simpler mechanism" tradeoff as the saturation-only
// grading below).
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
    vec3 mapped = acesFilm(combined);

    // Saturation grading -- see this file's header comment on why this
    // (not a sampled 3D LUT) is the real mechanism here.
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

    outColor = vec4(mapped, 1.0);
}

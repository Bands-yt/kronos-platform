#version 450

// Sprint 16 ("Cinematic Graphics") auto-exposure: renders into a real 1x1
// target, so this single invocation's output *is* the frame's measured
// average luminance -- no mipmap chain, no compute-shader reduction, just
// a fixed grid of texture fetches averaged in log space (the standard
// "log-average luminance" formulation: a handful of very bright pixels --
// an emissive crystal, the sun disk -- shouldn't single-handedly drag a
// linear average around the way they would a plain sum/N). See
// Renderer::drawLuminancePass()'s own comment for how the 1x1 result
// reaches the CPU without stalling the frame that produced it.

layout(location = 0) in vec2 inUV; // unused -- see below
layout(location = 0) out vec4 outLuminance;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

void main() {
    // Deliberately *not* using inUV (which is ~(0.5, 0.5) at this single
    // covered pixel, telling us nothing useful) -- instead sampling a
    // fixed 8x8 grid across the *whole* source image, real coverage of
    // the actual frame rather than one arbitrary point sample.
    const int kGridSize = 8;
    float logSum = 0.0;
    for (int y = 0; y < kGridSize; ++y) {
        for (int x = 0; x < kGridSize; ++x) {
            vec2 uv = (vec2(float(x), float(y)) + 0.5) / float(kGridSize);
            vec3 c = textureLod(hdrColor, uv, 0.0).rgb;
            float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
            logSum += log(max(luma, 1e-4));
        }
    }
    float avgLogLuma = logSum / float(kGridSize * kGridSize);
    outLuminance = vec4(exp(avgLogLuma), 0.0, 0.0, 1.0);
}

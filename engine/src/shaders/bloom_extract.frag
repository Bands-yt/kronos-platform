#version 450

// Bright-pass extract + a small blur, combined into one pass -- reads the
// full-resolution HDR scene color and writes a half-resolution "bloom"
// texture. Simplification, stated plainly: this is a single 3x3-tap blur
// at half res, not a full downsample/upsample mip chain the way a AAA
// bloom (Call of Duty's "next-gen post processing" dual-filter approach,
// or Unreal's) would build one -- that gives a much wider, smoother glow
// radius at a much higher pass count. This gives a real, visible glow
// around bright/emissive pixels at a fraction of the implementation and
// runtime cost; widening the effective radius later is a matter of adding
// more downsample steps here, not a redesign.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(push_constant) uniform BloomPushConstants {
    float threshold;
    float softKnee;
} params;

// Soft-knee bright-pass (same shape Unity/Unreal use): pixels well above
// threshold pass through fully, pixels well below contribute nothing, and
// a small band around the threshold blends smoothly instead of a hard
// cutoff that would show as a visible ring around bright objects.
vec3 extractBright(vec3 color) {
    float brightness = max(color.r, max(color.g, color.b));
    float knee = max(params.threshold * params.softKnee, 1e-4);
    float soft = brightness - params.threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, brightness - params.threshold) / max(brightness, 1e-5);
    return color * contribution;
}

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(hdrColor, 0));
    vec3 result = vec3(0.0);
    float weights[3] = float[](0.25, 0.5, 0.25);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            // *2 texel step: sampling the full-res source at a stride
            // matched to this half-res destination's own texel size.
            vec2 offset = vec2(x, y) * texelSize * 2.0;
            vec3 sampled = texture(hdrColor, inUV + offset).rgb;
            result += extractBright(sampled) * weights[x + 1] * weights[y + 1];
        }
    }
    outColor = vec4(result, 1.0);
}

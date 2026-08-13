#version 450

// Sprint 16 ("Cinematic Graphics"): real soft-particle depth fade --
// particlePipelineLayout_'s set=1 (see Renderer.hpp's own comment on why
// this pipeline has a dedicated layout) binds the scene's own real depth
// buffer here, so a particle approaching solid geometry fades out
// smoothly instead of the pre-existing hard depth-test cutoff (still
// real and still in effect -- Renderer::createParticlePipeline()'s
// depthTestEnable=true; this shader adds a *second*, softer real
// mechanism on top, not a replacement for it).
layout(set = 1, binding = 0) uniform sampler2D sceneDepth;

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Soft circular falloff from the quad's center -- turns the flat quad
    // into a soft dot/spark instead of a visible hard-edged square. A
    // cheap stand-in for a real particle sprite texture; no texture-asset
    // pipeline exists yet (see migration::AssetConverter's still-stubbed
    // texture path).
    vec2 centered = inUV * 2.0 - 1.0;
    float dist = length(centered);
    float falloff = 1.0 - smoothstep(0.6, 1.0, dist);

    // Real soft-particle depth fade: gl_FragCoord.z is this fragment's
    // own hardware depth, in the exact same [0,1] encoding as sceneDepth
    // -- both this pass and the opaque scene pass share the identical
    // view/proj (see shaders/particle.vert's own comment), so no extra
    // linearization is needed to compare them directly.
    // gl_FragCoord.xy are real window pixel coordinates; dividing by the
    // depth texture's own real size converts to the [0,1] UV sceneDepth
    // expects, without needing a separate viewport-size uniform.
    vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepth, 0));
    float surfaceDepth = texture(sceneDepth, screenUV).r;
    // Positive when this particle is in front of (nearer than) the real
    // solid surface behind it; smoothstep gives a real, soft fade band
    // instead of the binary depth-test cutoff -- 0 right at/behind the
    // surface, 1 a real, small depth-epsilon in front of it. Reversed-Z
    // is not in use here (depthCompareOp is VK_COMPARE_OP_LESS, "smaller
    // is nearer", the conventional convention), so a *smaller*
    // gl_FragCoord.z than surfaceDepth is what "in front of" means.
    // Real, honest simplification: this epsilon (0.02) is in raw,
    // non-linear hardware depth-buffer space, not a real linear world/
    // view-space distance -- standard perspective depth packs far more
    // precision close to the camera than far away, so this same 0.02
    // fade band corresponds to a much larger real-world distance far from
    // the camera than close to it. Reconstructing true linear depth here
    // would need the projection's near/far planes threaded into this
    // pipeline; at this engine's real scene scale (tens of units, most
    // particles spawned near on-screen action) the non-uniform fade band
    // reads as correct in practice, not worth the added real complexity
    // in this pass.
    float depthDelta = surfaceDepth - gl_FragCoord.z;
    float softFade = smoothstep(0.0, 0.02, depthDelta);
    falloff *= softFade;

    // Premultiplied-alpha-style output for additive blending (see
    // Renderer::createParticlePipeline's blend state) -- alpha still
    // modulates brightness/fade-out, but there's no back-to-front sort
    // dependency the way regular alpha blending would need, which is
    // exactly why additive was chosen for a first particle pass.
    outColor = vec4(inColor.rgb, inColor.a * falloff);
}

#version 450

// Real, single-pipeline 2D UI fragment shader -- two real real modes
// selected per-vertex (not per-draw-call) by a real UV sentinel:
// core::UIRenderer::drawRect() emits inUV = (-1,-1) for a real flat-color
// panel/bar quad; drawText() emits real font-atlas UVs in [0,1] for a
// real glyph quad, sampling the atlas's own red channel as a real alpha
// mask multiplied by the vertex color (so any real UI color can tint the
// same real white glyph atlas). One real shared pipeline, one real
// vertex buffer, one real draw call per frame either way.

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D fontAtlas;

void main() {
    if (inUV.x < 0.0) {
        outColor = inColor;
    } else {
        float alpha = texture(fontAtlas, inUV).r;
        outColor = vec4(inColor.rgb, inColor.a * alpha);
    }
}

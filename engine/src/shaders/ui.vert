#version 450

// Kronos ("User Interface" world-building): the real, first screen-space
// 2D UI pipeline this renderer has ever had -- see core::UIRenderer's own
// header comment for why (this engine's own documented "engine_runtime
// renders no text at all" boundary, now closed). Positions arrive
// already converted to NDC on the CPU side (core::UIRenderer::drawRect()/
// drawText()) -- no view/projection matrix at all, this is pure 2D
// screen-space geometry.

layout(location = 0) in vec2 inPositionNDC;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec4 outColor;

void main() {
    gl_Position = vec4(inPositionNDC, 0.0, 1.0);
    outUV = inUV;
    outColor = inColor;
}

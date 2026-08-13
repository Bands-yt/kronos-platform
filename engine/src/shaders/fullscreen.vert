#version 450

// The standard "one triangle covers the whole screen" trick -- no vertex
// buffer needed at all; positions/UVs are derived purely from
// gl_VertexIndex (drawn with vkCmdDraw(3, 1, 0, 0), no vertex/index
// buffer bound). Shared by every post-process pass (bloom extract,
// composite) -- see Renderer::drawBloomAndComposite().
layout(location = 0) out vec2 outUV;

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}

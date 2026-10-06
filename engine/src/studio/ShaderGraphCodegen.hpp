#pragma once

#include <string>

namespace engine::studio {

class ShaderGraph;

struct ShaderGraphCodegenResult {
    bool success = false;
    std::string glsl;
    std::string errorMessage;
};

// Standalone fragment shader (no scene lighting) whose colour is the
// graph's Base Color. Requires exactly one PBR Output node.
[[nodiscard]] ShaderGraphCodegenResult generateFragmentShaderGlsl(const ShaderGraph& graph);

// Which variant of the forward shader the renderer runs; the surface
// shader must match it (see Renderer::surfaceShaderTarget()).
struct SurfaceShaderTarget {
    bool rayTracing = false;
    bool bindless = false;
};

// The full forward-lit fragment shader with the graph spliced in as
// kronosSurfaceGraph(); compile it with the engine shader directory on
// the include path. PBR Output inputs left unconnected keep the
// material's own value.
[[nodiscard]] ShaderGraphCodegenResult generateSurfaceShaderGlsl(const ShaderGraph& graph, const SurfaceShaderTarget& target);

} // namespace engine::studio

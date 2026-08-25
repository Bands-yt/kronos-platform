#pragma once

#include <string>

namespace engine::studio {

class ShaderGraph;

// Kronos ("Studio Revamp" -- "Node-Based Visual Shader Graph" Phase 2):
// real GLSL text generation from a ShaderGraph -- see that header's own
// class comment for the real, bounded node set and scope this targets.
struct ShaderGraphCodegenResult {
    bool success = false;
    // A complete, real, standalone GLSL fragment shader (a real
    // `#version 450` + input/output declarations + `main()`), ready to
    // hand straight to RuntimeShaderCompiler::compile() -- valid only
    // when success.
    std::string glsl;
    // Populated only when !success -- e.g. "graph has no PBR Output
    // node," "graph has a cycle involving node 3." Real, specific
    // messages, not a generic "codegen failed."
    std::string errorMessage;
};

// Requires exactly one ShaderNodeKind::PbrOutput node in `graph` --
// that's the real root this walks backward from. Its four input pins
// (baseColor/metallic/roughness/emissive) that aren't connected to
// anything fall back to real, honest defaults (opaque white, non-
// metallic, mid-roughness, no emissive) rather than erroring -- same
// "an unassigned slot is a no-op, not a hard error" convention
// core::Texture's own default-white-texture fallback already
// establishes for this engine's material system.
[[nodiscard]] ShaderGraphCodegenResult generateFragmentShaderGlsl(const ShaderGraph& graph);

} // namespace engine::studio

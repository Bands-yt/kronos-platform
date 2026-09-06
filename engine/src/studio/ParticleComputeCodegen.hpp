#pragma once

#include <cstdint>
#include <string>

namespace engine::studio {

class ParticleComputeGraph;

// Kronos ("Real-Time GPU Particle Compute" -- v0.4.0 Creator Suite):
// real GLSL compute-shader generation from a ParticleComputeGraph -- the
// compute-stage sibling of ShaderGraphCodegen.hpp, see that header's own
// comment for the exact "generate real GLSL text, hand it to
// RuntimeShaderCompiler::compile()" shape this mirrors.
struct ParticleComputeCodegenResult {
    bool success = false;
    // A complete, real, standalone GLSL compute shader (`#version 450`,
    // a `Particle` SSBO, a `local_size_x` layout, and `main()`), ready
    // to hand straight to
    // RuntimeShaderCompiler::compile(..., ShaderStage::Compute, ...) --
    // valid only when success.
    std::string glsl;
    std::string errorMessage;
};

// Requires exactly one ParticleNodeKind::ParticleOutput node in `graph`
// -- the real root this walks backward from, same convention
// generateFragmentShaderGlsl() already uses for its own PbrOutput sink.
// Its two input pins (newPosition/newVelocity) fall back to this
// particle's own unmodified current position/velocity when
// unconnected -- an honest no-op, not an error, matching
// ShaderGraphCodegen's own "an unassigned slot is a no-op" convention.
[[nodiscard]] ParticleComputeCodegenResult generateComputeParticleShaderGlsl(const ParticleComputeGraph& graph,
                                                                              uint32_t localSizeX = 256);

} // namespace engine::studio

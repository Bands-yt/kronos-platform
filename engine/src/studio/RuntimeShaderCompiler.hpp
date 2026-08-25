#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::studio {

// Kronos ("Studio Revamp" -- "Node-Based Visual Shader Graph", Phase 1):
// real, in-process GLSL -> SPIR-V compilation via shaderc (Apache-2.0,
// see cmake/ShaderCompiler.cmake's own header comment for why this is a
// real dependency, unlike the license-gated Ultralight path). This is
// the load-bearing seam a future node-graph would generate GLSL text
// into and a future Renderer-side runtime-pipeline path would consume
// compiled SPIR-V from -- neither of those exists yet. What this class
// alone proves and provides: a Studio-authored GLSL string can become a
// real, loadable SPIR-V module while Studio is running, not just at
// `glslc` build time (see the shader-compile block at the top of
// src/CMakeLists.txt for that existing, separate, build-time path this
// does not replace -- every shipped .frag/.vert file keeps compiling
// that way; this is additive, for content authored *during* a Studio
// session).
class RuntimeShaderCompiler {
public:
    RuntimeShaderCompiler();
    ~RuntimeShaderCompiler();

    RuntimeShaderCompiler(const RuntimeShaderCompiler&) = delete;
    RuntimeShaderCompiler& operator=(const RuntimeShaderCompiler&) = delete;

    // The two real shader stages this engine's existing pipelines use
    // (see Renderer::createScenePipeline() and friends) -- no geometry/
    // tessellation/compute stage exists in this renderer to compile for
    // yet, so this enum doesn't invent cases nothing would consume.
    enum class ShaderStage { Fragment, Vertex };

    struct Result {
        bool success = false;
        // Valid only when success -- a real SPIR-V word stream (the
        // first word is always the 0x07230203 magic number on success),
        // ready for vkCreateShaderModule exactly like the compiled
        // output of any of this engine's own build-time .spv files.
        std::vector<uint32_t> spirv;
        // Populated only when !success -- shaderc's own real diagnostic
        // text (file:line, the actual GLSL error), not a generic
        // "compile failed" message.
        std::string errorMessage;
        size_t warningCount = 0;
    };

    // Real, synchronous compilation -- shaderc's own compiler instance
    // is not thread-safe for concurrent Compile calls on the same
    // instance (see shaderc's own header), so this is deliberately a
    // plain blocking call, not an async job queue; a future caller doing
    // this on every keystroke of a node-graph edit would need its own
    // debounce, not something this class hides.
    [[nodiscard]] Result compile(const std::string& glslSource, ShaderStage stage, const std::string& debugName) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::studio

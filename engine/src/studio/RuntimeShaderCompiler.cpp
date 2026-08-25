#include "studio/RuntimeShaderCompiler.hpp"

#ifdef KRONOS_WITH_SHADERC
#include <shaderc/shaderc.hpp>
#endif

namespace engine::studio {

#ifdef KRONOS_WITH_SHADERC
struct RuntimeShaderCompiler::Impl {
    shaderc::Compiler compiler;
};
#else
// Kronos: real, honest stub -- see cmake/ShaderCompiler.cmake's own
// KRONOS_WITH_SHADERC comment. A build with KRONOS_BUILD_SHADER_COMPILER
// OFF still compiles this class; compile() below just always reports
// unavailable rather than the real shaderc result.
struct RuntimeShaderCompiler::Impl {};
#endif

RuntimeShaderCompiler::RuntimeShaderCompiler() : impl_(std::make_unique<Impl>()) {}
RuntimeShaderCompiler::~RuntimeShaderCompiler() = default;

RuntimeShaderCompiler::Result RuntimeShaderCompiler::compile(const std::string& glslSource, ShaderStage stage,
                                                               const std::string& debugName) const {
    Result result;
#ifndef KRONOS_WITH_SHADERC
    (void)glslSource;
    (void)stage;
    (void)debugName;
    result.errorMessage = "Runtime shader compilation is unavailable in this build (KRONOS_BUILD_SHADER_COMPILER is OFF).";
    return result;
#else
    if (!impl_->compiler.IsValid()) {
        // Real, honest failure -- shaderc::Compiler's constructor can
        // fail to initialize its internal glslang/SPIRV-Tools state;
        // IsValid() is shaderc's own documented way to check before
        // trusting any compile result from it.
        result.errorMessage = "shaderc::Compiler failed to initialize";
        return result;
    }

    shaderc::CompileOptions options;
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    // Matches this engine's own real Vulkan instance target --
    // Renderer::createInstance()'s appInfo.apiVersion = VK_API_VERSION_1_3
    // (Renderer.cpp) -- not an arbitrary/older choice this compiled
    // SPIR-V then couldn't rely on the real feature set of.
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

    shaderc_shader_kind kind =
        stage == ShaderStage::Fragment ? shaderc_glsl_fragment_shader : shaderc_glsl_vertex_shader;
    shaderc::SpvCompilationResult compiled =
        impl_->compiler.CompileGlslToSpv(glslSource, kind, debugName.c_str(), options);

    result.warningCount = compiled.GetNumWarnings();
    if (compiled.GetCompilationStatus() != shaderc_compilation_status_success) {
        result.errorMessage = compiled.GetErrorMessage();
        return result;
    }

    result.spirv.assign(compiled.cbegin(), compiled.cend());
    result.success = true;
    return result;
#endif
}

} // namespace engine::studio

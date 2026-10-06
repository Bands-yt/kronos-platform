#include "studio/RuntimeShaderCompiler.hpp"

#ifdef KRONOS_WITH_SHADERC
#include <filesystem>
#include <fstream>
#include <sstream>

#include <shaderc/shaderc.hpp>
#endif

namespace engine::studio {

#ifdef KRONOS_WITH_SHADERC
struct RuntimeShaderCompiler::Impl {
    shaderc::Compiler compiler;
};

namespace {

class FileIncluder final : public shaderc::CompileOptions::IncluderInterface {
public:
    explicit FileIncluder(std::vector<std::string> directories) : directories_(std::move(directories)) {}

    shaderc_include_result* GetInclude(const char* requestedSource, shaderc_include_type type,
                                       const char* requestingSource, size_t) override {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        if (type == shaderc_include_type_relative) {
            candidates.push_back(fs::path(requestingSource).parent_path() / requestedSource);
        }
        for (const std::string& directory : directories_) candidates.push_back(fs::path(directory) / requestedSource);

        auto* data = new Data;
        for (const fs::path& candidate : candidates) {
            std::ifstream file(candidate, std::ios::binary);
            if (!file) continue;
            std::ostringstream content;
            content << file.rdbuf();
            data->name = candidate.lexically_normal().string();
            data->content = content.str();
            break;
        }
        if (data->name.empty()) data->content = std::string("cannot find include \"") + requestedSource + "\"";
        data->result.source_name = data->name.c_str();
        data->result.source_name_length = data->name.size();
        data->result.content = data->content.c_str();
        data->result.content_length = data->content.size();
        data->result.user_data = data;
        return &data->result;
    }

    void ReleaseInclude(shaderc_include_result* result) override { delete static_cast<Data*>(result->user_data); }

private:
    struct Data {
        shaderc_include_result result{};
        std::string name;
        std::string content;
    };
    std::vector<std::string> directories_;
};

} // namespace
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
    return compile(glslSource, stage, debugName, Options{});
}

RuntimeShaderCompiler::Result RuntimeShaderCompiler::compile(const std::string& glslSource, ShaderStage stage,
                                                               const std::string& debugName,
                                                               const Options& compileOptions) const {
    Result result;
#ifndef KRONOS_WITH_SHADERC
    (void)glslSource;
    (void)stage;
    (void)debugName;
    (void)compileOptions;
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
    if (!compileOptions.includeDirectories.empty()) {
        options.SetIncluder(std::make_unique<FileIncluder>(compileOptions.includeDirectories));
    }

    shaderc_shader_kind kind = shaderc_glsl_vertex_shader;
    if (stage == ShaderStage::Fragment) kind = shaderc_glsl_fragment_shader;
    else if (stage == ShaderStage::Compute) kind = shaderc_glsl_compute_shader;
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

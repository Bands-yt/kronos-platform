#include "studio/SurfaceGraphMaterials.hpp"

#include <chrono>
#include <unordered_set>

#include "core/Components.hpp"
#include "core/Renderer.hpp"
#include "studio/ShaderGraph.hpp"

namespace engine::studio {

namespace {

SurfaceShaderTarget targetOf(const core::Renderer::SurfaceShaderTarget& target) {
    return {target.rayTracing, target.bindless};
}

} // namespace

SurfaceGraphMaterials::Compiled SurfaceGraphMaterials::compile(const std::string& graphText, const SurfaceShaderTarget& target,
                                                               const std::string& shaderDirectory,
                                                               const RuntimeShaderCompiler& compiler) {
    Compiled out;
    const auto start = std::chrono::steady_clock::now();
    ShaderGraph graph;
    if (!ShaderGraph::deserialize(graphText, graph, out.error)) return out;
    ShaderGraphCodegenResult codegen = generateSurfaceShaderGlsl(graph, target);
    if (!codegen.success) {
        out.error = codegen.errorMessage;
        return out;
    }
    out.glsl = codegen.glsl;
    RuntimeShaderCompiler::Options options;
    options.includeDirectories.push_back(shaderDirectory);
    RuntimeShaderCompiler::Result result =
        compiler.compile(codegen.glsl, RuntimeShaderCompiler::ShaderStage::Fragment, "shader_graph_surface.frag", options);
    out.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!result.success) {
        out.error = result.errorMessage;
        return out;
    }
    out.spirv = std::move(result.spirv);
    out.success = true;
    return out;
}

SurfaceGraphMaterials::Compiled SurfaceGraphMaterials::prepare(const std::string& graphText,
                                                               const core::Renderer& renderer) {
    const core::Renderer::SurfaceShaderTarget target = renderer.surfaceShaderTarget();
    Compiled compiled = compile(graphText, targetOf(target), target.shaderDirectory, compiler_);
    if (compiled.success && cache_.count(graphText) == 0) prepared_[graphText] = compiled.spirv;
    return compiled;
}

void SurfaceGraphMaterials::update(core::ECS& ecs, core::Renderer& renderer) {
    std::unordered_set<std::string> used;
    auto& registry = ecs.raw();

    for (auto [entity, material, renderable] : registry.view<core::SurfaceGraphMaterial, core::Renderable>().each()) {
        auto it = cache_.find(material.graph);
        if (it == cache_.end()) {
            Compiled compiled;
            if (auto prepared = prepared_.find(material.graph); prepared != prepared_.end()) {
                compiled.success = true;
                compiled.spirv = std::move(prepared->second);
            } else {
                const core::Renderer::SurfaceShaderTarget target = renderer.surfaceShaderTarget();
                compiled = compile(material.graph, targetOf(target), target.shaderDirectory, compiler_);
            }
            Entry entry;
            if (compiled.success) {
                entry.pipeline = renderer.createSurfacePipeline(compiled.spirv);
                if (entry.pipeline == core::Renderer::kInvalidSurfacePipeline) entry.error = "pipeline creation failed";
            } else {
                entry.error = compiled.error;
            }
            it = cache_.emplace(material.graph, std::move(entry)).first;
        }
        used.insert(material.graph);
        renderable.surfacePipeline = it->second.pipeline;
    }

    for (auto [entity, renderable] : registry.view<core::Renderable>(entt::exclude<core::SurfaceGraphMaterial>).each()) {
        renderable.surfacePipeline = core::Renderable::kInvalidHandle;
    }

    prepared_.clear();
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (used.count(it->first) != 0) {
            ++it;
            continue;
        }
        if (it->second.pipeline != core::Renderer::kInvalidSurfacePipeline) renderer.destroySurfacePipeline(it->second.pipeline);
        it = cache_.erase(it);
    }
}

void SurfaceGraphMaterials::shutdown(core::Renderer& renderer) {
    for (auto& [graph, entry] : cache_) {
        if (entry.pipeline != core::Renderer::kInvalidSurfacePipeline) renderer.destroySurfacePipeline(entry.pipeline);
    }
    cache_.clear();
}

const std::string* SurfaceGraphMaterials::errorFor(const std::string& graphText) const {
    auto it = cache_.find(graphText);
    if (it == cache_.end() || it->second.error.empty()) return nullptr;
    return &it->second.error;
}

size_t SurfaceGraphMaterials::pipelineCount() const {
    size_t count = 0;
    for (const auto& [graph, entry] : cache_) count += entry.pipeline != core::Renderer::kInvalidSurfacePipeline ? 1 : 0;
    return count;
}

} // namespace engine::studio

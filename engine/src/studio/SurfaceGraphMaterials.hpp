#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/ECS.hpp"
#include "studio/RuntimeShaderCompiler.hpp"
#include "studio/ShaderGraphCodegen.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio {

// Keeps every entity's SurfaceGraphMaterial compiled into a renderer
// pipeline. Pipelines are shared by identical graphs and freed once no
// entity uses them.
class SurfaceGraphMaterials {
public:
    struct Compiled {
        bool success = false;
        std::string error;
        std::string glsl;
        std::vector<uint32_t> spirv;
        double milliseconds = 0.0;
    };

    [[nodiscard]] static Compiled compile(const std::string& graphText, const SurfaceShaderTarget& target,
                                          const std::string& shaderDirectory, const RuntimeShaderCompiler& compiler);

    void update(core::ECS& ecs, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

    // Compiles now so the editor can report errors and timing; a success
    // is kept so the next update() builds its pipeline without recompiling.
    [[nodiscard]] Compiled prepare(const std::string& graphText, const core::Renderer& renderer);

    // Error from the last compile of this graph text, or nullptr when it
    // compiled (or hasn't been seen yet).
    [[nodiscard]] const std::string* errorFor(const std::string& graphText) const;
    [[nodiscard]] size_t pipelineCount() const;

private:
    struct Entry {
        uint32_t pipeline = ~0u;
        std::string error;
    };

    RuntimeShaderCompiler compiler_;
    std::unordered_map<std::string, Entry> cache_;
    std::unordered_map<std::string, std::vector<uint32_t>> prepared_;
};

} // namespace engine::studio

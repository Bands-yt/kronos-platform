#pragma once

#include <string>
#include <vector>

#include "studio/IStudioPlugin.hpp"
#include "studio/RuntimeShaderCompiler.hpp"
#include "studio/ShaderGraph.hpp"

struct ImNodesEditorContext;

namespace engine::studio::plugins {

// Kronos ("Studio Revamp" -- "Node-Based Visual Shader Graph" Phase 3):
// the real, visual imnodes panel over Phase 2's already-proven
// ShaderGraph/ShaderGraphCodegen (see those headers' own class
// comments for the real, bounded node set and scope). This class is
// deliberately thin -- drawing/interaction only; every real question
// about what the graph *means* was already answered, and headlessly
// tested, in ShaderGraph itself. Not yet wired into core::Renderer's
// live pipeline (a real, separate, later step) -- "Compile" here
// proves the generated GLSL is real, valid SPIR-V (RuntimeShaderCompiler),
// it doesn't yet make anything render with it.
class ShaderGraphPlugin final : public IStudioPlugin {
public:
    ShaderGraphPlugin();
    ~ShaderGraphPlugin() override;

    ShaderGraphPlugin(const ShaderGraphPlugin&) = delete;
    ShaderGraphPlugin& operator=(const ShaderGraphPlugin&) = delete;

    [[nodiscard]] const char* name() const override { return "Shader Graph"; }
    [[nodiscard]] const char* category() const override { return "Rendering"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawToolbar();
    void drawNodeEditor();
    void drawNode(const ShaderNode& node);
    // Real per-kind node content -- the constant-value drag widgets for
    // Constant* nodes; every other kind just draws its pins (no extra
    // content), same "not every node needs authored fields" shape a
    // real shader graph's Add/Multiply/Sample nodes have.
    void drawNodeContent(ShaderNode& node);
    void handleNewLinks();
    void handleDeletion();
    void compile();

    ShaderGraph graph_;
    RuntimeShaderCompiler compiler_;
    ImNodesEditorContext* editorContext_ = nullptr;

    int addNodeKindIndex_ = 0;
    std::string statusMessage_;
    bool statusIsError_ = false;
    // The most recent real compiled result -- shown expanded so a
    // creator can actually read the generated GLSL, not just a
    // pass/fail line.
    std::string lastGeneratedGlsl_;
    bool showGeneratedGlsl_ = false;
};

} // namespace engine::studio::plugins

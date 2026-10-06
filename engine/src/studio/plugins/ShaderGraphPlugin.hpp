#pragma once

#include <set>
#include <string>
#include <vector>

#include "studio/IStudioPlugin.hpp"
#include "studio/RuntimeShaderCompiler.hpp"
#include "studio/ShaderGraph.hpp"

struct ImNodesEditorContext;

namespace engine::core {
class Renderer;
}

namespace engine::studio {
class SurfaceGraphMaterials;
}

namespace engine::studio::plugins {

// imnodes editor over ShaderGraph. Compiles the graph against the live
// renderer's forward shader and applies it to the selected objects as a
// SurfaceGraphMaterial.
class ShaderGraphPlugin final : public IStudioPlugin {
public:
    ShaderGraphPlugin();
    ~ShaderGraphPlugin() override;

    ShaderGraphPlugin(const ShaderGraphPlugin&) = delete;
    ShaderGraphPlugin& operator=(const ShaderGraphPlugin&) = delete;

    [[nodiscard]] const char* name() const override { return "Shader Graph"; }
    [[nodiscard]] const char* category() const override { return "Rendering"; }

    void setMaterials(SurfaceGraphMaterials* materials, const core::Renderer* renderer);

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawToolbar(core::ECS& ecs, const std::vector<core::EntityId>& targets);
    void drawAddNodeMenu();
    void drawNodeEditor();
    void drawNode(ShaderNode& node);
    void drawNodeContent(ShaderNode& node);
    void handleNewLinks();
    void handleDeletion();
    void handleLiveUpdate(core::ECS& ecs);
    bool compile();
    void applyTo(core::ECS& ecs, const std::vector<core::EntityId>& targets);
    void loadFrom(core::ECS& ecs, core::EntityId entity);
    void replaceGraph(ShaderGraph graph);
    void openFile(const std::string& path);
    void saveFile(const std::string& path);
    void setStatus(std::string message, bool error);

    ShaderGraph graph_;
    RuntimeShaderCompiler compiler_;
    SurfaceGraphMaterials* materials_ = nullptr;
    const core::Renderer* renderer_ = nullptr;
    ImNodesEditorContext* editorContext_ = nullptr;

    std::set<int> placedNodes_;
    bool placeNextAtMouse_ = false;
    float nextNodeScreenX_ = 0.0f;
    float nextNodeScreenY_ = 0.0f;

    std::string statusMessage_;
    bool statusIsError_ = false;
    std::string lastGeneratedGlsl_;
    bool showGeneratedGlsl_ = false;
    std::string filePath_;

    std::vector<core::EntityId> linked_;
    bool liveUpdate_ = true;
    std::string lastLiveGraph_;
    double liveChangedAt_ = -1.0;
};

} // namespace engine::studio::plugins

#pragma once

#include <set>
#include <string>
#include <vector>

#include "studio/IStudioPlugin.hpp"
#include "studio/VisualScript.hpp"

struct ImNodesEditorContext;

namespace engine::studio::plugins {

// imnodes editor over VisualScriptGraph. Applying a graph compiles it to
// Luau and stores the result in the object's Script, so it runs anywhere
// a hand-written script does (Studio Play, the player, servers).
class VisualScriptPlugin final : public IStudioPlugin {
public:
    VisualScriptPlugin();
    ~VisualScriptPlugin() override;

    VisualScriptPlugin(const VisualScriptPlugin&) = delete;
    VisualScriptPlugin& operator=(const VisualScriptPlugin&) = delete;

    [[nodiscard]] const char* name() const override { return "Visual Script"; }
    [[nodiscard]] const char* category() const override { return "Scripting"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawToolbar(core::ECS& ecs, const std::vector<core::EntityId>& targets);
    void drawAddNodeMenu();
    void drawNodeEditor();
    void drawNode(VsNode& node);
    void drawLiteral(const VsPin& pin, VsLiteral& value);
    void handleNewLinks();
    void handleDeletion();
    void handleLiveUpdate(core::ECS& ecs);
    bool compile();
    int applyTo(core::ECS& ecs, const std::vector<core::EntityId>& targets);
    void loadFrom(core::ECS& ecs, core::EntityId entity);
    void replaceGraph(VisualScriptGraph graph);
    void openFile(const std::string& path);
    void saveFile(const std::string& path);
    void setStatus(std::string message, bool error);

    VisualScriptGraph graph_;
    ImNodesEditorContext* editorContext_ = nullptr;

    std::set<int> placedNodes_;
    bool placeNextAtMouse_ = false;
    float nextNodeScreenX_ = 0.0f;
    float nextNodeScreenY_ = 0.0f;
    int tooltipNode_ = 0;
    double tooltipSince_ = 0.0;

    std::string statusMessage_;
    bool statusIsError_ = false;
    std::vector<std::string> warnings_;
    int errorNodeId_ = 0;
    std::string lastLuau_;
    size_t lastBytecodeSize_ = 0;
    bool showLuau_ = false;
    std::string filePath_;

    std::vector<core::EntityId> linked_;
    bool liveUpdate_ = true;
    std::string lastLiveGraph_;
    double liveChangedAt_ = -1.0;
};

} // namespace engine::studio::plugins

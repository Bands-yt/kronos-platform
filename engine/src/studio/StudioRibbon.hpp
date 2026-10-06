#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

#include "core/ECS.hpp"
#include "studio/StudioIcons.hpp"
#include "studio/panels/ViewportPanel.hpp"

namespace engine::core {
class MeshLibrary;
}

namespace engine::studio {

class PluginManager;
class UndoStack;
namespace panels {
class ExplorerPanel;
}
namespace plugins {
class PhysicsPreviewPlugin;
class ModelImporterPlugin;
}

// Scene-level operations the ribbon triggers but StudioApp owns, because
// they need the undo stack, clipboard or GPU resources together.
struct RibbonActions {
    std::function<void()> copy;
    std::function<void()> paste;
    std::function<void()> duplicate;
    std::function<void()> remove;
    std::function<void()> group;
    std::function<void()> ungroup;
    std::function<void()> toggleAnchor;
    std::function<void(panels::ViewportPanel::Primitive)> insertPrimitive;
    std::function<void()> togglePlay;
    std::function<void()> togglePerformanceOverlay;
    std::function<void()> resetLayout;
    std::function<void()> save;
};

struct RibbonState {
    bool canPaste = false;
    bool anchored = false;
    bool performanceOverlayOpen = false;
    bool canSave = false;
    bool showEngineOverlays = true;
    bool terrainAvailable = false;
};

struct RibbonContext {
    core::ECS& ecs;
    core::MeshLibrary& meshLibrary;
    panels::ExplorerPanel& explorer;
    panels::ViewportPanel& viewport;
    UndoStack& undo;
    PluginManager& plugins;
    plugins::PhysicsPreviewPlugin* physics = nullptr;
    plugins::ModelImporterPlugin* modelImporter = nullptr;
    const RibbonActions& actions;
    const RibbonState& state;
};

// Roblox Studio-style tabbed tool ribbon drawn across the top of the
// Studio window, under the menu bar.
class StudioRibbon {
public:
    void draw(const RibbonContext& context);

private:
    enum class Tab { Home, Model, Test, View };

    void drawTabStrip(const RibbonContext& context);
    void drawHome(const RibbonContext& context);
    void drawModel(const RibbonContext& context);
    void drawTest(const RibbonContext& context);
    void drawView(const RibbonContext& context);

    void drawToolsGroup(const RibbonContext& context);
    void drawPlayGroup(const RibbonContext& context);
    void drawPartButton(const RibbonContext& context, const char* id);
    void drawColorButton(const RibbonContext& context);
    void drawMaterialButton(const RibbonContext& context);
    void drawPluginToggle(const RibbonContext& context, const char* pluginName, Icon icon, const char* label);

    bool bigButton(const char* id, Icon icon, const char* label, bool active, bool enabled, const char* tooltip,
                   ImU32 iconColor, bool dropdown = false);
    bool smallButton(const char* id, Icon icon, const char* label, bool active, bool enabled, const char* tooltip,
                     ImU32 iconColor);
    bool beginDropdown(const char* popupId);
    void endDropdown();
    void beginGroup();
    void endGroup(const char* caption);
    void beginColumn();
    void endColumn();

    Tab tab_ = Tab::Home;
    ImVec2 bodyMin_{0.0f, 0.0f};
    float bodyHeight_ = 0.0f;
    float groupStartX_ = 0.0f;
    float cursorX_ = 0.0f;
    float columnStartX_ = 0.0f;
    float columnWidth_ = 0.0f;
    int columnRow_ = 0;
    bool inColumn_ = false;

    glm::vec4 editColor_{1.0f};
    std::vector<std::pair<core::EntityId, glm::vec4>> colorBefore_;
};

} // namespace engine::studio

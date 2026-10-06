#pragma once

#include <string>

#include "plugin/PluginHost.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio {

// Shows one editor panel registered through the C plugin API.
class PluginApiPanel final : public IStudioPlugin {
public:
    PluginApiPanel(plugin::PluginHost& host, std::string panelId, std::string title);

    [[nodiscard]] const char* name() const override { return title_.c_str(); }
    [[nodiscard]] const char* category() const override { return "Plugins"; }
    [[nodiscard]] const std::string& panelId() const { return panelId_; }
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // A KronosUi that draws with ImGui in the current window.
    [[nodiscard]] static KronosUi imguiTable();

private:
    plugin::PluginHost& host_;
    std::string panelId_;
    std::string title_;
};

} // namespace engine::studio

#include "studio/PluginManager.hpp"

#include <string>

#include <imgui.h>

namespace engine::studio {

void PluginManager::drawMenu() {
    if (!ImGui::BeginMenu("Plugins")) return;

    const char* currentCategory = nullptr;
    for (auto& plugin : plugins_) {
        if (currentCategory != nullptr && std::string(currentCategory) != plugin->category()) {
            ImGui::Separator();
        }
        currentCategory = plugin->category();

        bool open = plugin->isOpen();
        if (ImGui::MenuItem(plugin->name(), nullptr, open)) {
            plugin->setOpen(!open);
        }
    }
    ImGui::EndMenu();
}

void PluginManager::drawPanels(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) {
    for (auto& plugin : plugins_) {
        if (!plugin->isOpen()) continue;
        plugin->drawPanel(ecs, selected, selectedEntities);
    }
}

} // namespace engine::studio

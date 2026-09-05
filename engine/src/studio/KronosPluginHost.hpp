#pragma once

#include <memory>
#include <vector>

#include <SDL2/SDL.h>

#include "studio/IKronosPlugin.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio {

// Owns every registered IKronosPlugin -- the windowed-plugin sibling of
// PluginManager.hpp's IStudioPlugin list (see that header's own class
// comment). registerPlugin() has the same "no removal, only registered
// once for the process's lifetime" contract PluginManager already uses.
class KronosPluginHost {
public:
    void registerPlugin(std::unique_ptr<IKronosPlugin> plugin) { plugins_.push_back(std::move(plugin)); }

    // Real per-frame CPU tick, only for plugins currently open -- a
    // closed plugin has no window/swapchain to step (see
    // IKronosPlugin.hpp's own comment on why this differs from
    // IStudioPlugin::update()'s "always runs" contract). Also drains
    // each open plugin's own closeRequested() flag (set by its
    // SecondaryViewport observing a real window-close event) so a user
    // closing the OS window itself, not just a menu toggle, tears down
    // GPU resources the same real way.
    void tick(float dt, core::ECS& ecs, core::EntityId selected, core::Renderer& renderer) {
        for (auto& plugin : plugins_) {
            if (plugin->closeRequested()) plugin->close(renderer);
            if (plugin->isOpen()) plugin->tick(dt, ecs, selected);
        }
    }

    void renderFrame(core::Renderer& renderer, core::ECS& ecs, core::EntityId selected) {
        for (auto& plugin : plugins_) {
            if (plugin->isOpen()) plugin->renderFrame(renderer, ecs, selected);
        }
    }

    // Kronos ("3D Mesh & CSG Editor" -- multi-window event dispatch):
    // forwarded unconditionally to every registered plugin (open or not
    // -- SDL_WINDOWEVENT_CLOSE can arrive for a window this plugin no
    // longer owns only if the plugin itself is already closed, in which
    // case its own handler is a real no-op); each plugin's own
    // SecondaryViewport filters by its own windowId() internally (see
    // core::Window::pumpEvents()'s own comment on why only one
    // SDL_PollEvent loop may run per process -- this is that loop's
    // single dispatch point for every other window in the process).
    void handleEvent(const SDL_Event& event) {
        for (auto& plugin : plugins_) {
            plugin->handleEvent(event);
        }
    }

    // Closes every still-open plugin -- call once from StudioApp::shutdown().
    void shutdown(core::Renderer& renderer) {
        for (auto& plugin : plugins_) {
            if (plugin->isOpen()) plugin->close(renderer);
        }
    }

    [[nodiscard]] const std::vector<std::unique_ptr<IKronosPlugin>>& plugins() const { return plugins_; }

private:
    std::vector<std::unique_ptr<IKronosPlugin>> plugins_;
};

} // namespace engine::studio

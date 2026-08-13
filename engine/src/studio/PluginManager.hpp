#pragma once

#include <memory>
#include <vector>

#include "studio/IStudioPlugin.hpp"

namespace engine::studio {

// Owns every registered IStudioPlugin and draws the "Plugins" toolbar
// strip (docs/ARCHITECTURE.md §5/§9's plugin ecosystem).
//
// Most plugins registered here are first-party: a native C++ class
// StudioApp::initialize() constructs and hands to registerPlugin() (see
// AnimatorPlugin/AlignPlugin/MaterialPlugin in studio/plugins/). On top of
// that same contract (register once, get a toolbar button and a per-frame
// update+draw call), studio::plugins::PluginBrowserPlugin lets a user load
// third-party ones at runtime: it reads a studio::PluginManifest off disk
// and constructs a studio::plugins::ScriptedPlugin, an IStudioPlugin whose
// update()/drawPanel() forward into a sandboxed Luau VM (the same
// core::Scripting memory/time budgets gameplay scripts get) rather than
// native code -- registered through this exact registerPlugin(), no
// separate code path for "untrusted" plugins. What's still not built: a
// plugin package format beyond "manifest + one script file" (no
// zip/archive, no bundled assets, no dependency list) and a publish/
// marketplace flow for creator-authored plugins.
//
// registerPlugin() has no removal counterpart -- a plugin, first- or
// third-party, stays registered for the process's lifetime once loaded
// (closing its window just hides it via setOpen/isOpen). Same accepted
// "no removal, only replace" shape as MeshLibrary/TextureLibrary.
class PluginManager {
public:
    void registerPlugin(std::unique_ptr<IStudioPlugin> plugin) { plugins_.push_back(std::move(plugin)); }

    void update(float dt, core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) {
        for (auto& plugin : plugins_) {
            plugin->update(dt, ecs, selected, selectedEntities);
        }
    }

    // Call from inside an ImGui menu bar (see StudioApp::drawDockspace) --
    // one toggle-able MenuItem per plugin, grouped under its category().
    void drawMenu();

    // Draws every currently-open plugin's own window(s). Call once per
    // frame outside any other ImGui::Begin/End pair.
    void drawPanels(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities);

    [[nodiscard]] const std::vector<std::unique_ptr<IStudioPlugin>>& plugins() const { return plugins_; }

private:
    std::vector<std::unique_ptr<IStudioPlugin>> plugins_;
};

} // namespace engine::studio

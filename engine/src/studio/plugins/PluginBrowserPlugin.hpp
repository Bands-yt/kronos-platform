#pragma once

#include <string>
#include <vector>

#include "studio/IStudioPlugin.hpp"
#include "studio/LocalPluginDirectory.hpp"

namespace engine::net {
class NetworkSession;
}

namespace engine::studio {
class PluginManager;
class NotificationCenter;
}

namespace engine::studio::plugins {

// The runtime "load a third-party plugin" UI -- a native, first-party
// IStudioPlugin (category "Plugins", not "Third-Party": it lists and
// launches scripted plugins, it isn't one) that turns a manifest path
// typed by the user into a live studio::plugins::ScriptedPlugin
// registered against the same PluginManager every first-party plugin
// went through at startup. PluginManager::registerPlugin() has no
// removal counterpart (see PluginManager.hpp), so a plugin loaded here
// stays registered for the rest of the process -- the same accepted
// "no removal, only replace" shape MeshLibrary/TextureLibrary already
// have; closing a scripted plugin's window just hides it (setOpen), it
// doesn't unload the VM.
//
// Kronos (Alpha Roadmap Phase 9, "Platform Services" -- "Plugin
// marketplace (local only for alpha)"): also a real "Local Plugins"
// browser -- scanLocalPluginDirectory() (studio/LocalPluginDirectory.hpp)
// discovers every `*.manifest` sitting in a real local directory, so
// loading one is a one-click "Load" instead of typing its exact path.
class PluginBrowserPlugin final : public IStudioPlugin {
public:
    PluginBrowserPlugin(PluginManager& pluginManager, net::NetworkSession& networkSession, NotificationCenter& notifications)
        : pluginManager_(&pluginManager), networkSession_(&networkSession), notifications_(&notifications) {}

    [[nodiscard]] const char* name() const override { return "Plugin Browser"; }
    [[nodiscard]] const char* category() const override { return "Plugins"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawLocalPluginsSection();
    void loadFromManifestPath(const std::string& manifestPath);
    void scanNow();
    void createStarterPlugin();

    PluginManager* pluginManager_;
    net::NetworkSession* networkSession_; // real, live -- passed straight through to each ScriptedPlugin::load()
    NotificationCenter* notifications_;    // real toasts for load/scan/create results, see .cpp
    core::ECS* ecs_ = nullptr; // captured from the first drawPanel() call, see .cpp

    std::string manifestPathBuffer_;
    std::string statusMessage_;

    std::string pluginDirectoryBuffer_ = "plugins";
    std::vector<DiscoveredPlugin> discoveredPlugins_;
    bool hasScanned_ = false;
};

} // namespace engine::studio::plugins

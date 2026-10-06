#pragma once

#include <string>
#include <vector>

#include "core/NativePluginManager.hpp"
#include "core/ResourceManager.hpp"
#include "plugin/PluginHost.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Live view of core::ResourceManager: what is loaded, by whom, how long it
// took, what depends on what, and hot reload controls.
class ResourcesPlugin final : public IStudioPlugin {
public:
    explicit ResourcesPlugin(core::ResourceManager& resources);
    ~ResourcesPlugin() override;

    [[nodiscard]] const char* name() const override { return "Resources"; }
    [[nodiscard]] const char* category() const override { return "Assets"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    void setNativePlugins(core::NativePluginManager* plugins) { nativePlugins_ = plugins; }
    void setPluginApi(plugin::PluginHost* plugins) { pluginApi_ = plugins; }

private:
    void drawNativePlugins(core::ECS& ecs);
    void drawPluginApi();
    plugin::PluginHost* pluginApi_ = nullptr;

    core::NativePluginManager* nativePlugins_ = nullptr;
    core::ResourceManager* resources_;
    std::vector<core::BundleHandle> bundles_;
    std::string filter_;
    std::string status_;
    std::vector<std::string> recentReloads_;
    int listenerId_ = 0;
    float pollSeconds_ = 0.5f;
};

} // namespace engine::studio::plugins

#pragma once

#include <string>

#include "core/NativePluginManager.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/StudioNativePluginAbi.hpp"

namespace engine::core {
class Renderer;
} // namespace engine::core

namespace engine::studio {

// Kronos ("Native Plugin Architecture" -- Studio editor extensibility):
// bridges one dynamically loaded native plugin (a
// core::NativePluginManager slot) into the existing, tested
// studio::PluginManager infrastructure (drawMenu()/drawPanels()) as a
// real IStudioPlugin, so native plugins get the exact same menu/panel UI
// compiled-in plugins already have, with no new UI code in PluginManager
// itself.
//
// Deliberately does NOT cache a raw IStudioNativePluginExtension* across
// frames -- it re-resolves via NativePluginManager::queryExtension() by
// name on every call (update()/drawPanel()/drawMenuItems()), which is a
// real, honest no-op (drawing nothing) whenever the plugin is currently
// unloaded or mid-hot-swap. This is what makes a NativePluginAdapter safe
// to leave registered forever in PluginManager (which has no removal
// path -- see PluginManager.hpp's own comment) across any number of real
// unload/reload cycles of the underlying native plugin: there is never a
// stale pointer into a dlclose()'d library for this adapter to dereference.
class NativePluginAdapter final : public IStudioPlugin {
public:
    NativePluginAdapter(std::string pluginName, core::NativePluginManager& manager, core::Renderer& renderer);
    ~NativePluginAdapter() override;

    [[nodiscard]] const char* name() const override { return displayName_.c_str(); }
    [[nodiscard]] const char* category() const override { return displayCategory_.c_str(); }

    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected,
                   const std::vector<core::EntityId>& selectedEntities) override;
    void drawExtraMenuItems() override;

private:
    // Real re-resolve of this plugin's current IStudioNativePluginExtension
    // (nullptr if unloaded). Whenever the resolved pointer differs from
    // the last one seen (first attach, or a fresh instance after a
    // hot-swap), real-attaches it to the live ImGui editor context/
    // allocators (see IStudioNativePluginExtension::attachToEditor()'s own
    // comment on why that must happen before any other call) and refreshes
    // this adapter's own cached display name/category.
    [[nodiscard]] IStudioNativePluginExtension* resolve();

    std::string pluginName_;
    core::NativePluginManager& manager_;
    core::Renderer& renderer_;
    IStudioNativePluginExtension* lastSeen_ = nullptr;
    // Own copies (not the extension's raw const char*) so name()/category()
    // stay valid -- and keep showing this plugin's real menu entry -- even
    // while the underlying plugin is momentarily unloaded.
    std::string displayName_;
    std::string displayCategory_;
};

} // namespace engine::studio

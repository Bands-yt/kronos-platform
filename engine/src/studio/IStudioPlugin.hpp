#pragma once

#include <vector>

#include "core/ECS.hpp"

namespace engine::studio {

// The interface every Studio plugin implements -- first-party (native
// C++, compiled into the studio binary, registered at startup by
// PluginManager.hpp) today, and the seam a real third-party plugin
// system would implement against later.
//
// Modeled on Roblox Studio's own plugin shape: a toolbar button that
// toggles a dockable widget, called every frame regardless of whether
// that widget is visible so background work (an animation preview
// advancing, a selection listener) keeps running while the window is
// closed. What's NOT built yet: a plugin *package* format, a
// Luau-scripted plugin API surface (sandboxed the way core::Scripting's
// Luau VM already sandboxes gameplay scripts), or a publish/marketplace
// flow for third-party plugins -- see PluginManager.hpp's header comment
// for why that's a deliberate, stated next step rather than something
// half-built here.
class IStudioPlugin {
public:
    virtual ~IStudioPlugin() = default;

    [[nodiscard]] virtual const char* name() const = 0;     // toolbar button label + window title
    [[nodiscard]] virtual const char* category() const = 0; // grouping in the Plugins menu, e.g. "Animation"

    // Called once per frame regardless of open()/visibility -- see class
    // comment. `selected` is Explorer's primary selection; plugins that
    // want the full multi-select set take it via `selectedEntities` too.
    virtual void update(float dt, core::ECS& ecs, core::EntityId selected,
                         const std::vector<core::EntityId>& selectedEntities) {
        (void)dt;
        (void)ecs;
        (void)selected;
        (void)selectedEntities;
    }

    // Only called while isOpen() -- draws the plugin's own ImGui window(s).
    virtual void drawPanel(core::ECS& ecs, core::EntityId selected,
                            const std::vector<core::EntityId>& selectedEntities) = 0;

    [[nodiscard]] bool isOpen() const { return open_; }
    void setOpen(bool open) { open_ = open; }
    void toggleOpen() { open_ = !open_; }

protected:
    bool open_ = true; // first-party tools start docked/visible, matching Studio's default toolbox layout
};

} // namespace engine::studio

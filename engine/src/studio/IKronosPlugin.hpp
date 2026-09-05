#pragma once

#include <SDL2/SDL.h>

#include "core/ECS.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio {

// Kronos ("3D Mesh & CSG Editor" -- Beta Roadmap Phase 2, "windowed
// plugin module"): the seam a Studio tool that needs its own real OS
// window implements against -- distinct from IStudioPlugin.hpp's
// docked-ImGui-panel shape, which shares the main window's single
// swapchain. A plugin here owns a real, independent
// studio::SecondaryViewport (its own SDL2 window + Vulkan surface +
// swapchain), so it needs its own open/close lifecycle to actually
// create/destroy that GPU state -- IStudioPlugin::update() runs every
// frame *regardless* of isOpen() precisely because it has nothing GPU-
// resident to create/destroy; a closed IKronosPlugin has no window and
// no swapchain, so ticking/rendering it would be a real, honest no-op
// at best and a null-swapchain crash at worst.
//
// In-process only (compiled into the studio binary, registered by
// StudioApp::initialize() the same way PluginManager's first-party
// IStudioPlugins are) -- not a dlopen()'d shared-library plugin. A real
// out-of-process ABI is separate, deliberately unattempted scope, same
// stated tradeoff as IStudioPlugin.hpp's own header comment.
class IKronosPlugin {
public:
    virtual ~IKronosPlugin() = default;

    [[nodiscard]] virtual const char* name() const = 0;

    [[nodiscard]] bool isOpen() const { return open_; }

    // Real, idempotent state transitions -- open() on an already-open
    // plugin (or close() on an already-closed one) is a safe no-op, so a
    // menu toggle callback doesn't need to track its own "did I already
    // call this" state and can't double-create/double-destroy this
    // plugin's GPU resources by calling either twice.
    void open(core::Renderer& renderer) {
        if (open_) return;
        open_ = true;
        closeRequested_ = false;
        onOpen(renderer);
    }

    void close(core::Renderer& renderer) {
        if (!open_) return;
        open_ = false;
        closeRequested_ = false;
        onClose(renderer);
    }

    // Set by the plugin itself (e.g. its SecondaryViewport observing a
    // real SDL_WINDOWEVENT_CLOSE for its own window) -- the owner
    // (StudioApp/KronosPluginHost) polls this once per frame and calls
    // close() on it, rather than this class calling close() on itself
    // mid-tick/render.
    [[nodiscard]] bool closeRequested() const { return closeRequested_; }
    void requestClose() { closeRequested_ = true; }

    // CPU-side per-frame work while open (camera orbit input, polling
    // ECS/selection state) -- called before renderFrame(), same
    // dt/ecs shape as IStudioPlugin::update().
    virtual void tick(float dt, core::ECS& ecs, core::EntityId selected) {
        (void)dt;
        (void)ecs;
        (void)selected;
    }

    // Real GPU work against this plugin's own window/swapchain --
    // acquire, draw, present -- entirely separate from StudioApp's own
    // renderFrame() for the main window. Only called while isOpen().
    virtual void renderFrame(core::Renderer& renderer, core::ECS& ecs, core::EntityId selected) {
        (void)renderer;
        (void)ecs;
        (void)selected;
    }

    // Forwarded every raw SDL event, regardless of isOpen() -- see
    // KronosPluginHost::handleEvent()'s own comment for why this is a
    // dispatch (not a second SDL_PollEvent loop). Default no-op; a
    // plugin that owns a real SecondaryViewport overrides this to
    // forward into SecondaryViewport::handleEvent(), which does the
    // actual windowId() filtering.
    virtual void handleEvent(const SDL_Event& event) { (void)event; }

protected:
    // Real GPU/window creation and teardown -- called exactly once per
    // real open()/close() transition (see those methods' own idempotency
    // guard above), never on a no-op call.
    virtual void onOpen(core::Renderer& renderer) { (void)renderer; }
    virtual void onClose(core::Renderer& renderer) { (void)renderer; }

private:
    bool open_ = false;
    bool closeRequested_ = false;
};

} // namespace engine::studio

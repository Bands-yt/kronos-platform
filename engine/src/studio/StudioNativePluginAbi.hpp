#pragma once

#include <vector>

#include "core/ECS.hpp"

namespace engine::core {
class Renderer;
} // namespace engine::core

namespace engine::studio {

// Kronos ("Native Plugin Architecture" -- Studio editor extensibility):
// the optional second interface a dlopen()'d native plugin (see
// core::NativePluginManager/core::IHotReloadableModule) can implement to
// register a custom Studio editor panel and menu bar entry, mirroring
// IStudioPlugin.hpp's own shape for compiled-in plugins as closely as
// possible so studio::NativePluginAdapter can bridge one into the other
// with almost no new UI logic.
//
// Deliberately lives in studio/, NOT core/ -- core::IHotReloadableModule
// (which links into engine_runtime, a shipped game with no ImGui/Studio
// at all) only knows about a single, ImGui-agnostic
// queryExtension(const char*) -> void* hook (see HotReloadModuleAbi.hpp's
// own comment). A native plugin that wants Studio integration implements
// this interface and returns `static_cast<void*>(this)` for
// kInterfaceId from its own queryExtension() override; engine_core itself
// never needs to know this class exists.
//
// Cross-DSO ImGui note: Dear ImGui keeps its live UI state behind a
// single global context pointer. A plugin .so that statically links its
// own copy of imgui gets its OWN, separate context -- calling any
// ImGui:: function before adopting the host's real context/allocators
// would operate on an uninitialized context and likely crash. See
// attachToEditor()'s own comment for the required sequencing.
class IStudioNativePluginExtension {
public:
    // The string both a plugin's queryExtension() and Studio's own
    // lookup code agree on out of band -- bump the trailing version
    // number (a new "...v2" constant, not an edit to this one) if this
    // interface's shape ever changes, so an old plugin built against v1
    // is simply never found by a v2-only Studio instead of being
    // silently miscast.
    static constexpr const char* kInterfaceId = "kronos.studio.plugin.v1";

    virtual ~IStudioNativePluginExtension() = default;

    [[nodiscard]] virtual const char* panelName() const = 0;     // toolbar/window title + Plugins-menu label
    [[nodiscard]] virtual const char* category() const = 0;      // grouping in the Plugins menu

    // Called once, real-synchronously, right after this plugin is loaded
    // AND again after every real hot-swap thereafter (a freshly
    // hot-swapped module has its own fresh, uninitialized globals) --
    // BEFORE any other method on this interface is ever called. Must
    // adopt the allocators FIRST, then the context: setting the context
    // before the allocators are adopted means any allocation this DSO's
    // ImGui:: calls make during context adoption itself would use the
    // wrong heap. Concretely, the plugin's own .cpp implements this as:
    //
    //   void attachToEditor(void* ctx, void* allocFn, void* freeFn, void* userData) override {
    //       ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(allocFn),
    //                                     reinterpret_cast<ImGuiMemFreeFunc>(freeFn), userData);
    //       ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx));
    //   }
    //
    // and Studio's own caller passes ImGui::GetCurrentContext() and the
    // three values from ImGui::GetAllocatorFunctions(). All four
    // parameters are opaque void* here (not ImGuiContext*/ImGuiMemAllocFunc)
    // so this header itself never needs to #include <imgui.h> -- see this
    // class's own header comment on why that matters.
    virtual void attachToEditor(void* imguiContext, void* allocFn, void* freeFn, void* userData) = 0;

    // Called once right after attachToEditor() above, on first attach AND
    // again after every hot-swap (a fresh module instance means any
    // callback the PREVIOUS instance registered is real-removed by
    // NativePluginAdapter before this runs -- see that class's own
    // comment -- so this is always registering fresh, never leaking a
    // stale std::function into a dlclose()'d library). Default no-op --
    // most plugins have no render-time hook. A plugin that wants one
    // calls renderer.addPluginOverlayCallback(<its own panelName()>, ...)
    // here; see core::Renderer::addPluginOverlayCallback()'s own comment
    // for the exact pass/timing it runs at.
    virtual void registerRendererCallbacks(core::Renderer& renderer) { (void)renderer; }

    // Real per-tick logic, separate from IHotReloadableModule::tick() --
    // most plugins only need one or the other, but a Studio-integrated
    // plugin may want editor-only per-frame work (e.g. polling selection)
    // that shouldn't also run in a headless engine_runtime build. Default
    // no-op so a plugin that only cares about drawPanel() doesn't have to
    // override this.
    virtual void updateInEditor(float dt, core::ECS& ecs, core::EntityId selected,
                                 const std::vector<core::EntityId>& selectedEntities) {
        (void)dt;
        (void)ecs;
        (void)selected;
        (void)selectedEntities;
    }

    // Only called while isOpen() -- draws this plugin's own ImGui
    // window(s). Only ever called after attachToEditor() has run at least
    // once for the module instance currently loaded.
    virtual void drawPanel(core::ECS& ecs, core::EntityId selected,
                            const std::vector<core::EntityId>& selectedEntities) = 0;

    // Optional extra entries inside this plugin's own submenu in the
    // Plugins menu, below the automatic open/close toggle every plugin
    // already gets (see studio::NativePluginAdapter). Default no-op --
    // most plugins only need the panel toggle.
    virtual void drawMenuItems() {}

    [[nodiscard]] bool isOpen() const { return open_; }
    void setOpen(bool open) { open_ = open; }
    void toggleOpen() { open_ = !open_; }

protected:
    bool open_ = true;
};

} // namespace engine::studio

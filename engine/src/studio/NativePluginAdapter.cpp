#include "studio/NativePluginAdapter.hpp"

#include <algorithm>

#include <imgui.h>

#include "core/Renderer.hpp"

namespace engine::studio {

NativePluginAdapter::NativePluginAdapter(std::string pluginName, core::NativePluginManager& manager,
                                          core::Renderer& renderer)
    : pluginName_(std::move(pluginName)), manager_(manager), renderer_(renderer), displayName_(pluginName_),
      displayCategory_("Native Plugins") {}

NativePluginAdapter::~NativePluginAdapter() {
    // Real teardown of whatever renderer hook the currently loaded
    // instance (if any) registered -- see
    // IStudioNativePluginExtension::registerRendererCallbacks()'s own
    // comment. Safe to call unconditionally: removePluginOverlayCallback()
    // is a real no-op if this name was never registered.
    releaseRendererCallbacks();
}

void NativePluginAdapter::onPluginUnloading() {
    releaseRendererCallbacks();
    lastSeen_ = nullptr;
}

void NativePluginAdapter::releaseRendererCallbacks() {
    for (const std::string& name : ownedOverlays_) renderer_.removePluginOverlayCallback(name);
    ownedOverlays_.clear();
}

IStudioNativePluginExtension* NativePluginAdapter::resolve() {
    void* raw = manager_.queryExtension(pluginName_, IStudioNativePluginExtension::kInterfaceId);
    auto* extension = static_cast<IStudioNativePluginExtension*>(raw);
    if (extension != lastSeen_) {
        if (lastSeen_ != nullptr) {
            // The instance that registered this callback (if any) is gone
            // -- either unloaded outright, or superseded by a hot-swap --
            // so its std::function must be dropped before it's ever
            // invoked again, or the next overlay pass calls into a
            // dlclose()'d library.
            releaseRendererCallbacks();
        }
        lastSeen_ = extension;
        if (extension != nullptr) {
            // Allocators FIRST, then the context -- see
            // IStudioNativePluginExtension::attachToEditor()'s own comment
            // on why that order matters.
            ImGuiMemAllocFunc allocFn = nullptr;
            ImGuiMemFreeFunc freeFn = nullptr;
            void* userData = nullptr;
            ImGui::GetAllocatorFunctions(&allocFn, &freeFn, &userData);
            extension->attachToEditor(ImGui::GetCurrentContext(), reinterpret_cast<void*>(allocFn),
                                       reinterpret_cast<void*>(freeFn), userData);
            const std::vector<std::string> before = renderer_.pluginOverlayNames();
            extension->registerRendererCallbacks(renderer_);
            for (const std::string& name : renderer_.pluginOverlayNames()) {
                if (std::find(before.begin(), before.end(), name) == before.end()) ownedOverlays_.push_back(name);
            }
            displayName_ = extension->panelName();
            displayCategory_ = extension->category();
        }
    }
    return extension;
}

void NativePluginAdapter::update(float dt, core::ECS& ecs, core::EntityId selected,
                                  const std::vector<core::EntityId>& selectedEntities) {
    IStudioNativePluginExtension* extension = resolve();
    if (extension != nullptr) extension->updateInEditor(dt, ecs, selected, selectedEntities);
}

void NativePluginAdapter::drawPanel(core::ECS& ecs, core::EntityId selected,
                                     const std::vector<core::EntityId>& selectedEntities) {
    IStudioNativePluginExtension* extension = resolve();
    if (extension == nullptr) return;
    extension->setOpen(true);
    extension->drawPanel(ecs, selected, selectedEntities);
    if (!extension->isOpen()) setOpen(false);
}

void NativePluginAdapter::drawExtraMenuItems() {
    IStudioNativePluginExtension* extension = resolve();
    if (extension != nullptr) extension->drawMenuItems();
}

} // namespace engine::studio

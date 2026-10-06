// Kronos ("Native Plugin Architecture" -- Studio editor extensibility):
// the reference native plugin proving out core::IHotReloadableModule +
// studio::IStudioNativePluginExtension end-to-end -- a real, separately
// compiled .so that core::NativePluginManager::discover()/loadPlugin()
// finds and loads from ENGINE_NATIVE_PLUGIN_DIR, exposing:
//   - a plain gameplay tick() (IHotReloadableModule), so this plugin
//     works identically if dropped into a headless engine_runtime build
//     with no Studio/ImGui at all;
//   - a custom docked ImGui panel + a "Plugins" menu entry
//     (IStudioNativePluginExtension), bridged into Studio's existing
//     PluginManager UI by studio::NativePluginAdapter;
//   - a Renderer overlay hook (registerRendererCallbacks()) that counts
//     the frames it runs in; the panel shows the count.
//
// Rebuilding this file and re-launching Studio (or, for a live edit
// loop, re-running NativePluginManager::loadPlugin() with the same name
// against the freshly built .so) is the concrete way to exercise real
// hot-reload of a Studio-integrated plugin -- see
// CppHotReloadHost.hpp's own class comment for why "load again into the
// same slot" IS the reload path, with no separate reload() entry point.
#include <cstring>

#include "core/ECS.hpp"
#include "core/HotReloadLayoutFingerprint.hpp"
#include "core/HotReloadModuleAbi.hpp"
#include "core/Renderer.hpp"
#include "studio/StudioNativePluginAbi.hpp"

#include <imgui.h>

namespace {

class SampleStudioToolPlugin final : public engine::core::IHotReloadableModule,
                                      public engine::studio::IStudioNativePluginExtension {
public:
    void onLoad(engine::core::ECS& /*ecs*/) override { tickCount_ = 0; }

    // Plain gameplay tick -- runs whether or not Studio (or anything
    // Studio-specific) is present. Real, observable effect: counts real
    // ticks so drawPanel() below has something true to show, the same
    // "state lives where the loader can see it, not hidden" spirit as
    // this ABI's own onLoad() doc comment.
    void tick(float dt, engine::core::ECS& /*ecs*/) override {
        ++tickCount_;
        elapsedSeconds_ += dt;
    }

    [[nodiscard]] void* queryExtension(const char* interfaceId) override {
        if (std::strcmp(interfaceId, engine::studio::IStudioNativePluginExtension::kInterfaceId) == 0) {
            // static_cast (not reinterpret_cast) -- this class has two
            // real base subobjects, so the IStudioNativePluginExtension
            // pointer is NOT at the same address as `this`; the compiler
            // must apply the real base-offset adjustment here.
            return static_cast<engine::studio::IStudioNativePluginExtension*>(this);
        }
        return nullptr;
    }

    [[nodiscard]] const char* panelName() const override { return "Sample Studio Tool"; }
    [[nodiscard]] const char* category() const override { return "Samples"; }

    void attachToEditor(void* imguiContext, void* allocFn, void* freeFn, void* userData) override {
        // Allocators first, then the context -- see this method's own
        // interface-level comment for why the order matters.
        ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(allocFn),
                                      reinterpret_cast<ImGuiMemFreeFunc>(freeFn), userData);
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(imguiContext));
    }

    void registerRendererCallbacks(engine::core::Renderer& renderer) override {
        renderer.addPluginOverlayCallback(
            panelName(), [this](VkCommandBuffer, VkImageView, VkExtent2D) { ++overlayFrames_; });
    }

    void drawPanel(engine::core::ECS& /*ecs*/, engine::core::EntityId /*selected*/,
                   const std::vector<engine::core::EntityId>& /*selectedEntities*/) override {
        if (!ImGui::Begin(panelName(), &open_)) {
            ImGui::End();
            return;
        }
        ImGui::Text("Loaded from a real, separately compiled .so.");
        ImGui::Text("Gameplay ticks so far: %d", tickCount_);
        ImGui::Text("Elapsed: %.2fs", static_cast<double>(elapsedSeconds_));
        ImGui::Text("Renderer overlay frames: %d", overlayFrames_);
        ImGui::Separator();
        ImGui::Text("Rebuild this file and reload the plugin to see this");
        ImGui::Text("panel's own code change live -- that's the hot-reload proof.");
        ImGui::End();
    }

    void drawMenuItems() override {
        if (ImGui::MenuItem("Reset Tick Counter")) {
            tickCount_ = 0;
            elapsedSeconds_ = 0.0f;
        }
    }

private:
    int tickCount_ = 0;
    float elapsedSeconds_ = 0.0f;
    int overlayFrames_ = 0;
};

} // namespace

extern "C" int kronosHotReloadAbiVersion() { return engine::core::kHotReloadModuleAbiVersion; }
extern "C" unsigned long long kronosHotReloadLayoutFingerprint() { return engine::core::kHotReloadLayoutFingerprint; }
extern "C" engine::core::IHotReloadableModule* kronosCreateHotReloadModule() { return new SampleStudioToolPlugin(); }
extern "C" void kronosDestroyHotReloadModule(engine::core::IHotReloadableModule* module) { delete module; }

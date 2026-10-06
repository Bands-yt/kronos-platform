#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/CppHotReloadHost.hpp"

namespace engine::core {

class ECS;

// Kronos ("Native Plugin Architecture"): the manager for dynamically
// loading/unloading custom engine tools and subsystems as real, separately
// compiled shared libraries (.so/.dll), on top of CppHotReloadHost's N-slot
// dlopen()/LoadLibrary() infrastructure. This is deliberately NOT the same
// system as studio::PluginManager (an in-process std::vector<IStudioPlugin>
// compiled directly into the studio binary, never dlopen'd) or
// core::PluginRegistry (an unused, in-process metadata catalogue with no-op
// initialize()/shutdown()) -- those two names were already taken by
// unrelated, in-process-only systems, hence this class's own distinct name.
//
// The plugin ABI a native plugin implements is IHotReloadableModule itself
// (see HotReloadModuleAbi.hpp) -- onLoad(ECS&)/onUnload()/tick(float, ECS&)
// plus the exported kronosHotReloadAbiVersion()/kronosCreateHotReloadModule()/
// kronosDestroyHotReloadModule() C symbols CppHotReloadHost resolves and
// version-checks before trusting anything else in the library. A "hot-
// reloadable gameplay module" and a "dynamically loaded engine plugin" are
// the same real shape from the loader's point of view -- both are just code
// that can be swapped in and out of a running process without restarting it
// -- so this class does not invent a second, parallel interface for the
// same contract. What it adds on top of CppHotReloadHost is the
// plugin-specific bookkeeping a hot-reload workflow doesn't need: real
// directory discovery of installed plugins, and tracking each loaded
// plugin's own source path so a caller can list what's currently active
// without reaching into CppHotReloadHost's own private slot storage.
class NativePluginManager {
public:
    // A file found on disk by discover() -- not yet loaded. Kept as its own
    // type (distinct from LoadedPluginInfo below) so "found this .so on
    // disk" and "this plugin is currently loaded" can never be confused at
    // a call site, matching studio::scanLocalPluginDirectory's own
    // DiscoveredPlugin/loaded-plugin distinction.
    struct DiscoveredPlugin {
        std::string name;
        std::string libraryPath;
    };

    struct LoadedPluginInfo {
        std::string name;
        std::string libraryPath;
        int64_t fileStamp = 0;
        int64_t pendingStamp = 0;
        std::string lastError; // latest failed reload; the previous build keeps running
    };

    // The real, platform-native shared library extension a discoverable
    // plugin file must have -- ".dll" on Windows, ".so" everywhere else
    // (matching CppHotReloadHost's own #if defined(_WIN32) split).
    static const char* nativeLibraryExtension();

    // Real, non-recursive scan of `directoryPath` for files ending in
    // nativeLibraryExtension() -- each match's filename stem (no directory,
    // no extension) becomes that discovered plugin's suggested slot name.
    // An honest empty result (not an error) if the directory doesn't exist
    // or has no matching files, matching this codebase's own "missing input
    // is a real zero-result answer" convention (see
    // studio::scanLocalPluginDirectory's identical shape for the Lua/
    // manifest-based plugin system). Pure discovery -- never calls dlopen()/
    // LoadLibrary(), never touches any already-loaded plugin.
    [[nodiscard]] static std::vector<DiscoveredPlugin> discover(const std::string& directoryPath);

    // Real load of `libraryPath` into the named plugin slot -- delegates
    // directly to CppHotReloadHost::load() (see that class's own comment
    // for the full load/reload contract) and, only on success, records
    // `libraryPath` so listLoadedPlugins() can report it later. Calling
    // this again with the same `name` is a real hot-swap of that plugin
    // (its own onUnload() runs, then the freshly loaded module's onLoad()
    // runs) -- every other loaded plugin is untouched.
    [[nodiscard]] bool loadPlugin(const std::string& name, const std::string& libraryPath, ECS& ecs,
                                   std::string& outError);

    // Real, explicit unload of one plugin (CppHotReloadHost::unloadSlot())
    // plus removal of its bookkeeping entry. A no-op if `name` isn't
    // currently loaded.
    void unloadPlugin(const std::string& name);

    // See CppHotReloadHost::setBeforeUnload.
    void setBeforeUnload(std::function<void(const std::string& name)> callback) { host_.setBeforeUnload(std::move(callback)); }

    // Real per-tick forward to every currently loaded plugin, in the order
    // each was first loaded -- see CppHotReloadHost::tick()'s own comment.
    void tick(float dt, ECS& ecs);

    // Reloads every plugin whose library file changed on disk, right now.
    // Returns how many reloaded. A failed reload leaves the old build
    // running and is not retried until the file changes again.
    size_t checkForChanges(ECS& ecs);
    bool reloadPlugin(const std::string& name, ECS& ecs, std::string& outError);

    // With auto reload on, update() polls library files and reloads a
    // plugin once its file has stopped changing for one poll interval, so
    // a half-written build is never loaded.
    void setAutoReload(bool enabled, double pollSeconds = 0.5);
    [[nodiscard]] bool autoReloadEnabled() const { return autoReload_; }
    size_t update(ECS& ecs);

    [[nodiscard]] std::optional<CppHotReloadHost::SlotStatus> status(const std::string& name) const {
        return host_.status(name);
    }

    [[nodiscard]] bool isPluginLoaded(const std::string& name) const;
    [[nodiscard]] const std::vector<LoadedPluginInfo>& listLoadedPlugins() const { return loaded_; }
    [[nodiscard]] size_t pluginCount() const { return loaded_.size(); }

    // Real forward to the named plugin's own
    // IHotReloadableModule::queryExtension() -- see
    // CppHotReloadHost::queryExtension()'s own comment on why this is
    // always safe to call again after a hot-swap, unlike caching a raw
    // module pointer would be. nullptr if `name` isn't loaded or doesn't
    // implement `interfaceId`. This is the one seam studio::
    // NativePluginAdapter uses to reach a native plugin's optional
    // IStudioNativePluginExtension without engine_core ever needing to
    // know that interface exists.
    [[nodiscard]] void* queryExtension(const std::string& name, const char* interfaceId) const {
        return host_.queryExtension(name, interfaceId);
    }

private:
    CppHotReloadHost host_;
    // Insertion-ordered bookkeeping of what's currently loaded, kept in
    // sync with host_'s own slots by loadPlugin()/unloadPlugin() -- lets
    // listLoadedPlugins() report each plugin's own source path without
    // CppHotReloadHost needing to expose that itself.
    std::vector<LoadedPluginInfo> loaded_;
    bool autoReload_ = false;
    double pollSeconds_ = 0.5;
    std::chrono::steady_clock::time_point lastPoll_{};
};

} // namespace engine::core

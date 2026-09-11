#include "core/NativePluginManager.hpp"

#include <algorithm>
#include <filesystem>

namespace engine::core {

namespace {
namespace fs = std::filesystem;
} // namespace

const char* NativePluginManager::nativeLibraryExtension() {
#if defined(_WIN32)
    return ".dll";
#else
    return ".so";
#endif
}

std::vector<NativePluginManager::DiscoveredPlugin> NativePluginManager::discover(const std::string& directoryPath) {
    std::vector<DiscoveredPlugin> result;
    std::error_code ec;
    if (!fs::is_directory(directoryPath, ec)) return result; // honest empty result, not an error

    for (const auto& entry : fs::directory_iterator(directoryPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const fs::path& path = entry.path();
        if (path.extension() != nativeLibraryExtension()) continue;
        result.push_back(DiscoveredPlugin{path.stem().string(), path.string()});
    }
    return result;
}

bool NativePluginManager::loadPlugin(const std::string& name, const std::string& libraryPath, ECS& ecs,
                                      std::string& outError) {
    if (!host_.load(name, libraryPath, ecs, outError)) return false;

    auto it = std::find_if(loaded_.begin(), loaded_.end(),
                            [&](const LoadedPluginInfo& info) { return info.name == name; });
    if (it != loaded_.end()) {
        it->libraryPath = libraryPath; // real reload of an already-tracked plugin -- same slot, new source path
    } else {
        loaded_.push_back(LoadedPluginInfo{name, libraryPath});
    }
    return true;
}

void NativePluginManager::unloadPlugin(const std::string& name) {
    host_.unloadSlot(name);
    loaded_.erase(std::remove_if(loaded_.begin(), loaded_.end(),
                                  [&](const LoadedPluginInfo& info) { return info.name == name; }),
                  loaded_.end());
}

void NativePluginManager::tick(float dt, ECS& ecs) { host_.tick(dt, ecs); }

bool NativePluginManager::isPluginLoaded(const std::string& name) const { return host_.hasModuleLoaded(name); }

} // namespace engine::core

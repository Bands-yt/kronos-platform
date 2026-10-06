#include "core/NativePluginManager.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace engine::core {

namespace {
namespace fs = std::filesystem;

int64_t libraryStamp(const std::string& path) {
    std::error_code ec;
    auto time = fs::last_write_time(path, ec);
    if (ec) return 0;
    auto size = fs::file_size(path, ec);
    if (ec) return 0;
    return static_cast<int64_t>(time.time_since_epoch().count()) ^ static_cast<int64_t>(size);
}
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
    if (it == loaded_.end()) it = loaded_.insert(loaded_.end(), LoadedPluginInfo{name, libraryPath});
    it->libraryPath = libraryPath;
    it->fileStamp = libraryStamp(libraryPath);
    it->pendingStamp = 0;
    it->lastError.clear();
    return true;
}

bool NativePluginManager::reloadPlugin(const std::string& name, ECS& ecs, std::string& outError) {
    auto it = std::find_if(loaded_.begin(), loaded_.end(), [&](const LoadedPluginInfo& info) { return info.name == name; });
    if (it == loaded_.end()) {
        outError = "plugin '" + name + "' is not loaded";
        return false;
    }
    const std::string path = it->libraryPath;
    const int64_t stamp = libraryStamp(path);
    if (loadPlugin(name, path, ecs, outError)) return true;
    it = std::find_if(loaded_.begin(), loaded_.end(), [&](const LoadedPluginInfo& info) { return info.name == name; });
    if (it != loaded_.end()) {
        it->fileStamp = stamp;
        it->pendingStamp = 0;
        it->lastError = outError;
    }
    return false;
}

size_t NativePluginManager::checkForChanges(ECS& ecs) {
    std::vector<std::string> changed;
    for (const auto& info : loaded_) {
        const int64_t stamp = libraryStamp(info.libraryPath);
        if (stamp != 0 && stamp != info.fileStamp) changed.push_back(info.name);
    }
    size_t reloaded = 0;
    for (const auto& name : changed) {
        std::string error;
        if (reloadPlugin(name, ecs, error)) ++reloaded;
    }
    return reloaded;
}

void NativePluginManager::setAutoReload(bool enabled, double pollSeconds) {
    autoReload_ = enabled;
    pollSeconds_ = pollSeconds > 0.0 ? pollSeconds : 0.5;
}

size_t NativePluginManager::update(ECS& ecs) {
    if (!autoReload_ || loaded_.empty()) return 0;
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - lastPoll_).count() < pollSeconds_) return 0;
    lastPoll_ = now;

    std::vector<std::string> ready;
    for (auto& info : loaded_) {
        const int64_t stamp = libraryStamp(info.libraryPath);
        if (stamp == 0 || stamp == info.fileStamp) {
            info.pendingStamp = 0;
        } else if (stamp == info.pendingStamp) {
            ready.push_back(info.name);
        } else {
            info.pendingStamp = stamp;
        }
    }
    size_t reloaded = 0;
    for (const auto& name : ready) {
        std::string error;
        if (reloadPlugin(name, ecs, error)) {
            ++reloaded;
        } else {
            std::fprintf(stderr, "NativePluginManager: reloading '%s' failed, keeping the previous build: %s\n",
                         name.c_str(), error.c_str());
        }
    }
    return reloaded;
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

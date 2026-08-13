#include "core/AssetCache.hpp"

#include <chrono>
#include <filesystem>

namespace engine::core {

int64_t fileWriteTimeSeconds(const std::string& path) {
    std::error_code ec;
    auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec) return kUnknownWriteTime;
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(writeTime.time_since_epoch()).count());
}

bool assetNeedsReload(int64_t cachedWriteTimeSeconds, int64_t currentWriteTimeSeconds) {
    if (cachedWriteTimeSeconds == kUnknownWriteTime || currentWriteTimeSeconds == kUnknownWriteTime) return true;
    return cachedWriteTimeSeconds != currentWriteTimeSeconds;
}

} // namespace engine::core

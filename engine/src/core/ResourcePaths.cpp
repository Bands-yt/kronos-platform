#include "core/ResourcePaths.hpp"

#include <filesystem>

#if defined(__linux__)
#include <climits>
#include <unistd.h>
#endif

namespace engine::core {

std::string executableDirectory() {
#if defined(__linux__)
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return {};
    buffer[length] = '\0';
    return std::filesystem::path(buffer).parent_path().string();
#else
    return {};
#endif
}

std::string resolveResourceDir(const std::string& exeDir, const std::string& subdirName,
                                const std::string& compileTimeFallback) {
    if (!exeDir.empty()) {
        std::string candidate = exeDir + "/" + subdirName;
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec) && !ec) return candidate;
    }
    return compileTimeFallback;
}

} // namespace engine::core

#include "core/ResourcePaths.hpp"

#include <filesystem>

#if defined(__linux__)
#include <climits>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace engine::core {

std::string executableDirectory() {
#if defined(__linux__)
    char buffer[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return {};
    buffer[length] = '\0';
    return std::filesystem::path(buffer).parent_path().string();
#elif defined(_WIN32)
    // Kronos ("Windows Studio Launch Crash" -- Jay's issue): real root
    // cause of a packaged kronos-windows-x64 build silently opening and
    // closing. This returned an honest "" here unconditionally before
    // this fix (matching CrashReporter.hpp's own Linux-only precedent at
    // the time), which meant resolveResourceDir() below always fell back
    // to compileTimeFallback -- an absolute path baked in on whichever
    // machine *built* the installer, not the machine running it. On any
    // Windows box other than that build machine, shader/asset lookup
    // (Renderer::createScenePipeline() et al., see Renderer.cpp's own
    // ENGINE_SHADER_DIR call sites) failed outright, StudioApp::initialize()
    // returned false, and -- with no visible error surface on Windows,
    // see StudioMain.cpp's own comment -- the process just exited.
    // GetModuleFileNameW(nullptr, ...) is the real, standard Win32 way to
    // ask "what file is the currently-running process's own image", same
    // "real Win32 API for a real per-platform primitive" precedent
    // CredentialStoreWindows.cpp already established for DPAPI. A path
    // buffer that fills completely (return value == size) is real,
    // documented MAX_PATH-truncation ambiguity -- there's no unambiguous
    // real directory to return in that case, so this honestly returns ""
    // (the same "no packaged-layout candidate available" fallback the
    // readlink() failure path above already uses) rather than handing
    // back a silently-truncated, wrong path.
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) return {};
    return std::filesystem::path(buffer, buffer + length).parent_path().string();
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

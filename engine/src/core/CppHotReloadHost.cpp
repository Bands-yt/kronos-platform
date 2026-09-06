#include "core/CppHotReloadHost.hpp"

#include <filesystem>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#include "core/ECS.hpp"

namespace engine::core {

namespace {
namespace fs = std::filesystem;
}

bool CppHotReloadHost::loadInto(const std::string& sharedLibraryPath, LoadedLibrary& out, std::string& outError) {
#if defined(_WIN32)
    (void)sharedLibraryPath;
    (void)out;
    outError = "CppHotReloadHost: Windows is not implemented (POSIX dlopen only) -- see this class's own header comment";
    return false;
#else
    std::error_code ec;
    if (!fs::exists(sharedLibraryPath, ec)) {
        outError = "CppHotReloadHost: shared library not found: " + sharedLibraryPath;
        return false;
    }

    // A fresh, never-before-seen path every call -- see this class's own
    // header comment on why dlopen()'s per-path cache otherwise makes a
    // second load() of the *same* built .so silently return stale code.
    std::string tempPath = sharedLibraryPath + ".hotreload_" + std::to_string(nextTempSuffix_++);
    fs::copy_file(sharedLibraryPath, tempPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "CppHotReloadHost: failed to copy '" + sharedLibraryPath + "' to temp path: " + ec.message();
        return false;
    }

    void* handle = dlopen(tempPath.c_str(), RTLD_NOW);
    if (!handle) {
        outError = std::string("CppHotReloadHost: dlopen() failed: ") + dlerror();
        fs::remove(tempPath, ec);
        return false;
    }

    auto abiVersionFn = reinterpret_cast<HotReloadAbiVersionFn>(dlsym(handle, kHotReloadAbiVersionSymbol));
    auto createFn = reinterpret_cast<HotReloadCreateModuleFn>(dlsym(handle, kHotReloadCreateModuleSymbol));
    auto destroyFn = reinterpret_cast<HotReloadDestroyModuleFn>(dlsym(handle, kHotReloadDestroyModuleSymbol));
    if (!abiVersionFn || !createFn || !destroyFn) {
        outError = std::string("CppHotReloadHost: module is missing one or more required exported symbols (") +
                   kHotReloadAbiVersionSymbol + "/" + kHotReloadCreateModuleSymbol + "/" + kHotReloadDestroyModuleSymbol + ")";
        dlclose(handle);
        fs::remove(tempPath, ec);
        return false;
    }

    int moduleAbiVersion = abiVersionFn();
    if (moduleAbiVersion != kHotReloadModuleAbiVersion) {
        outError = "CppHotReloadHost: ABI version mismatch (module=" + std::to_string(moduleAbiVersion) +
                   ", host=" + std::to_string(kHotReloadModuleAbiVersion) + ")";
        dlclose(handle);
        fs::remove(tempPath, ec);
        return false;
    }

    IHotReloadableModule* module = createFn();
    if (!module) {
        outError = "CppHotReloadHost: module's create function returned nullptr";
        dlclose(handle);
        fs::remove(tempPath, ec);
        return false;
    }

    out.handle = handle;
    out.module = module;
    out.destroyFn = destroyFn;
    out.tempPath = tempPath;
    return true;
#endif
}

void CppHotReloadHost::unload(LoadedLibrary& lib) {
    if (lib.module) {
        lib.module->onUnload();
        if (lib.destroyFn) lib.destroyFn(lib.module);
        lib.module = nullptr;
    }
#if !defined(_WIN32)
    if (lib.handle) {
        dlclose(lib.handle);
        lib.handle = nullptr;
    }
    if (!lib.tempPath.empty()) {
        std::error_code ec;
        fs::remove(lib.tempPath, ec);
        lib.tempPath.clear();
    }
#endif
    lib.destroyFn = nullptr;
}

bool CppHotReloadHost::load(const std::string& sharedLibraryPath, ECS& ecs, std::string& outError) {
    LoadedLibrary fresh;
    if (!loadInto(sharedLibraryPath, fresh, outError)) return false;

    unload(current_); // real teardown of whatever was loaded before, if anything -- a no-op the first time
    current_ = fresh;
    current_.module->onLoad(ecs);
    return true;
}

void CppHotReloadHost::tick(float dt, ECS& ecs) {
    if (current_.module) current_.module->tick(dt, ecs);
}

CppHotReloadHost::~CppHotReloadHost() { unload(current_); }

} // namespace engine::core

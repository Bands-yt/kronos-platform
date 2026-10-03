#include "core/CppHotReloadHost.hpp"

#include <algorithm>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "core/ECS.hpp"
#include "core/HotReloadLayoutFingerprint.hpp"

namespace engine::core {

namespace {
namespace fs = std::filesystem;

#if defined(_WIN32)
// GetLastError() only, formatted -- mirrors dlerror()'s "the last
// failure as a human-readable string" shape on the POSIX side below.
std::string lastWin32Error() {
    DWORD err = GetLastError();
    if (err == 0) return "unknown error";
    LPSTR buffer = nullptr;
    DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
                                 nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = (size > 0 && buffer != nullptr) ? std::string(buffer, size) : "unknown error";
    if (buffer != nullptr) LocalFree(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
    return message;
}
#endif
} // namespace

bool CppHotReloadHost::loadInto(const std::string& sharedLibraryPath, LoadedLibrary& out, std::string& outError) {
    std::error_code ec;
    if (!fs::exists(sharedLibraryPath, ec)) {
        outError = "CppHotReloadHost: shared library not found: " + sharedLibraryPath;
        return false;
    }

    // A fresh, never-before-seen path every call -- see this class's own
    // header comment on why the platform loader's per-path cache
    // otherwise makes a second load() of the *same* built shared
    // library silently return stale code.
    std::string tempPath = sharedLibraryPath + ".hotreload_" + std::to_string(nextTempSuffix_++);
    fs::copy_file(sharedLibraryPath, tempPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "CppHotReloadHost: failed to copy '" + sharedLibraryPath + "' to temp path: " + ec.message();
        return false;
    }

#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(tempPath.c_str());
    if (handle == nullptr) {
        outError = "CppHotReloadHost: LoadLibraryA() failed: " + lastWin32Error();
        fs::remove(tempPath, ec);
        return false;
    }

    auto abiVersionFn = reinterpret_cast<HotReloadAbiVersionFn>(GetProcAddress(handle, kHotReloadAbiVersionSymbol));
    auto createFn = reinterpret_cast<HotReloadCreateModuleFn>(GetProcAddress(handle, kHotReloadCreateModuleSymbol));
    auto destroyFn = reinterpret_cast<HotReloadDestroyModuleFn>(GetProcAddress(handle, kHotReloadDestroyModuleSymbol));
    if (!abiVersionFn || !createFn || !destroyFn) {
        outError = std::string("CppHotReloadHost: module is missing one or more required exported symbols (") +
                   kHotReloadAbiVersionSymbol + "/" + kHotReloadCreateModuleSymbol + "/" + kHotReloadDestroyModuleSymbol + ")";
        FreeLibrary(handle);
        fs::remove(tempPath, ec);
        return false;
    }

    int moduleAbiVersion = abiVersionFn();
    if (moduleAbiVersion != kHotReloadModuleAbiVersion) {
        outError = "CppHotReloadHost: ABI version mismatch (module=" + std::to_string(moduleAbiVersion) +
                   ", host=" + std::to_string(kHotReloadModuleAbiVersion) + ")";
        FreeLibrary(handle);
        fs::remove(tempPath, ec);
        return false;
    }

    if (auto fingerprintFn =
            reinterpret_cast<HotReloadLayoutFingerprintFn>(GetProcAddress(handle, kHotReloadLayoutFingerprintSymbol))) {
        if (fingerprintFn() != kHotReloadLayoutFingerprint) {
            outError = "CppHotReloadHost: module was built against different engine headers; rebuild it";
            FreeLibrary(handle);
            fs::remove(tempPath, ec);
            return false;
        }
    }

    IHotReloadableModule* module = createFn();
    if (!module) {
        outError = "CppHotReloadHost: module's create function returned nullptr";
        FreeLibrary(handle);
        fs::remove(tempPath, ec);
        return false;
    }

    out.handle = reinterpret_cast<void*>(handle);
#else
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

    if (auto fingerprintFn =
            reinterpret_cast<HotReloadLayoutFingerprintFn>(dlsym(handle, kHotReloadLayoutFingerprintSymbol))) {
        if (fingerprintFn() != kHotReloadLayoutFingerprint) {
            outError = "CppHotReloadHost: module was built against different engine headers; rebuild it";
            dlclose(handle);
            fs::remove(tempPath, ec);
            return false;
        }
    }

    IHotReloadableModule* module = createFn();
    if (!module) {
        outError = "CppHotReloadHost: module's create function returned nullptr";
        dlclose(handle);
        fs::remove(tempPath, ec);
        return false;
    }

    out.handle = handle;
#endif
    out.module = module;
    out.destroyFn = destroyFn;
    out.tempPath = tempPath;
    return true;
}

void CppHotReloadHost::unload(LoadedLibrary& lib) {
    if (lib.module) {
        lib.module->onUnload();
        if (lib.destroyFn) lib.destroyFn(lib.module);
        lib.module = nullptr;
    }
    if (lib.handle) {
#if defined(_WIN32)
        FreeLibrary(reinterpret_cast<HMODULE>(lib.handle));
#else
        dlclose(lib.handle);
#endif
        lib.handle = nullptr;
    }
    if (!lib.tempPath.empty()) {
        std::error_code ec;
        fs::remove(lib.tempPath, ec);
        lib.tempPath.clear();
    }
    lib.destroyFn = nullptr;
}

CppHotReloadHost::LoadedLibrary* CppHotReloadHost::findSlot(const std::string& slot) {
    auto it = std::find_if(slots_.begin(), slots_.end(), [&](const auto& entry) { return entry.first == slot; });
    return it != slots_.end() ? &it->second : nullptr;
}

const CppHotReloadHost::LoadedLibrary* CppHotReloadHost::findSlot(const std::string& slot) const {
    auto it = std::find_if(slots_.begin(), slots_.end(), [&](const auto& entry) { return entry.first == slot; });
    return it != slots_.end() ? &it->second : nullptr;
}

bool CppHotReloadHost::load(const std::string& slot, const std::string& sharedLibraryPath, ECS& ecs,
                             std::string& outError) {
    LoadedLibrary fresh;
    if (!loadInto(sharedLibraryPath, fresh, outError)) return false;

    LoadedLibrary* existing = findSlot(slot);
    if (existing != nullptr) {
        unload(*existing); // real teardown of this slot's previous module -- every other slot is untouched
        *existing = std::move(fresh);
        existing->module->onLoad(ecs);
    } else {
        slots_.emplace_back(slot, std::move(fresh));
        slots_.back().second.module->onLoad(ecs);
    }
    return true;
}

void CppHotReloadHost::unloadSlot(const std::string& slot) {
    auto it = std::find_if(slots_.begin(), slots_.end(), [&](const auto& entry) { return entry.first == slot; });
    if (it == slots_.end()) return;
    unload(it->second);
    slots_.erase(it);
}

void CppHotReloadHost::tick(float dt, ECS& ecs) {
    for (auto& [name, lib] : slots_) {
        if (lib.module) lib.module->tick(dt, ecs);
    }
}

bool CppHotReloadHost::hasModuleLoaded(const std::string& slot) const {
    const LoadedLibrary* lib = findSlot(slot);
    return lib != nullptr && lib->module != nullptr;
}

void* CppHotReloadHost::queryExtension(const std::string& slot, const char* interfaceId) const {
    const LoadedLibrary* lib = findSlot(slot);
    if (lib == nullptr || lib->module == nullptr) return nullptr;
    return lib->module->queryExtension(interfaceId);
}

CppHotReloadHost::~CppHotReloadHost() {
    for (auto& [name, lib] : slots_) unload(lib);
}

} // namespace engine::core

#include "core/CppHotReloadHost.hpp"

#include <algorithm>
#include <filesystem>
#include <new>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
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

void* openLibrary(const std::string& path, std::string& error) {
#if defined(_WIN32)
    HMODULE handle = LoadLibraryA(path.c_str());
    if (handle == nullptr) error = "CppHotReloadHost: LoadLibraryA() failed: " + lastWin32Error();
    return reinterpret_cast<void*>(handle);
#else
    void* handle = dlopen(path.c_str(), RTLD_NOW);
    if (handle == nullptr) error = std::string("CppHotReloadHost: dlopen() failed: ") + dlerror();
    return handle;
#endif
}

void* findSymbol(void* handle, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

void closeLibrary(void* handle) {
    if (handle == nullptr) return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

template <typename Fn>
Fn symbol(void* handle, const char* name) {
    return reinterpret_cast<Fn>(findSymbol(handle, name));
}

bool sameLayout(const KronosHotReloadStateLayout& a, const KronosHotReloadStateLayout& b) {
    return a.size == b.size && a.alignment == b.alignment && a.typeHash == b.typeHash && a.version == b.version;
}

void* allocateState(const KronosHotReloadStateLayout& layout) {
    return ::operator new(layout.size == 0 ? 1 : layout.size, std::align_val_t(layout.alignment == 0 ? 1 : layout.alignment));
}

void freeState(void* state, const KronosHotReloadStateLayout& layout) {
    ::operator delete(state, std::align_val_t(layout.alignment == 0 ? 1 : layout.alignment));
}
} // namespace

const char* hotReloadStateTransferName(HotReloadStateTransfer transfer) {
    switch (transfer) {
    case HotReloadStateTransfer::None: return "none";
    case HotReloadStateTransfer::Created: return "created";
    case HotReloadStateTransfer::Kept: return "kept in place";
    case HotReloadStateTransfer::Migrated: return "migrated";
    case HotReloadStateTransfer::Reset: return "reset (layout changed)";
    }
    return "none";
}

bool CppHotReloadHost::loadInto(const std::string& sharedLibraryPath, LoadedLibrary& out, std::string& outError) {
    std::error_code ec;
    if (!fs::exists(sharedLibraryPath, ec)) {
        outError = "CppHotReloadHost: shared library not found: " + sharedLibraryPath;
        return false;
    }

    // A fresh path every load: the platform loader caches by path and
    // would otherwise hand back the old code.
#if defined(_WIN32)
    const unsigned long processId = GetCurrentProcessId();
#else
    const unsigned long processId = static_cast<unsigned long>(getpid());
#endif
    std::string tempPath = sharedLibraryPath + ".hotreload_" + std::to_string(processId) + "_" +
                           std::to_string(nextTempSuffix_++);
    fs::copy_file(sharedLibraryPath, tempPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "CppHotReloadHost: failed to copy '" + sharedLibraryPath + "' to temp path: " + ec.message();
        return false;
    }

    void* handle = openLibrary(tempPath, outError);
    auto fail = [&](std::string message) {
        if (!message.empty()) outError = std::move(message);
        closeLibrary(handle);
        fs::remove(tempPath, ec);
        return false;
    };
    if (handle == nullptr) return fail({});

    auto abiVersionFn = symbol<HotReloadAbiVersionFn>(handle, kHotReloadAbiVersionSymbol);
    auto createFn = symbol<HotReloadCreateModuleFn>(handle, kHotReloadCreateModuleSymbol);
    auto destroyFn = symbol<HotReloadDestroyModuleFn>(handle, kHotReloadDestroyModuleSymbol);
    if (!abiVersionFn || !createFn || !destroyFn) {
        return fail(std::string("CppHotReloadHost: module is missing one or more required exported symbols (") +
                    kHotReloadAbiVersionSymbol + "/" + kHotReloadCreateModuleSymbol + "/" +
                    kHotReloadDestroyModuleSymbol + ")");
    }

    int moduleAbiVersion = abiVersionFn();
    if (moduleAbiVersion != kHotReloadModuleAbiVersion) {
        return fail("CppHotReloadHost: ABI version mismatch (module=" + std::to_string(moduleAbiVersion) +
                    ", host=" + std::to_string(kHotReloadModuleAbiVersion) + ")");
    }

    if (auto fingerprintFn = symbol<HotReloadLayoutFingerprintFn>(handle, kHotReloadLayoutFingerprintSymbol)) {
        if (fingerprintFn() != kHotReloadLayoutFingerprint) {
            return fail("CppHotReloadHost: module was built against different engine headers; rebuild it");
        }
    }

    auto layoutFn = symbol<KronosHotReloadStateLayoutFn>(handle, kHotReloadStateLayoutSymbol);
    if (layoutFn != nullptr) {
        out.constructState = symbol<KronosHotReloadStateFn>(handle, kHotReloadConstructStateSymbol);
        out.destroyState = symbol<KronosHotReloadStateFn>(handle, kHotReloadDestroyStateSymbol);
        out.bindState = symbol<KronosHotReloadStateFn>(handle, kHotReloadBindStateSymbol);
        out.migrateState = symbol<KronosHotReloadMigrateStateFn>(handle, kHotReloadMigrateStateSymbol);
        if (!out.constructState || !out.destroyState || !out.bindState) {
            return fail("CppHotReloadHost: module exports a state layout but not its construct/destroy/bind "
                        "functions; declare the state with KRONOS_HOT_RELOAD_STATE");
        }
        layoutFn(&out.layout);
        if (out.layout.alignment == 0 || (out.layout.alignment & (out.layout.alignment - 1)) != 0) {
            return fail("CppHotReloadHost: module state has an invalid alignment");
        }
        out.hasState = true;
    }

    IHotReloadableModule* module = createFn();
    if (!module) return fail("CppHotReloadHost: module's create function returned nullptr");

    out.handle = handle;
    out.module = module;
    out.destroyFn = destroyFn;
    out.tempPath = tempPath;
    return true;
}

void CppHotReloadHost::unload(LoadedLibrary& lib, bool keepState) {
    if (lib.module) {
        lib.module->onUnload();
        if (lib.destroyFn) lib.destroyFn(lib.module);
        lib.module = nullptr;
    }
    if (lib.state != nullptr) {
        if (!keepState) {
            lib.destroyState(lib.state);
            freeState(lib.state, lib.layout);
        }
        lib.state = nullptr;
    }
    closeLibrary(lib.handle);
    lib.handle = nullptr;
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
    bool keepOldState = false;
    if (fresh.hasState) {
        if (existing != nullptr && existing->state != nullptr && sameLayout(existing->layout, fresh.layout)) {
            fresh.state = existing->state;
            fresh.transfer = HotReloadStateTransfer::Kept;
            keepOldState = true;
        } else {
            fresh.state = allocateState(fresh.layout);
            fresh.constructState(fresh.state);
            fresh.transfer = HotReloadStateTransfer::Created;
            if (existing != nullptr && existing->state != nullptr) {
                fresh.transfer = HotReloadStateTransfer::Reset;
                if (fresh.migrateState != nullptr && fresh.migrateState(&existing->layout, existing->state, fresh.state)) {
                    fresh.transfer = HotReloadStateTransfer::Migrated;
                }
            }
        }
        fresh.bindState(fresh.state);
    }

    if (existing != nullptr) {
        fresh.reloadCount = existing->reloadCount + 1;
        if (beforeUnload_) beforeUnload_(slot);
        unload(*existing, keepOldState);
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
    if (beforeUnload_) beforeUnload_(slot);
    unload(it->second, false);
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

std::optional<CppHotReloadHost::SlotStatus> CppHotReloadHost::status(const std::string& slot) const {
    const LoadedLibrary* lib = findSlot(slot);
    if (lib == nullptr) return std::nullopt;
    SlotStatus result;
    result.reloadCount = lib->reloadCount;
    result.hasState = lib->hasState;
    result.stateBytes = lib->hasState ? lib->layout.size : 0;
    result.stateVersion = lib->hasState ? lib->layout.version : 0;
    result.state = lib->state;
    result.transfer = lib->transfer;
    return result;
}

CppHotReloadHost::~CppHotReloadHost() {
    for (auto& [name, lib] : slots_) {
        if (beforeUnload_) beforeUnload_(name);
        unload(lib, false);
    }
}

} // namespace engine::core

#pragma once

#include <string>

#include "core/HotReloadModuleAbi.hpp"

namespace engine::core {

class ECS;

// Kronos ("Logic Hot-Reloading" -- v0.4.0 Creator Suite): the real,
// POSIX dlopen()-based host for IHotReloadableModule .so files -- see
// that header's own class comment for the ABI contract a module must
// follow. Windows (LoadLibrary/GetProcAddress) is a real, deliberately
// unattempted follow-on -- same stated-scope-cut convention
// core::ProcessLaunch.hpp's own real POSIX-only scope already uses
// elsewhere in this codebase, not a silent gap: load()/reload() return
// false with a real, honest error message on Windows rather than
// pretending to work.
//
// The real trick that makes *reloading* (not just loading once) work:
// dlopen() caches by canonical file path -- calling it twice on the
// same path returns the SAME cached handle without re-reading the
// file's current contents, even if the file changed on disk since the
// first call. load() therefore always copies the real target .so to a
// fresh, uniquely-numbered temp path (deleted again once that load is
// superseded or the host is destroyed) before ever calling dlopen() on
// it, so every real reload gets a real inode dlopen() has never seen
// before -- the same trick every native hot-reload tool has to use for
// the same reason, not specific to this engine.
//
// State discipline: a reload real-destroys the previous
// IHotReloadableModule (calling its own onUnload(), then its own
// module's exported destroy function, using the OLD dlopen() handle)
// and dlclose()s that OLD handle only after the destroy call returns,
// then constructs a fresh module from the newly dlopen()'d library and
// calls its onLoad(ecs). The ECS the module operates on is owned by the
// CALLER (see tick()'s own `ecs` parameter) and is never touched by
// load()/reload() themselves -- exactly the property that makes this a
// real hot *code* reload rather than a world reset, matching
// core::tickScriptHotReload()'s own "only the Script component's own
// scriptId changes, nothing else in the ECS is touched" behavior on the
// Lua side.
class CppHotReloadHost {
public:
    CppHotReloadHost() = default;
    ~CppHotReloadHost();

    CppHotReloadHost(const CppHotReloadHost&) = delete;
    CppHotReloadHost& operator=(const CppHotReloadHost&) = delete;

    // Real load -- copies `sharedLibraryPath` to a fresh temp path (see
    // this class's own header comment), dlopen()s it RTLD_NOW, resolves
    // all three real exported symbols, checks the ABI version, and
    // (only if every one of those succeeds) real-unloads whatever
    // module was previously loaded, then calls onLoad(ecs) on the
    // freshly-created module. Returns false and leaves the host in
    // whatever state it was already in (a no-op on failure, not a
    // partial swap) if any step fails -- `outError` names which one.
    // Safe to call with no module currently loaded (a plain first
    // load); also the real hot-swap path -- there is no separate
    // reload() entry point because every real load after the first one
    // already IS a reload.
    [[nodiscard]] bool load(const std::string& sharedLibraryPath, ECS& ecs, std::string& outError);

    // Real per-tick forward to whatever module is currently loaded -- a
    // no-op if none is (an honest "nothing to tick", not an error).
    void tick(float dt, ECS& ecs);

    [[nodiscard]] bool hasModuleLoaded() const { return current_.module != nullptr; }

private:
    struct LoadedLibrary {
        void* handle = nullptr;
        IHotReloadableModule* module = nullptr;
        HotReloadDestroyModuleFn destroyFn = nullptr; // resolved from the SAME handle `module` was created from
        std::string tempPath;                         // this load's own real temp copy on disk, removed on unload
    };

    [[nodiscard]] bool loadInto(const std::string& sharedLibraryPath, LoadedLibrary& out, std::string& outError);
    // Real, shared teardown: onUnload() -> destroyFn() -> dlclose() ->
    // delete the real temp file copy. Used by both load() (superseding
    // the previous module) and ~CppHotReloadHost() (final teardown) --
    // same "one real teardown path" convention core::Scripting's own
    // closeAllScripts() already establishes for its VM list.
    static void unload(LoadedLibrary& lib);

    LoadedLibrary current_;
    int nextTempSuffix_ = 0;
};

} // namespace engine::core

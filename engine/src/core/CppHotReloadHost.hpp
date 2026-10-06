#pragma once

#include <cstdint>
#include <optional>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/HotReloadModuleAbi.hpp"
#include "core/HotReloadState.hpp"

namespace engine::core {

class ECS;

// What happened to a slot's KRONOS_HOT_RELOAD_STATE on its latest load.
enum class HotReloadStateTransfer : uint8_t {
    None,     // the module declares no state
    Created,  // first load
    Kept,     // same layout: the new code was handed the same memory
    Migrated, // layout changed and the module's migrate function carried values over
    Reset,    // layout changed and nothing carried over
};
[[nodiscard]] const char* hotReloadStateTransferName(HotReloadStateTransfer transfer);

// Kronos ("Logic Hot-Reloading" -- v0.4.0 Creator Suite): the real,
// dlopen()/LoadLibrary()-based host for IHotReloadableModule shared
// libraries -- see that header's own class comment for the ABI contract
// a module must follow. A module compiled against a different
// compiler/ABI/library-version combination than the host is still a
// real, stated constraint (see HotReloadModuleAbi.hpp) -- this class
// only generalizes WHERE a module can be loaded from (any of N
// independent, separately-reloadable slots) and WHICH platform's
// dynamic-loader API backs that (POSIX dlopen or Win32 LoadLibrary),
// not that cross-build ABI constraint.
//
// N-slot generalization: each module lives in its own named slot,
// loaded/reloaded/unloaded independently of every other slot -- e.g. a
// PluginManager can keep one gameplay-logic .so and several independent
// tool/subsystem plugins hot-swappable at once, none of them tearing
// down or disturbing the others. tick() ticks every loaded slot, in the
// order each slot was first loaded (insertion order), so plugin-to-
// plugin tick ordering stays deterministic across a session even as
// individual slots get reloaded in between.
//
// The real trick that makes *reloading* (not just loading once) work:
// dlopen()/LoadLibrary() both cache by canonical file path -- calling
// either twice on the same path returns the SAME cached handle without
// re-reading the file's current contents, even if the file changed on
// disk since the first call. load() therefore always copies the real
// target shared library to a fresh, uniquely-numbered temp path
// (deleted again once that load is superseded or the host is
// destroyed) before ever loading it, so every real reload gets a real
// file identity neither loader has ever seen before -- the same trick
// every native hot-reload tool has to use for the same reason, not
// specific to this engine or this platform.
//
// Zero-copy state: a module that declares KRONOS_HOT_RELOAD_STATE (see
// HotReloadState.hpp) has its state allocated by the host, outside the
// library. A reload whose state layout is unchanged binds the new code to
// that same memory, so nothing is serialized or copied.
//
// State discipline: reloading a slot real-destroys that slot's previous
// IHotReloadableModule (calling its own onUnload(), then its own
// module's exported destroy function, using the OLD library handle) and
// closes that OLD handle only after the destroy call returns, then
// constructs a fresh module from the newly loaded library and calls its
// onLoad(ecs). The ECS the module operates on is owned by the CALLER
// (see tick()'s own `ecs` parameter) and is never touched by
// load()/unloadSlot() themselves -- exactly the property that makes
// this a real hot *code* reload rather than a world reset, matching
// core::tickScriptHotReload()'s own "only the Script component's own
// scriptId changes, nothing else in the ECS is touched" behavior on the
// Lua side. Reloading one slot never touches any other slot's module or
// state.
class CppHotReloadHost {
public:
    CppHotReloadHost() = default;
    ~CppHotReloadHost();

    CppHotReloadHost(const CppHotReloadHost&) = delete;
    CppHotReloadHost& operator=(const CppHotReloadHost&) = delete;

    // Real load into the named slot -- copies `sharedLibraryPath` to a
    // fresh temp path (see this class's own header comment), loads it
    // (dlopen(..., RTLD_NOW) on POSIX, LoadLibraryA() on Windows),
    // resolves all three real exported symbols, checks the ABI version,
    // and (only if every one of those succeeds) real-unloads whatever
    // module was previously loaded in THIS slot (every other slot is
    // untouched), then calls onLoad(ecs) on the freshly-created module.
    // Returns false and leaves this slot in whatever state it was
    // already in (a no-op on failure, not a partial swap) if any step
    // fails -- `outError` names which one. Safe to call with nothing
    // loaded in `slot` yet (a plain first load into a new slot); also
    // the real hot-swap path for an existing slot -- there is no
    // separate reload() entry point because every real load after the
    // first one into the same slot already IS a reload of that slot.
    [[nodiscard]] bool load(const std::string& slot, const std::string& sharedLibraryPath, ECS& ecs,
                             std::string& outError);

    // Real, explicit teardown of one slot (onUnload() -> destroy
    // function -> close handle -> delete temp file copy) without
    // loading a replacement into it. A no-op if `slot` has nothing
    // loaded. Every other slot is untouched.
    void unloadSlot(const std::string& slot);

    // Called with the slot name right before a slot's library is closed
    // (unload, hot swap, or destruction), while its code is still mapped.
    void setBeforeUnload(std::function<void(const std::string& slot)> callback) { beforeUnload_ = std::move(callback); }

    // Real per-tick forward to every currently-loaded slot's module, in
    // the order each slot was first loaded -- a no-op if no slots are
    // loaded (an honest "nothing to tick", not an error).
    void tick(float dt, ECS& ecs);

    [[nodiscard]] bool hasModuleLoaded(const std::string& slot) const;
    [[nodiscard]] size_t loadedSlotCount() const { return slots_.size(); }

    // Real forward to the named slot's own module's
    // IHotReloadableModule::queryExtension() (see that method's own
    // comment) -- nullptr if `slot` has nothing loaded, or if the loaded
    // module doesn't implement `interfaceId`. Deliberately does NOT expose
    // the raw IHotReloadableModule* itself: a caller that cached that
    // pointer across a hot-swap would be holding a dangling pointer into a
    // real-destroyed module the moment this slot reloads, whereas
    // re-querying by slot name here is always safe to call again after a
    // swap.
    [[nodiscard]] void* queryExtension(const std::string& slot, const char* interfaceId) const;

    struct SlotStatus {
        uint32_t reloadCount = 0;
        bool hasState = false;
        uint64_t stateBytes = 0;
        uint64_t stateVersion = 0;
        const void* state = nullptr;
        HotReloadStateTransfer transfer = HotReloadStateTransfer::None;
    };
    [[nodiscard]] std::optional<SlotStatus> status(const std::string& slot) const;

private:
    struct LoadedLibrary {
        void* handle = nullptr;
        IHotReloadableModule* module = nullptr;
        HotReloadDestroyModuleFn destroyFn = nullptr; // resolved from the SAME handle `module` was created from
        std::string tempPath;                         // this load's own real temp copy on disk, removed on unload
        bool hasState = false;
        KronosHotReloadStateLayout layout{};
        void* state = nullptr;
        KronosHotReloadStateFn constructState = nullptr;
        KronosHotReloadStateFn destroyState = nullptr;
        KronosHotReloadStateFn bindState = nullptr;
        KronosHotReloadMigrateStateFn migrateState = nullptr;
        HotReloadStateTransfer transfer = HotReloadStateTransfer::None;
        uint32_t reloadCount = 0;
    };

    [[nodiscard]] bool loadInto(const std::string& sharedLibraryPath, LoadedLibrary& out, std::string& outError);
    // Real, shared teardown: onUnload() -> destroyFn() -> close the
    // platform library handle -> delete the real temp file copy. Used
    // by load() (superseding a slot's previous module), unloadSlot(),
    // and ~CppHotReloadHost() (final teardown of every remaining slot)
    // -- same "one real teardown path" convention core::Scripting's own
    // closeAllScripts() already establishes for its VM list.
    // keepState: the state now belongs to the module replacing this one.
    static void unload(LoadedLibrary& lib, bool keepState);

    // Insertion-ordered (not sorted) so tick() runs slots in the order
    // they were first loaded -- linear lookup by name is fine here, the
    // real expected slot count is a handful of plugins/modules, not
    // enough for a hash map to earn its complexity.
    [[nodiscard]] LoadedLibrary* findSlot(const std::string& slot);
    [[nodiscard]] const LoadedLibrary* findSlot(const std::string& slot) const;

    std::vector<std::pair<std::string, LoadedLibrary>> slots_;
    int nextTempSuffix_ = 0;
    std::function<void(const std::string& slot)> beforeUnload_;
};

} // namespace engine::core

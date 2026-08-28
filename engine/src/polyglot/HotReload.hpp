#pragma once

// DRAFT SCAFFOLDING -- not wired into the build, not implemented.
// See polyglot/README.md.
//
// Goal: edit a script/system's logic while the engine is running and see
// it live, across whichever language it's written in, without a restart.

#include <functional>
#include <string>
#include <vector>

namespace engine::polyglot {

enum class ReloadableKind {
    LuauScript,      // real, partial precedent: core::Scripting can already
                      // reload a Luau source string into a running VM --
                      // this pillar's real, novel work is generalizing that
                      // one already-real case to every language, not
                      // inventing script reload from scratch.
    NativeSystemDll, // hot-swapping compiled C++ system logic (a shared
                      // library reload) -- real, hard, unsolved: live
                      // in-memory ECS state referencing the old code's
                      // vtables/layout has to survive the swap.
    WasmModule,
};

struct ReloadEvent {
    ReloadableKind kind;
    std::string identifier; // script path, system name, or module name
    bool succeeded;
    std::string errorMessage; // only meaningful when !succeeded
};

// Real, honest scope note: this can only ever be as safe as its
// least-safe supported kind. LuauScript reload is close to real today.
// NativeSystemDll reload is a genuinely hard, separate problem (ABI
// stability across reloads, live ECS state migration) that a real
// implementation would likely stage in far behind the scripted-language
// kinds, not ship simultaneously with them.
class HotReloadController {
public:
    using ReloadCallback = std::function<void(const ReloadEvent&)>;

    // TODO: real filesystem watch per registered reloadable, or an
    // explicit reloadNow() call from a Studio "Apply" button -- which of
    // the two (or both) is a real product decision, not made here.
    void watch(ReloadableKind kind, const std::string& identifier);
    void unwatch(const std::string& identifier);

    void setOnReload(ReloadCallback callback);

private:
    std::vector<std::string> watchedIdentifiers_;
    ReloadCallback onReload_;
};

} // namespace engine::polyglot

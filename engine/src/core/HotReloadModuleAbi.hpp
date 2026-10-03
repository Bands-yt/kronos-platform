#pragma once

namespace engine::core {

class ECS;

// Kronos ("Logic Hot-Reloading" -- v0.4.0 Creator Suite): the real,
// bounded seam this engine did not have before -- studio::IKronosPlugin
// documents itself as "in-process only ... not a dlopen()'d shared-
// library plugin" (see that header's own class comment), so real
// runtime C++ code swap needed a wholly new subsystem rather than an
// extension of that one. A module compiled against this exact header,
// in the exact same real build (same compiler, same C++ ABI, same
// entt/glm versions -- see CppHotReloadHost.hpp's own class comment on
// why that's a real, stated constraint and not a general cross-compiler
// plugin ABI), can be swapped at runtime while the ECS (and whatever
// Jolt PhysicsSystem is stepping it) keeps running underneath it
// untouched -- the same "state lives in the ECS, not in the code that
// reads it" principle core::ScriptHotReload.hpp's Lua-side reload
// already relies on, just applied to a real, natively-compiled
// .so/.dll instead of a re-interpreted Luau chunk.
class IHotReloadableModule {
public:
    virtual ~IHotReloadableModule() = default;

    // Called exactly once right after this module object is constructed
    // -- on the very first load AND on every real hot-swap thereafter.
    // Real one-time setup against the live ECS this module will operate
    // on. Deliberately does NOT hand back a place to stash gameplay
    // state as private member fields meant to survive a reload -- see
    // this class's own header comment: anything that needs to survive a
    // reload belongs in an ECS component, not a module-local field,
    // since the whole IHotReloadableModule object (and everything it
    // privately owns) is real-destroyed by CppHotReloadHost on the next
    // reload.
    virtual void onLoad(ECS& ecs) { (void)ecs; }

    // Called exactly once, right before this module is real-destroyed
    // (and its library dlclose()'d) -- for releasing anything this
    // module itself allocated OUTSIDE the ECS (a real file handle, a
    // background thread it spawned). Never touches ECS component data
    // itself -- that's the host's to keep, per this class's own "state
    // lives in the ECS" principle, which is exactly why it takes no ECS
    // parameter.
    virtual void onUnload() {}

    // Real per-tick gameplay logic -- called once per frame by whichever
    // real game loop owns the CppHotReloadHost this module is loaded
    // into, the same dt/ecs shape every other per-tick system in this
    // engine (Scripting::tick(), core::ParticleSystem::update()) already
    // uses.
    virtual void tick(float dt, ECS& ecs) {
        (void)dt;
        (void)ecs;
    }

    // Optional extension query -- lets a module expose a second interface
    // beyond onLoad/onUnload/tick without engine_core itself knowing what
    // that interface is. `interfaceId` is a string constant the caller and
    // the module both agree on out of band (e.g.
    // studio::IStudioNativePluginExtension::kInterfaceId); a module that
    // doesn't implement that interface returns nullptr, which is the
    // default here so every existing module (which never overrides this)
    // is unaffected. This is deliberately the ONLY hook engine_core adds
    // for Studio-editor extensibility (custom ImGui panels, menu actions)
    // -- see NativePluginManager.hpp's own comment -- because engine_core
    // links into engine_runtime too, which has no ImGui/Studio concept at
    // all; the actual extension interface lives in studio/, not here.
    [[nodiscard]] virtual void* queryExtension(const char* interfaceId) {
        (void)interfaceId;
        return nullptr;
    }
};

// Real ABI version this header defines -- bump it any time
// IHotReloadableModule's own virtual interface changes shape (a new
// pure virtual, a reordered/changed method signature). A module built
// against a different version's vtable layout being loaded into this
// version's host would be real, silent undefined behavior the moment
// either side calls a virtual method on it; CppHotReloadHost::load()
// checks this (via the module's own exported kronosHotReloadAbiVersion()
// symbol, called BEFORE the module is ever touched as a real
// IHotReloadableModule*) and refuses to load on a mismatch -- see that
// class's own header comment for the exact sequencing.
//
// v2: added IHotReloadableModule::queryExtension() -- a real vtable shape
// change, so a v1-built module must not be trusted against a v2 host.
// v3: Renderer gained members; modules built before that crash on load.
inline constexpr int kHotReloadModuleAbiVersion = 3;

// Kronos: every real hot-reloadable module's .cpp defines these three
// functions under `extern "C"` -- unmangled, literal symbol names
// dlsym() can find by string, which is the one real ABI-stability
// property this whole scheme needs from the dlopen() boundary itself
// (the *types* being exchanged, e.g. IHotReloadableModule*, are only
// ever safe to pass across it because both sides are built from this
// exact header in the exact same real build -- a genuine, stated
// constraint, not a general cross-compiler C++ plugin ABI):
//
//   extern "C" int kronosHotReloadAbiVersion() { return engine::core::kHotReloadModuleAbiVersion; }
//   extern "C" engine::core::IHotReloadableModule* kronosCreateHotReloadModule() { return new MyModule(); }
//   extern "C" void kronosDestroyHotReloadModule(engine::core::IHotReloadableModule* m) { delete m; }
//
// See tests/hotreload_fixtures/CounterModuleV1.cpp / CounterModuleV2.cpp
// for two real, complete, minimal examples.
using HotReloadAbiVersionFn = int (*)();
using HotReloadCreateModuleFn = IHotReloadableModule* (*)();
using HotReloadDestroyModuleFn = void (*)(IHotReloadableModule*);

inline constexpr const char* kHotReloadAbiVersionSymbol = "kronosHotReloadAbiVersion";
inline constexpr const char* kHotReloadCreateModuleSymbol = "kronosCreateHotReloadModule";
inline constexpr const char* kHotReloadDestroyModuleSymbol = "kronosDestroyHotReloadModule";
// Optional: modules that reach into engine types (e.g. Renderer) also export
// `uint64_t kronosHotReloadLayoutFingerprint()` returning
// kHotReloadLayoutFingerprint from core/HotReloadLayoutFingerprint.hpp; the
// host refuses the module when it differs from its own.
using HotReloadLayoutFingerprintFn = unsigned long long (*)();
inline constexpr const char* kHotReloadLayoutFingerprintSymbol = "kronosHotReloadLayoutFingerprint";

} // namespace engine::core

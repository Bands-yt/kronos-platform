#pragma once

#include <cstdint>
#include <new>
#include <type_traits>

// Zero-copy state for hot-reloadable native modules.
//
// A module declares one state struct with KRONOS_HOT_RELOAD_STATE. The host
// allocates it, the module constructs it, and on a reload whose state layout
// (size, alignment, type name, version) is unchanged the new code is handed
// the very same memory: nothing is serialized or copied, so containers,
// counters and caches carry over untouched. When the layout changes the new
// module gets a freshly constructed state, and may define
// kronosHotReloadMigrateState to carry values across from the old one.
//
// The state must not hold anything that points into the module's own code
// or static data: no virtual types, function pointers, std::function, or
// string literals kept as const char*. Those addresses die with the old
// library. Heap memory (std::string, std::vector, std::unordered_map) is
// fine because both builds share the same allocator.
//
//   struct SpawnerState { int spawned = 0; std::vector<uint32_t> live; };
//   KRONOS_HOT_RELOAD_STATE(SpawnerState, 1)
//
//   void tick(float dt, ECS& ecs) override { kronosState<SpawnerState>().spawned++; }
//
// The state is bound before onLoad() runs, not during the module's
// constructor.

extern "C" {

struct KronosHotReloadStateLayout {
    uint64_t size;
    uint64_t alignment;
    uint64_t typeHash;
    uint64_t version;
};

using KronosHotReloadStateLayoutFn = void (*)(KronosHotReloadStateLayout* out);
using KronosHotReloadStateFn = void (*)(void* state);
// Returns nonzero when values were carried over into `newState` (already
// constructed); returning 0 keeps the freshly constructed state.
#if defined(_WIN32)
extern void* kronosHotReloadStatePointer;
#else
// Hidden so every module keeps its own pointer instead of sharing one
// process-wide symbol.
extern __attribute__((visibility("hidden"))) void* kronosHotReloadStatePointer;
#endif

using KronosHotReloadMigrateStateFn = int (*)(const KronosHotReloadStateLayout* oldLayout, void* oldState,
                                               void* newState);
}

namespace engine::core {

inline constexpr const char* kHotReloadStateLayoutSymbol = "kronosHotReloadStateLayout";
inline constexpr const char* kHotReloadConstructStateSymbol = "kronosHotReloadConstructState";
inline constexpr const char* kHotReloadDestroyStateSymbol = "kronosHotReloadDestroyState";
inline constexpr const char* kHotReloadBindStateSymbol = "kronosHotReloadBindState";
inline constexpr const char* kHotReloadMigrateStateSymbol = "kronosHotReloadMigrateState";

[[nodiscard]] constexpr uint64_t hotReloadStateTypeHash(const char* name) {
    uint64_t hash = 14695981039346656037ull;
    for (; *name != '\0'; ++name) hash = (hash ^ static_cast<unsigned char>(*name)) * 1099511628211ull;
    return hash;
}

template <typename T>
[[nodiscard]] T& kronosState() {
    return *static_cast<T*>(kronosHotReloadStatePointer);
}

[[nodiscard]] inline bool kronosStateBound() { return kronosHotReloadStatePointer != nullptr; }

} // namespace engine::core

using engine::core::kronosState;

#define KRONOS_HOT_RELOAD_STATE(Type, Version)                                                                       \
    static_assert(!std::is_polymorphic_v<Type>, "hot reload state must not have a vtable");                          \
    extern "C" void kronosHotReloadStateLayout(KronosHotReloadStateLayout* out) {                                    \
        out->size = sizeof(Type);                                                                                    \
        out->alignment = alignof(Type);                                                                              \
        out->typeHash = engine::core::hotReloadStateTypeHash(#Type);                                                 \
        out->version = (Version);                                                                                    \
    }                                                                                                                \
    extern "C" void kronosHotReloadConstructState(void* state) { ::new (state) Type(); }                             \
    extern "C" void kronosHotReloadDestroyState(void* state) { static_cast<Type*>(state)->~Type(); }                 \
    extern "C" void* kronosHotReloadStatePointer = nullptr;                                                          \
    extern "C" void kronosHotReloadBindState(void* state) { kronosHotReloadStatePointer = state; }

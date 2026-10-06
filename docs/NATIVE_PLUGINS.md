# Native plugins and hot reload

Native plugins are shared libraries (`.so` on Linux, `.dll` on Windows) that Studio and the player load from a
`native_plugins` folder next to the executable. They are built with the same compiler and engine headers as the
engine itself.

For plugins that should keep working across engine updates, be written in another language, or come from
someone else (and run sandboxed), use the C plugin API instead: see [PLUGIN_API.md](PLUGIN_API.md).

## A minimal plugin

```cpp
#include <vector>

#include "core/ECS.hpp"
#include "core/HotReloadModuleAbi.hpp"
#include "core/HotReloadState.hpp"

struct SpinnerState {
    int spins = 0;
    std::vector<float> recentSpeeds;
};
KRONOS_HOT_RELOAD_STATE(SpinnerState, 1)

class Spinner final : public engine::core::IHotReloadableModule {
public:
    void tick(float dt, engine::core::ECS& ecs) override {
        auto& state = kronosState<SpinnerState>();
        state.spins++;
    }
};

extern "C" int kronosHotReloadAbiVersion() { return engine::core::kHotReloadModuleAbiVersion; }
extern "C" engine::core::IHotReloadableModule* kronosCreateHotReloadModule() { return new Spinner(); }
extern "C" void kronosDestroyHotReloadModule(engine::core::IHotReloadableModule* m) { delete m; }
```

## What survives a rebuild

- **ECS components** always survive. Anything stored on entities is untouched by a reload.
- **The plugin state** (`KRONOS_HOT_RELOAD_STATE`) is allocated by the engine, not the plugin. If the rebuilt plugin
  declares the same type name, size, alignment and version, its code is handed the same object. Nothing is copied,
  so large containers carry over for free.
- **Everything else** in the plugin (members of the module class, globals, statics) starts fresh on every reload.

The state is bound before `onLoad()` runs. Don't touch it in the module's constructor.

### Changing the state layout

Adding, removing or reordering fields changes the layout. Bump the version when you change meaning without
changing size. On a layout change the new state is default-constructed. To carry values across, export a migrate
function. It receives the old layout and both objects; return nonzero when values were copied:

```cpp
extern "C" int kronosHotReloadMigrateState(const KronosHotReloadStateLayout* old, void* from, void* to) {
    if (old->version != 1) return 0;
    auto& a = *static_cast<SpinnerStateV1*>(from);
    auto& b = *static_cast<SpinnerState*>(to);
    b.spins = a.spins;
    b.recentSpeeds = std::move(a.recentSpeeds);
    return 1;
}
```

### What the state must not contain

The old library is unloaded after a reload, so the state cannot point into it: no virtual functions, function
pointers, `std::function`, lambdas, or `const char*` to string literals. Heap-owning types such as `std::string`,
`std::vector` and `std::unordered_map` are fine.

## Reloading

- **Studio** watches every loaded plugin's library. When the file changes and then stays unchanged for one poll
  (half a second), the new build is swapped in. Toggle this under Plugins → Resources → Native plugins, which also
  shows each plugin's state size, what happened to its state on the last reload, and the reload count.
- **The player** does the same when started with `KRONOS_HOT_RELOAD=1`.
- **A broken build** (missing symbols, wrong ABI version, a file that isn't a library) is refused. The previous
  build keeps running, the error is shown in the panel, and the plugin is not retried until the file changes again.

## API

| Call | Purpose |
|---|---|
| `NativePluginManager::loadPlugin(name, path, ecs, error)` | Load, or reload under the same name |
| `NativePluginManager::reloadPlugin(name, ecs, error)` | Reload from the plugin's current path |
| `NativePluginManager::checkForChanges(ecs)` | Reload every plugin whose file changed, now |
| `NativePluginManager::setAutoReload(enabled, pollSeconds)` / `update(ecs)` | Polling reload once the file settles |
| `NativePluginManager::status(name)` | Reload count, state size and version, state transfer |
| `CppHotReloadHost::status(slot)` | The same, for the lower-level host |

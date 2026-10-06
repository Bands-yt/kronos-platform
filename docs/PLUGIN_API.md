# The Kronos plugin API

Plugins built against `engine/src/plugin/kronos_plugin.h` use a plain C
interface. Only C types cross the boundary, so a plugin:

- can be written in C, C++, Rust, Zig or anything else that can export C functions;
- doesn't need rebuilding when the engine changes, only when the API's major version does;
- can run sandboxed in its own process, so a plugin from someone else can't crash Studio or read your files.

The older C++ interface (`IHotReloadableModule`, [NATIVE_PLUGINS.md](NATIVE_PLUGINS.md)) still works.
It suits your own engine-side code that wants zero-copy hot reload. It's tied to the exact engine
build, though, and can't be sandboxed.

## Where plugins go

| Folder | Runs | Use for |
|---|---|---|
| `native_plugins/` next to Studio | in Studio's process (trusted) | your own plugins |
| `~/.local/share/kronos/plugins/` (Linux), `%LOCALAPPDATA%\Kronos\plugins` (Windows), or `KRONOS_PLUGIN_DIR` | sandboxed | plugins from other people |

Studio loads both at startup. Plugins can sit directly in the folder or in a subfolder each.
**Plugins → Resources → Plugins** lists every plugin with:

- whether it's trusted or sandboxed;
- what it's allowed to do;
- how many blocked calls it made;
- why it stopped, if it did.

It also has buttons to reload, restart or remove each plugin. Trusted plugins reload by themselves
when their library is rebuilt.

The player (`engine_runtime`) loads trusted plugins from its own `native_plugins` folder too.

## A plugin

`native_plugins/SampleCPlugin.c` is a complete example. It adds a Grid Snap panel and converts PGM
heightmaps into OBJ terrain. In short:

```c
#include "plugin/kronos_plugin.h"

static const KronosHostApi* api;

static void draw(void* user, const KronosUi* ui) {
    if (ui->button(ui->context, "Say hi")) api->log(api->host, KRONOS_LOG_INFO, "hi");
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT int32_t kronos_plugin_query(KronosPluginInfo* info) {
    info->api_major = KRONOS_PLUGIN_API_MAJOR;
    info->api_minor = KRONOS_PLUGIN_API_MINOR;
    info->id = "com.example.hello";
    info->name = "Hello";
    info->version = "1.0.0";
    info->capabilities = KRONOS_CAP_EDITOR_PANELS;
    return KRONOS_OK;
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT int32_t kronos_plugin_load(const KronosHostApi* host, void** instance) {
    KronosPanel panel = {sizeof(KronosPanel), "com.example.hello.panel", "Hello", 1, NULL, draw};
    api = host;
    *instance = NULL;
    return api->register_panel(api->host, &panel);
}

KRONOS_PLUGIN_EXTERN KRONOS_PLUGIN_EXPORT void kronos_plugin_unload(void* instance) {}
```

Build it as a shared library with `engine/src` on the include path:

```
cc -shared -fPIC -fvisibility=hidden -I engine/src hello.c -o native_plugins/hello.so
```

`kronos_plugin_tick(void* instance, float dt)` is optional and runs once per frame.

## What a plugin can do

A plugin lists the capabilities it needs, and the Resources panel shows them. Studio grants what
was asked for; engine code can grant less with `LoadOptions::allowedCapabilities`. Any call outside
what was granted returns `KRONOS_DENIED`, and the first one is logged.

| Capability | Calls |
|---|---|
| `KRONOS_CAP_SCENE_READ` | `entity_count`, `entity_at`, `find_entity`, `entity_name`, `get_position` |
| `KRONOS_CAP_SCENE_WRITE` | `set_position` |
| `KRONOS_CAP_ASSET_IMPORTERS` | `register_asset_importer` |
| `KRONOS_CAP_EDITOR_PANELS` | `register_panel` |
| `KRONOS_CAP_CHANNELS` | `open_channel` |

`log` is always allowed. Call the API only from inside the functions the engine calls
(load, tick, panel draw, import), on the thread that called them.

### Asset importers

An importer converts a file the engine can't read into one it can, for example `.pgm` into `.obj`.
Dropping a matching file on Studio converts it, then imports the result as usual.

Importers are named by `type`. When two plugins register the same type, the higher `version` wins.
If the newer plugin is removed, the older importer takes over again. Registering the same type at
the same version twice returns `KRONOS_CONFLICT`. Panels work the same way, keyed by `id`.

### Editor panels

A panel's `draw` gets a `KronosUi` with these widgets:

- `text`
- `button`
- `checkbox`
- `slider_float`
- `input_text`
- `separator`
- `same_line`

Widgets return nonzero when the user changed them. Labels identify widgets, so keep them unique
within a panel. In a sandboxed plugin a click reaches the plugin one frame later.

### Channels

`open_channel(name, version, size)` returns a block of zeroed memory shared by name between:

- plugins;
- the engine (`PluginHost::channel`/`openChannel`);
- for sandboxed plugins, across processes too.

Opening an existing channel with a different version or size returns `KRONOS_CONFLICT`. Channels
outlive the plugin that opened them, so they also keep a plugin's state across reloads and crashes.

## Versioning rules

- `KRONOS_PLUGIN_API_MAJOR` changes only for breaking changes. The editor refuses a plugin built for
  another major version and says which version it needs.
- Minor versions only add fields to the end of structs. Every struct starts with `struct_size`,
  so old plugins keep working on new engines and new plugins can detect old engines:
  `KRONOS_HAS_FIELD(api, KronosHostApi, some_new_call)`.
- Registration copies every string. Function pointers must stay valid until `kronos_plugin_unload`.
  Everything a plugin registered is removed before its library is unloaded.

## The sandbox

Sandboxed plugins run in `kronos_plugin_sandbox`, one process per plugin, which ships next to
Studio. Before the plugin's library is loaded, the process:

- gives up the ability to read any files except the plugin's own folder and system libraries,
  and to write any files at all (Landlock);
- gives up network access, starting programs, `fork`, `ptrace`, signalling other processes,
  `io_uring`, kernel keyrings and similar (seccomp);
- gets no environment variables, no core dumps, and at most 4 GB of memory and 64 open files.

The plugin reaches the editor only through a socket, and every call is checked against its
capabilities on the editor's side. A crash, or a call that takes longer than 2 seconds (30 for an
import), stops just that plugin. Its panels and importers go away and the Resources panel shows why.
**Restart** brings it back. Channels keep their contents through this.

Limits:

- The sandbox needs Linux 5.13 or newer with Landlock enabled. Without it, third-party plugins
  aren't loaded at all; the editor won't fall back to running them unsandboxed.
- Windows and macOS don't have the sandbox yet, so third-party plugins can't be loaded there.
  Trusted plugins work everywhere.
- Trusted plugins get the same capability checks on API calls, but they're ordinary native code
  in the editor's process and can do anything the editor can. Only put plugins you trust in `native_plugins`.

## For engine code

`plugin::PluginHost` (`engine/src/plugin/PluginHost.hpp`) is the whole host side:

- `load`, `reload`, `unload`, `tick` and `update` (polling reload);
- `importers`, `importerFor`, `importAsset` and `importFile`;
- `panels` and `drawPanel`;
- `openChannel` and `channel`;
- `log`.

The engine tests (`testPluginApiInProcess`, `testPluginApiSandbox`, `testSamplePluginApiPlugin`)
load C plugins from `engine/tests/plugin_fixtures/` both ways. They check that inside the sandbox
a plugin really can't open sockets, fork, write files, read outside its folder or signal the editor.

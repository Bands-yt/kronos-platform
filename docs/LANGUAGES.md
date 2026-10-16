# Languages in Kronos

Kronos has three kinds of code. They run in different places and have different safety rules.

| | Luau | Python (PocketPy) | Native plugins (C, C++, Rust) |
|---|---|---|---|
| Used for | game scripts, Roblox imports | game scripts (preview) | Studio tools |
| Runs in | Player, servers, Studio Play | Player, servers, Studio Play | Studio only |
| What it is | Luau VM (the Roblox language) | PocketPy 2.2.0, a small Python 3, not CPython | compiled library over a C ABI (`kronos_plugin.h`) |
| Sandbox | memory, time and stack limits; no file or OS access | memory, time and stack limits; no file, OS or import access | separate process on Linux (permissions asked in the manifest); a plugin in the trusted folder runs in-process; Windows sandbox not built yet |
| Limits | 64 MB per script, 256 MB per game, 8 ms per tick | 64 MB, 8 ms per tick, 15 scripts | whatever its capability bits allow |
| Roblox API | yes, see ROBLOX_BRIDGE.md | no, only `world` and `events` | no |
| Docs | LUA_API.md | PYTHON_SCRIPTS.md | PLUGIN_API.md |

Notes:
- Rust is only a way to build native plugins (the `kronos` crate over the C ABI). There is no Rust scripting.
- Python is not full Python. There are no third-party packages, no `import` from disk, and `input`, `exit` and `time.sleep` raise errors.
- A script is Python when its first line is a `#` comment; otherwise Luau.
- Luau and Python scripts can't call each other yet. They share the world through `world` and the Roblox tree.

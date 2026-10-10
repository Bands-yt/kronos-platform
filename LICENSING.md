# Licensing

Kronos uses two licences, so it's clear what you can build on.

## The plugin kit: open source (Apache 2.0)

These files are open source under the
[Apache License 2.0](LICENSES/Apache-2.0.txt). You may use, copy, change
and share them, including in paid or closed-source projects, as long as
you keep the licence and copyright notices. They're the parts you need to
build plugins, tools and scripts for Kronos.

| File or folder | What it is |
|---|---|
| `engine/src/plugin/kronos_plugin.h` | The C plugin API (native plugins, any language that can make a C library) |
| `native_plugins/SampleCPlugin.c` | Example native plugin in C |
| `native_plugins/SampleStudioToolPlugin.cpp` | Example native Studio plugin in C++ |
| `templates/plugin/` | The Luau Studio plugin template |
| `engine/assets/luau/kronos_globals.d.lua` | Luau type definitions for the script API (autocomplete, type checking) |
| `docs/PLUGIN_API.md` | Plugin API documentation |
| `docs/LUA_API.md` | Script API documentation |
| `docs/THIRD_PARTY_TOOLS.md` | Rules and launch options for third-party tools |

Source files in the kit start with `SPDX-License-Identifier: Apache-2.0`.

## Everything else: all rights reserved

The engine, the Player, Kronos Studio, the backend, the games, the art and
the build scripts are covered by [LICENSE](LICENSE): you can read the code
on GitHub, but you need written permission to copy, change or reuse it.

The name **Kronos** and the Kronos logo aren't covered by either licence.
See [docs/THIRD_PARTY_TOOLS.md](docs/THIRD_PARTY_TOOLS.md) for how to
name a tool that works with Kronos.

## Third-party libraries

Kronos is built with libraries that keep their own licences, including
Jolt Physics, Luau, EnTT, Dear ImGui, ENet, glm and nlohmann/json (MIT)
and SDL2 (zlib). Test files in `engine/tests/fixtures/rbx-test-files/`
come from rbx-dom (MIT; see the licence file in that folder).

## Contributing

Pull requests to the plugin kit are licensed under Apache 2.0, like the
kit itself. For anything else, open an issue first, so we can agree on it
before you write code.

# Samples

What is in the repository to learn from, and under what licence (775). The licence of everything not named here is
"All Rights Reserved"; see [../LICENSING.md](../LICENSING.md).

## Plugin samples (721)

| Sample | Where | Shows | Licence |
|---|---|---|---|
| C plugin | `native_plugins/SampleCPlugin.c` | panel, importer, channels | Apache 2.0 |
| Studio tool (C++) | `native_plugins/SampleStudioToolPlugin.cpp` | editor panel | Apache 2.0 |
| Plugin template | `templates/plugin/` | the smallest working plugin | Apache 2.0 |
| Rust plugin template | `templates/rust-plugin/` | the `kronos` crate | Apache 2.0 |
| Rust SDK | `sdk/rust/kronos/` | safe wrapper over the C ABI | Apache 2.0 |

## Game and script samples

| Sample | Where | Shows |
|---|---|---|
| Lua examples | `examples/lua/` | moving platform, interactive light, networked ping |
| Ported Roblox script | `examples/migration/` | `PortedScript.luau`, `SampleGame.rbxlx` |
| Default world, Sky Garden | `games/DefaultWorld`, `games/SkyGarden` | minimal projects |
| Remote Check | `engine/tests/fixtures/games/remote_check/` | remotes, DataStore, Python in one game (used by the smoke test) |
| Compatibility places | `engine/tests/compat_corpus/` | 10 Roblox places with behaviour checks |

## Samples we do not have yet

An audio-driven rhythm game (759), a cinematic storyboard (760), a terrain sculpting walkthrough (761), a Python game (762),
a Rust Studio plugin beyond the template (763) and a Luau plus Python game (765). Each should be a small project folder with a
README and a test that loads it.

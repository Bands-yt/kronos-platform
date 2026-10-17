# Learning path for programmers

An order to learn Kronos if you already write code. Each step has something to try and a doc to read.

1. **Run it.** Build ([QUICKSTART.md](QUICKSTART.md)), open Studio, press Play. Check `engine_runtime --gpu-info` works.
2. **Make a part move.** Insert a part, add a Luau script from the template cards, press Play ([LUA_API.md](LUA_API.md)).
3. **Use the Roblox-style API.** `workspace`, `Instance.new`, events, `Touched` ([ROBLOX_BRIDGE.md](ROBLOX_BRIDGE.md)).
4. **Try Python.** Start a script with `# python` and use `world` and `events` ([PYTHON_SCRIPTS.md](PYTHON_SCRIPTS.md)).
5. **Test with other players.** Test > Server with 2 players ([MULTIPLAYER_TEST.md](MULTIPLAYER_TEST.md)); then read how remotes work and
   why the server decides ([SECURITY_PROCESS.md](SECURITY_PROCESS.md)).
6. **Import a Roblox place.** File > Import Roblox file, then run the Bridge Doctor ([BRIDGE_DOCTOR.md](BRIDGE_DOCTOR.md)).
7. **Write a Studio plugin.** Start from `templates/plugin` (C) or File > Rust Plugins ([PLUGIN_API.md](PLUGIN_API.md)).
8. **Read the engine.** [ARCHITECTURE_OVERVIEW.md](ARCHITECTURE_OVERVIEW.md), then [ARCHITECTURE.md](ARCHITECTURE.md); run the tests and add one
   (`engine/tests/test_main.cpp`, see [CONTRIBUTING.md](../CONTRIBUTING.md)).
9. **Know the limits.** [LIMITATIONS.md](LIMITATIONS.md) and [MATURITY.md](MATURITY.md).

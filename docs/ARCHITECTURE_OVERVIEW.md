# Architecture overview

A one-page map for someone new. Details are in [ARCHITECTURE.md](ARCHITECTURE.md). Shipped versus prototype status is
in [MATURITY.md](MATURITY.md).

```
Player (engine_runtime)      Studio (kronos_studio)      Server (kronos_server, Linux)
        \                          |                          /
         +-------------- engine_core (C++20) ----------------+
           ECS (EnTT) | Physics (Jolt) | Scripts (Luau, Python) | Net (ENet)
           Renderer (Vulkan) | Audio (miniaudio) | Roblox bridge | Plugins
                        |
          games (kronos_games, kronos_despair, kronos_brokenbones) -- game code, outside the core
                        |
        Backend (Node, accounts / publishing / catalogue)  -- talks to the apps over HTTPS
```

- **Core rule:** `engine_core` has no game headers. Games plug in through `GameMode` and `GameRules`.
- **Roblox bridge:** `migration/` reads `.rbxl/.rbxlx`; `core/InstanceTree` and `ScriptInstanceApi` give scripts the
  Roblox `Instance` API on top of the ECS.
- **Plugins:** C ABI in `plugin/kronos_plugin.h`. Third-party plugins run in `kronos_plugin_sandbox`.
- **Server:** the same simulation without a window (the graphics code is compiled in but never started).
- **Tests:** one big `engine/tests/test_main.cpp`, plus the network smoke test and the compatibility score.

How to measure progress: [ROADMAP.md](ROADMAP.md) (plan), [V5_PLAN.md](V5_PLAN.md) (the 1,000 items),
[BACKLOG_AUDIT.md](BACKLOG_AUDIT.md) (what is done), [BENCHMARKS.md](BENCHMARKS.md) (numbers and how to get them).

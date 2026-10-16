# What is stable, preview or experimental (16 Oct 2026)

| Label | Meaning |
|---|---|
| stable | used in a release, covered by tests, safe to rely on |
| preview | works and is tested, but the API or behaviour may change |
| experimental | exists, little tested or only partly built |

| Area | Label | Notes |
|---|---|---|
| Studio: scene editing, undo, Explorer, Inspector | preview | gizmo drags and large scenes not tested |
| Studio: Luau scripting and editor | preview | no autocomplete from the Roblox class table, no debugger |
| Studio: Python scripts | preview | no autocomplete or error markers |
| Studio: test with a server and 1 to 4 players | preview | |
| Player: home, avatar, catalogue, My Games | preview | |
| Roblox place import (`.rbxl`, `.rbxlx`) | preview | 100% on the 10 test places and the owner's obby; not on every Roblox game |
| Roblox Luau API | preview | what works and what doesn't is listed in ROBLOX_BRIDGE.md |
| Roblox services that are stubs | experimental | Marketplace, Badge, Teleport |
| Physics (Jolt), collision groups | preview | characters don't use groups |
| Multiplayer (ENet), remotes, replication | preview | full diff each tick, no interest management |
| Rollback netcode and Match scripts | experimental | not tried in a real two-window match |
| `kronos_server` and its container image | experimental | Linux only |
| Native plugins (C, C++, Rust) | preview | Windows sandbox not built |
| Movie Mode, Audio Maker, 3D Maker | experimental | |
| Windows installer and downloads | preview | fresh-PC test still to do |
| Backend: accounts, publishing, catalogue | preview | DataStore backend and moderation data not deployed |
| Cloud hosting, matchmaking, Team Create, web player, mobile | not built | see ROADMAP.md |

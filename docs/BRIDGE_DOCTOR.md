# Bridge Doctor

Bridge Doctor checks a Roblox place before you bring it into Kronos and tells you what will work, what may break, and where to look. It runs offline, never downloads Roblox assets, and never changes your scripts.

## Run it

```
kronos_compat my_game.rbxlx --report-md report.md --report-json report.json
```

- Accepts `.rbxlx`, `.rbxl`, `.rbxm` and `.rbxmx` files, or a folder of them.
- Without `--report-*`, `kronos_compat` prints the same one-line scores as before.
- Exit codes: `0` ok, `1` below `--min`, `2` nothing could be scored, `3` a report file couldn't be written.

## What it measures

Four results, always kept separate. None of them is folded into another.

| Measure | Meaning |
|---|---|
| Classes | Instances whose class Kronos imports |
| API uses | Roblox API uses in script source that Kronos has (a text scan) |
| Scripts | Scripts that compiled and started without errors in a 3-second headless run with one player |
| Behaviour | Results of the place's behaviour checks, or "not run" when it has none |

A place that imports and runs without errors is **not** proof that it plays correctly. Only behaviour checks test gameplay.

## Findings

Each finding has a category, a stable `code`, what it affects, a script path and line (only when Kronos reported one), why it matters, and where to get help.

| Category | Codes |
|---|---|
| Blocker | `import-failed`, `import-blocked`, `compile-error`, `runtime-error` |
| Gameplay risk | `missing-api`, `unsupported-class` (non-visual), `attributes-not-imported`, `behaviour-failed` |
| Visual and audio | `asset-unavailable` (Roblox-hosted textures, meshes, sounds), `unsupported-class` (effects, meshes, sky, images) |
| Information | `unread-properties` (grouped per class), `unused-roblox-services`, `shimmed-api`, `not-started`, `empty-script`, `safety-flag` |

Roblox Studio saves dozens of built-in services and bookkeeping properties (`HistoryId`, `UniqueId`, ...) in every place. They are grouped into one information line each so they don't hide real problems.

## Behaviour checks

Put a `<place>.checks.luau` file next to the place. It runs after the place's scripts have started, with one player in the game:

```lua
-- my_obby.checks.luau
local pad = workspace:FindFirstChild("finish line")
touch(pad, hit)                 -- fires Touched as if the player's character hit the pad
task.wait(0.2)
check("touching the finish pad turns it green", pad.BrickColor == BrickColor.new("Lime green"))
```

- `check(name, ok, detail)` records one result.
- `touch(part, otherPart)` fires `Touched` (scripts run without physics, so nothing touches by itself).
- `player`, `character` and `hit` (the character's root part) are ready to use.
- An error in the checks file is reported as a failed check.

The five test places in `engine/tests/compat_corpus/` each have one (16 checks), and the tests run them on every build.

## Known limits

- No physics, rendering or input during the run; touches only happen through `touch()`.
- Server and client scripts share one session with one player; multiplayer isn't tested.
- The API scan reads source text, so calls made indirectly (`obj[name]()`) are missed.
- Code that only runs later (after 3 seconds, or on events nothing fires) isn't executed, so its errors aren't seen.
- Unread properties are listed by name; whether a game depends on them isn't known.

The reports list these limits too, so they travel with the results.

# Validate Project

**File > Validate Project...** in Kronos Studio checks the open scene and lists what to fix. It reads the scene only. It never changes it, and it doesn't play the game.

## Checks

Each check reports one of: `PASS`, `FAIL`, `WARNING`, `NOT RUN`, `UNAVAILABLE`, `INCONCLUSIVE`. A check that couldn't run is never shown as `PASS`.

| Check | Fails or warns when |
|---|---|
| Scripts compile | A script has a syntax error (with its line) |
| Roblox APIs are available | A script uses a Roblox API Kronos doesn't have yet (text scan, with line) |
| Object references point to real objects | An Instance property points to a deleted object, or a WeldConstraint has no Part0/Part1 |
| Asset files exist | A sound, mesh or mesh-collider file is missing, or a Roblox-hosted asset (`rbxassetid://...`) is used |
| No script errors in the last Play | Script errors were logged during the last Play. `NOT RUN` until you press Play once |
| Everything in the scene can be saved | The scene has WeldConstraints (not saved in scenes yet) |
| Works with more than one player | Always `UNAVAILABLE` for now |
| Game packages and starts in the Player | Always `UNAVAILABLE` for now |

Relative asset paths are looked up in Studio's folder and the game's folder.

## Using the window

- **Run again** re-checks and compares with the previous run: new findings are tagged `[new]`, and the header counts what was fixed. Findings are matched by code, script and object, so a finding keeps its identity when lines move.
- Filter by category or text. Hover a finding for why it matters and how to fix it.
- Click a finding to select its object; for scripts, the Script Editor opens at the line.
- Type a file path and press **Export Markdown** or **Export JSON**. Reports contain no script source and are the same byte for byte for the same scene.

## Known limits

- Runtime errors only cover code that ran during the last Play. The log keeps the last 2000 lines, so a very noisy Play can lose older errors.
- The API scan reads source text; indirect calls (`obj[name]()`) are missed.
- Gameplay isn't tested. For imported Roblox places, use [Bridge Doctor](BRIDGE_DOCTOR.md) behaviour checks.

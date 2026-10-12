# Compatibility Lab

Bridge Doctor ([BRIDGE_DOCTOR.md](BRIDGE_DOCTOR.md)) tells you what Kronos understands in a Roblox place. The Compatibility Lab goes further: it **plays** each place headlessly, with physics and several players, and records what goes wrong while it runs. Run it before and after a change to see exactly what got better or worse.

## Run it

```
kronos_compat --lab tests/compat_corpus --report-md lab.md --report-json lab.json
kronos_compat --lab my_game.rbxlx --seconds 60 --players 4
kronos_compat --lab tests/compat_corpus --baseline last_release.json
```

| Option | Meaning |
|---|---|
| `--seconds N` | Game time to play each place (default 30). Runs faster than real time |
| `--players N` | Test players that join (default 2, at most 8). The first one is the local player |
| `--report-json FILE` / `--report-md FILE` | Write the results |
| `--baseline OLD.json` | Compare with an earlier `--report-json` and list what is better and worse |
| `--no-isolate` | Run every place in this process (for debugging) |

Exit codes: `0` ok, `2` nothing to run or the baseline can't be read, `3` a report couldn't be written, `4` something got worse than the baseline, `5` a place crashed.

## What happens in a run

1. The place is imported, scored like Bridge Doctor does, and loaded into an empty world.
2. Server scripts start, then the test players join with real physics characters at a `SpawnLocation`.
3. The game runs in fixed steps: scripts, Roblox events (`Heartbeat`, `Stepped`, `Touched` from real contacts), physics and respawns.
4. After 2 game seconds the place's checks file (if any) starts and keeps running alongside the game.
5. At the end the players leave and the results are collected.

Each place runs in its own child process, so a crash or a hang (2 minutes plus 4 times the game time) only loses that place and is reported as `CRASHED (signal 11)`, `timed out` and so on.

## What it reports

| Result | Meaning |
|---|---|
| Score | Bridge Doctor's overall percent |
| Errors | Each different script error once, with the game time it first happened, the script, the line (when Luau gave one) and how often it repeated |
| Checks | The checks file's results |
| Deaths / fell out | How often the test players died, and how often they dropped below `workspace.FallenPartsDestroyHeight` |
| Parts fell | Unanchored parts below `FallenPartsDestroyHeight` at the end, usually a missing `Anchored` or weld |
| Touches | `Touched` events from physics contacts |
| Missing images | GUI `Image` values with no file in the place's folder (see ROBLOX_BRIDGE "Images") |
| Instances | Objects at the start and end (a big jump points at something spawning without limit) |
| Slowest script frame | The longest time scripts took in one frame (machine-dependent, not compared) |

With `--baseline`, a place counts as **worse** when it crashes now, its score fell, a check that passed now fails, a new error appears, or more parts fall out of the world. Fixed errors, passing checks and higher scores are listed as **better**.

## Checks files

The same `<place>.checks.luau` files as Bridge Doctor, with a few extras:

```lua
-- my_obby.checks.luau
local humanoid = character:FindFirstChildOfClass("Humanoid")
humanoid:MoveTo(workspace.Finish.Position)   -- the character really walks there
humanoid.MoveToFinished:Wait()
check("the player can walk to the finish", (hit.Position - workspace.Finish.Position).Magnitude < 6)
check("both players joined", #players == 2)
```

- `check(name, ok, detail)`, `touch(part, otherPart)`, `player`, `character` and `hit` work as in Bridge Doctor.
- `players` lists every test player; `seconds` is the run length.
- Physics is on, so `MoveTo`, falling and real touches happen. `touch()` still fires `Touched` directly.

## Known limits

- Players stand still unless a checks file moves them.
- Nothing is drawn, so visual differences aren't measured. The GUI is laid
  out (1280×720), so `AbsolutePosition`, `AbsoluteSize` and scrolling work in
  checks, and `click(button)` clicks GUI buttons.
- Server and client scripts share one session; there is no network in between.
- Roblox-hosted assets (meshes, images, sounds) are never downloaded.
- On Windows a hang is not timed out yet.

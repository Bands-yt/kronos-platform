# Kronos Alpha — Tester Guide

For people trying the **packaged Alpha release** (the zip from GitHub
Releases) — not building from source. If you're building from source
instead, see [QUICKSTART.md](QUICKSTART.md).

## What's in the zip

```
kronos-alpha/
  engine_runtime      # the player client
  studio               # the creator tool
  shaders/              # compiled, required by both binaries
  assets/               # required by both binaries
  plugins/              # 4 real starter Luau plugins, pre-loaded and ready to Scan
  templates/            # a starter project + a starter plugin to copy from
  examples/              # example gameplay Luau scripts
```

Both binaries look for `shaders/`/`assets/` next to themselves, so keep
the folder intact — don't move just the binary out on its own.

## Running it

```sh
cd kronos-alpha
./engine_runtime
```

You'll land on a real Home Screen — "Play" drops you into an offline
scene, "Sessions" opens a real LAN session browser (host from a second
machine/instance on the same network to test multiplayer). **Press Esc**
at any time while playing to get back to the Home Screen.

```sh
./studio
```

Opens the creator tool: a docked Explorer/Inspector/Viewport, a
first-launch welcome panel with an "Open Default Project" button, and
every built-in tool reachable from the **Plugins** menu (grouped by
category — nothing cascades open by default). Worth trying:

- **Block Builder** (Plugins → World) — place cube/sphere/cylinder/wedge
  primitives, snap-to-grid, then use the Viewport's own gizmo to
  move/rotate/scale what you placed.
- **Plugin Browser** (Plugins → Plugins) — click **Scan** to see the 4
  starter plugins already sitting in `plugins/`; **Load** any of them to
  see its window appear (they log to the Debug Console).
- **Creator Tools** (Plugins → World) — place props, apply a terrain
  preset (try Rolling Hills), adjust biome lighting.

```sh
./engine_runtime --house-demo
```

A real, complete built house — front door (walk up and press **E** to
open/close it), 2 windows, a kitchen, a fireplace with real light and
rising embers — sitting on rolling-hill terrain. This one scene
exercises most of what's listed above at once, so it's a good first
thing to look at.

## What to report

If something crashes, hangs, or does nothing when you expect a
response, that's useful signal — please include:
- Which binary and launch flag (if any)
- What you clicked/pressed right before it happened
- Anything printed to the terminal (stdout/stderr) at the time

## Known limitations (not bugs)

- Cylinder/Wedge blocks placed via Block Builder render and move fine
  live, but don't yet survive a Save Scene → reload with their exact
  shape (Cube/Sphere do). Flagged directly in Block Builder's own panel.
- Session Browser only discovers hosts on the same local network — there's
  no internet-wide matchmaking yet.
- `--server`/`--client`/`--stress`/`--tntwars`/`--miningsim`/
  `--render-showcase` are separate, non-interactive-menu launch modes —
  see [QUICKSTART.md](QUICKSTART.md)'s own table for the full list.

# Python game scripts (preview)

Games can have Python scripts next to their Luau ones. Kronos runs them on
[PocketPy](https://github.com/pocketpy/pocketpy) 2.2.0 (MIT), a small Python 3
interpreter built into the engine. No system Python is needed.

Status: **preview** (4.5 step 3). It runs in the Player, on servers and in Studio
Play.

## Where scripts go

A game's `Scripts/Main.py` runs when the game loads, after `Scripts/Main.lua`.

```python
crate = world.find("Crate")

@events.on_update
def spin(dt):
    x, y, z = world.get_position(crate)
    world.set_position(crate, x, y + dt, z)

@events.on_collision
def bump(a, b):
    print("bump", world.name(a), world.name(b))
```

`world` and `events` are always there; no import needed.

## Python scripts in Studio

A script on an entity is Python when its **first line is a `#` comment** (Luau
comments start with `--`, so a Luau script can never begin that way). In the
Script Editor, pick the **Python script** or **Python spinner** card on the
empty screen, or import a `.py` file (a `# python` line is added if it has no
leading comment). The editor then uses Python colours and turns the Luau
checker and autocomplete off for that script.

Press Play and the script runs with the same `world` and `events` as above;
errors and `print` go to the Engine Log. Edit the script during Play and it
reloads, like Luau. Python scripts count towards the 15-script limit.

## API

| Call | What it does |
| --- | --- |
| `world.find(name)` | Entity id (int) with that name, or `None` |
| `world.name(e)` | Name, or `None` |
| `world.exists(e)` | `True` if the entity is still there |
| `world.get_position(e)` | `(x, y, z)`, or `None` |
| `world.set_position(e, x, y, z)` | Moves the entity |
| `world.apply_impulse(e, x, y, z)` | Pushes a physics body |
| `world.set_velocity(e, x, y, z)` | Sets a physics body's velocity |
| `world.time()` | Game time in seconds |
| `events.on_update(fn)` | `fn(dt)` every tick |
| `events.on_collision(fn)` | `fn(a, b)` when two bodies start touching |
| `events.on_unload(fn)` | `fn()` when the script is unloaded |

The `events` functions return `fn`, so they work as decorators. Entity ids are
the same numbers Luau's `world` API uses. `print` goes to the game log.

The standard modules that come with PocketPy work: `math`, `random`, `json`,
`time` (except `sleep`), `easing`, `enum`, `base64`, `vmath`, `array2d` and
others.

## Limits and safety

Python scripts get the same kind of limits as Luau game scripts:

- **Memory:** 64 MB per script. Going over stops the script. One huge
  request (like `[0] * 10**10`) is refused straight away.
- **Time:** 8 ms for each handler call, 250 ms for the top
  level. A script that runs too long is stopped, even if it catches the error.
- **Depth:** deep nesting (`repr` of a list nested 50,000 times, very deep
  expressions in `eval`) raises `RecursionError` or `SyntaxError` instead of
  crashing the game.
- **No outside access:** no files, no `os`, no sockets, no loading modules from
  disk, no raw memory (`stdc`), no watchdog control (`pkpy`), no `pickle`.
  `exit()`, `input()` and `time.sleep()` raise an error.
- **At most 15 Python scripts** at once (PocketPy has 16 VMs; Kronos leaves
  one unused).

A script that is stopped prints why in the log, for example
`[python] MyGame/Main.py stopped: ran longer than its 8 ms limit`. Other scripts
keep running. A normal error in a handler is reported once and the script
keeps going.

### How it's enforced

- `engine/external/pocketpy_kronos/` is the glue. Its header is force-included
  into every PocketPy file. It counts every allocation per VM and frees a whole
  VM in one go, uses wall-clock time for the watchdog, and checks how much of
  the C stack is used.
- `engine/cmake/Dependencies.cmake` applies a few small, checked patches to
  PocketPy's sources in the build folder. If a PocketPy update moves the code,
  the build stops instead of silently skipping a patch. The patches:
  - the watchdog keeps firing once tripped;
  - stack checks in `py_call`, `py_next`, `json` and the compiler;
  - an overflow check in `str * n`;
  - a fix for an upstream bug where traceback entries left values uninitialised
    for the garbage collector.
- `core/PythonScripting.*` runs the scripts and holds the `world`/`events` API.

PocketPy is younger and less battle-tested than Luau. The cases above are
covered by `testPythonScripts` and were checked with AddressSanitizer, but
fuzzing it is still to do.

## Not done yet

- The script editor has no Python autocomplete or error markers yet (Python errors show in the log when the script loads).
- No Roblox-style Instance API, `task` library, networking, UI or audio from
  Python.
- No `import` of a game's other `.py` files.

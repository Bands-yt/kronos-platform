# Design notes: scripting languages, plugins, testing and operations

Decisions and plans in one place. Nothing here claims a feature exists unless it says "today". Updated 17 Oct 2026.

## Python and Luau side by side (430, 431, 444)

| | Luau | Python |
|---|---|---|
| Find an object | `workspace:FindFirstChild("Crate")` returns an Instance | `world.find("Crate")` returns an entity id (an integer) |
| Position | `part.Position` (Vector3) | `world.get_position(e)` returns `(x, y, z)`; `world.set_position(e, x, y, z)` |
| Forces | velocity and impulse properties and methods (see LUA_API.md) | `world.apply_impulse`, `world.set_velocity` |
| Events | `RunService.Heartbeat`, `Touched`, ... | `@events.on_update`, `@events.on_collision`, `@events.on_unload` |
| Time | `os.clock()` | `world.time()` |
| Roblox services, GUI, remotes, DataStore | yes | no |
| API version | n/a | `world.API_VERSION` |

Today the two languages share state only through the world (positions and physics) and the Roblox tree.
Data rules for any future bridge: numbers are 64-bit floats, `nil` and `None` are null, tables with consecutive keys
from 1 are lists and others are objects, vectors are `[x, y, z]`, entities are numeric ids. Anything else is refused
with an error that names the value. See [schemas/message.schema.json](schemas/message.schema.json).

## Python limits and policy (433, 434, 438, 439, 440)

- **No coroutines or async:** a script that needs to wait keeps a counter in `events.on_update`.
- **Determinism:** Python is not checked for determinism. Rollback matches use Luau "Match scripts", which are
  (see [ROLLBACK.md](ROLLBACK.md)).
- **Packages:** none are allowed. No `import` from disk, no pip.
- **Project-local modules (planned):** a later version may allow `import` from the game's own `Scripts/` folder only,
  with the same time and memory limits per script and no packages; then no lock file is needed because there is
  nothing to lock.

## Events across languages (445, 467, 446, 472)

Planned: one named-event bus with JSON-like payloads ([schema](schemas/message.schema.json)). Every message carries a
16-hex-digit `trace` id; a handler that sends another message copies the id, so one log search (`trace=...`) shows
the whole chain across Luau, Python and plugins, and across client and server when the id is sent over the network.
Not built today.

## Rust SDK (455, 456, 469, 473)

Audit checklist for the safe wrapper: every `unsafe` block has a comment saying why it is sound; no panic crosses the
C boundary (`catch_unwind` is used for each callback, already true); strings are copied at registration; handles are
not used after unload; the crate builds with `#![deny(unsafe_op_in_unsafe_fn)]`. Build commands:

| OS | Command | Output |
|---|---|---|
| Linux | `cargo build --release` | `target/release/libyour_plugin.so` |
| Windows | `cargo build --release` (MSVC toolchain) | `target\release\your_plugin.dll` |
| macOS | `cargo build --release` | `target/release/libyour_plugin.dylib` (loads trusted only; no sandbox yet) |

Plan: generate the Rust and Python API declarations from one schema file so they cannot drift, and run one
conformance suite (the same checks) against C, C++, Rust and Luau. Not built.

## Opening projects you did not make (834)

Planned "safe open": a project from outside opens with scripts disabled and plugins refused; the creator sees a list
of what the project contains (scripts, plugins, network use) and chooses to enable it. Today scripts run in Play, and
third-party plugins are sandboxed.

## Secrets (618)

Keys and tokens are never in the repository or in a download. Servers read them from environment variables
(for example `KRONOS_API_URL`, and a moderation key when one exists); CI reads them from the repository's encrypted
secrets. A secret that is committed by mistake is treated as leaked and replaced, not just deleted from history.
`scripts/release-check.sh` looks for obvious key shapes before a release.

## Trying a server locally for free (625)

```
docker build -f docker/kronos-server.Dockerfile -t kronos-server .
docker run --rm -p 7777:7777/udp -v "$PWD/games:/games:ro" kronos-server --port 7777 --game my-game
engine_runtime --client 127.0.0.1:7777
```

Nothing is rented, nothing is billed, and everything stops when you press Ctrl+C.

## Crash reproduction format (823)

A crash report should be enough to repeat the crash. The format is a text file with these sections, in order:
version and commit; operating system and GPU (`--gpu-info` output); the command line; the game and scene name;
the random seed (when one was used); the last 200 log lines; the stack trace. User name, home folder and e-mail are
removed. To replay: start the same version with the same command line and seed.

## Flaky tests (825)

A test that fails sometimes is written in the table below with its name, date, and what was seen. After three entries
it is fixed or rewritten, not retried. Never "rerun until green" without writing it down.

| Test | First seen | What happened | Status |
|---|---|---|---|
| crash handler "re-raised SIGABRT" | Oct 2026 | failed once, passed on rerun | watching |
| Studio multiplayer test | Oct 2026 | server did not start in 30 s while a build ran | passes alone |

## Low-end reference machine (863)

Target for "runs acceptably": 4 CPU cores, 8 GB RAM, an integrated GPU with Vulkan 1.3 (for example an Intel Iris Xe),
1080p, the Low quality preset (30 fps). Not measured yet; the numbers go in [BENCHMARKS.md](BENCHMARKS.md).

## Authority and ownership (580)

Server decides, client asks: see the table in [SECURITY_PROCESS.md](SECURITY_PROCESS.md). A networked object has one
owner at a time (the server or one player); ownership moves only on the server, to the closest requester, after a minimum
hold time (`net/NetTools.hpp`, not wired in yet).

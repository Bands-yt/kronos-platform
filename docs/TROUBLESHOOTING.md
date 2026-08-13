# Troubleshooting

Real, specific fixes for the failures you're most likely to actually hit,
grouped by when they show up. If your problem isn't here, check
[engine/README.md](../engine/README.md)'s "Real bugs this pass found"
section — it documents real, historical failure modes and their fixes in
detail.

## At `cmake -S . -B build`

**`Could NOT find SDL2` / configure fails looking for SDL2**
SDL2 isn't installed as a system package, or it doesn't provide a CMake
config (`SDL2Config.cmake`) or `sdl2-config` on `PATH`. Install it via
your package manager (`pacman -S sdl2`, `apt install libsdl2-dev`, etc.)
— it's the one real dependency this build doesn't fetch for you.

**Configure hangs or fails with a network error**
First configure needs network access to fetch every other dependency
(EnTT, glm, Jolt, Luau, ENet, Dear ImGui, VMA, ImGuizmo, nlohmann_json)
via CMake `FetchContent`. If you're offline or behind a proxy that blocks
git, configure will fail partway through — there's no offline/vendored
fallback for these.

## At `cmake --build build`

**`find_program` can't find `glslc`**
`glslc` (part of the Vulkan SDK / `shaderc`) must be on `PATH` — the
build compiles every shader to SPIR-V at build time via a
`find_program(... REQUIRED)` custom command in `src/CMakeLists.txt`, it
isn't optional or deferred to runtime. Install the Vulkan SDK, or your
distro's `shaderc`/`glslang` package, and confirm `glslc --version` works
from the same shell you're building in.

**First build is slow / seems stuck**
Expected — first configure+build compiles Luau and Jolt from source,
both large. Subsequent builds are incremental as normal; only a fresh
checkout or a `Dependencies.cmake` version bump re-pays this cost.

## At `./build/src/engine_runtime` or `./build/src/studio`

**`Renderer: volkInitialize failed -- is the Vulkan loader ... installed?`**
No Vulkan loader (`libvulkan.so.1` / `vulkan-1.dll`) is installed on this
machine at all — install your GPU vendor's Vulkan driver package (or, on
Linux, `vulkan-icd-loader` plus the matching driver package for your
GPU).

**`Renderer: no Vulkan-capable physical devices found.`**
A loader is present but no GPU/driver actually exposes a Vulkan device —
common on a remote/headless machine with no real GPU attached, or a VM
without GPU passthrough. Neither binary has a software-rasterizer
fallback; a real Vulkan 1.3 device is required to get past device
selection.

**`Renderer: no device supports the required swapchain + dynamicRendering + synchronization2 feature set.`**
Your GPU is Vulkan-capable but too old — `dynamicRendering` and
`synchronization2` are both core Vulkan 1.3 features this renderer
requires unconditionally, not optional extensions with a fallback path.
Update your GPU driver first; if the hardware itself predates Vulkan
1.3-class features, it isn't supported.

**Window opens but nothing loads correctly after moving the binary to a different folder/machine**
Fixed as of the Alpha Packaging pass (see
[ALPHA_PACKAGING.md](ALPHA_PACKAGING.md)) — a binary now looks for real
`shaders/`/`assets/` directories sitting next to itself first. If you're
running a binary built directly from `engine/build/src/`, that's
expected to keep using the compile-time path into `engine/build/`
(there's no packaged-layout `shaders/`/`assets/` next to it) — that's
normal for a dev build, not a bug. If you're running a *packaged* build
(via `scripts/package_alpha.sh`) and still see this, confirm `shaders/`
and `assets/` are real, present directories sitting directly next to the
binary you're running, not one level up or down.

**Mouse feels stuck / can't get it back**
Expected — `engine_runtime` captures the mouse on launch (relative-mode
look). Alt+Tab away, or close the window, to release it. This isn't a
bug, just easy to forget on first run.

## At `./build/tests/engine_tests`

**A test that reads a template/fixture file fails to find it**
Run the suite from `engine/build/` (either `./tests/engine_tests` from
inside that directory, or `ctest` from inside it) — a handful of tests
reference real, checked-in fixture files via paths relative to that
directory (e.g. `../../templates/plugin/example.manifest`,
`../../examples/lua/moving_platform.lua`). Running the binary from a
different working directory breaks those specific tests, not the whole
suite.

**A test fails after you've edited engine source**
Rebuild `engine_tests` first (`cmake --build build --target engine_tests`)
— an out-of-date test binary against changed headers is the most common
cause of a confusing local failure that isn't reproducible after a clean
rebuild.

## Plugins

**Studio doesn't see my plugin**
The Plugin Browser scans a real, local `plugins/` directory (relative to
wherever you launched `studio` from) for `*.manifest` files — confirm
your plugin's manifest is directly inside that folder (or point the
Plugin Browser's directory field at wherever it actually is), and that
the manifest itself is valid (see
[PLUGIN_SYSTEM.md](PLUGIN_SYSTEM.md)). Copy `templates/plugin/` as a
known-good starting point if you're not sure the manifest format is
right.

**My plugin fails to load with a `compile error` or `runtime error`**
Both are real Luau errors, forwarded verbatim (with a real
`chunkName:line:` prefix) from the actual script compile/execution — read
the message, it names the real problem. Check
[LUA_CREATOR_EXPERIENCE.md](LUA_CREATOR_EXPERIENCE.md)'s "Lua error
formatting" section for exactly what the prefix means, and remember
Studio's plugin `world` table is smaller than a gameplay script's (no
`createEntity`/`destroy`/physics/animation calls) — see
[LUA_API.md](LUA_API.md)'s availability table if the error is about a
missing global.

## Multiplayer

**Client can't connect to a server on another machine**
Confirm the port is actually reachable (firewall/NAT) — `NetworkSession`
uses real ENet/UDP, there's no relay/NAT-punchthrough fallback in this
alpha. For local testing, `--server` and `--client 127.0.0.1` on the same
machine is the simplest way to confirm the binaries themselves work
before troubleshooting network reachability.

## Still stuck

Check `core::Logger`'s output (both binaries print to stdout/stderr, and
Studio's own Engine Log panel shows the same real, categorized log
stream — see [CRASH_TELEMETRY.md](CRASH_TELEMETRY.md)) — most real
failures in this codebase log a specific, real reason rather than failing
silently. If a run actually crashes, check for a `crash_report_*.txt`
file written next to wherever you ran the binary from — it contains the
real signal, a real backtrace, and the last ~50 real log lines leading up
to the crash.

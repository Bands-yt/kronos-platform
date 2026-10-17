# Platforms: what runs where, and what it would take

Updated 17 Oct 2026. Nothing below the first table is built; it is assessment and planning.

## Capability matrix (943)

| | Windows 11 | Linux | macOS | Web | Mobile | Consoles |
|---|---|---|---|---|---|---|
| Player | yes (preview) | yes (preview) | no | no | no | no |
| Studio | yes (preview) | yes (preview) | no | no | no | no |
| `kronos_server` | no | yes | no | no | no | no |
| Plugin sandbox | yes | yes (Landlock 5.13+) | no | n/a | n/a | n/a |
| Graphics | Vulkan 1.3+ | Vulkan 1.3+ | would need MoltenVK | would need WebGPU | Vulkan on Android; Metal on iOS | vendor APIs |

## Falling back when something is missing (946)

Rule: a missing optional feature turns itself off and says so; it never crashes and never silently looks wrong.

- Optional GPU features (fragment shading rate, host image copy, shader clock, compute derivatives) are detected at start; without
  them the same game runs the slower way (`core/GpuFeatures`).
- No GPU timestamp support: the Stats panel shows CPU time only.
- No Vulkan driver at all: the Player says so in plain words and points to the graphics driver.
- No Landlock or sandbox: third-party plugins are refused, not run unsandboxed.
- Rule for new features: write the fallback and a test for it in the same change.

## Console architecture assessment (937)

Consoles need a licensed developer programme, a devkit and a vendor SDK under NDA, so nothing can be built or shown
publicly before an agreement. What would have to change in the engine:

- a graphics backend for the console's API (Kronos is Vulkan-only; Switch and some others support Vulkan, others need a port);
- file, input, audio and network layers behind interfaces (SDL2 is used directly in several places today);
- no runtime code generation (Luau has an interpreter mode; Jolt and PocketPy are portable C++/C);
- a certification pass (memory, suspend/resume, controller rules, save data).
Verdict: not before the Windows and Linux releases are stable. Revisit with a publisher or a devkit in hand.

## Gamepad certification checklist, draft (939)

- [ ] Every action reachable on a pad (menus included), with no mouse needed.
- [ ] Controller disconnect pauses the game and shows a message; reconnect resumes.
- [ ] Button prompts match the pad in use (Xbox, PlayStation, Nintendo).
- [ ] Dead zones and sensitivity are adjustable. Vibration can be turned off.
- [ ] Two pads can play at once when the game supports it.
- [ ] No input is lost when switching between keyboard, mouse and pad.
- [ ] Text entry has an on-screen option.

## Web build experiment (936), cache versioning (949)

Plan only. A web player means compiling the engine to WebAssembly with WebGPU and a different file layer; Jolt, Luau,
PocketPy and ENet (needs WebSocket or WebRTC instead) all have web stories, but it is weeks of work and no Vulkan code
would carry over. First experiment: build only `engine_core` without graphics for wasm in CI, to see what breaks.
Cache versioning: every file the page loads gets the build's content hash in its name (`player.3f9a1c.wasm`), the page
itself is never cached, so a new release can't be mixed with old files.

## Mobile (911, 924)

Not planned for v5. Notes for later:
- Asset profiles (see `core/AssetTools.hpp` presets for audio): textures at half resolution with ASTC compression,
  meshes under 50k triangles, audio mono 24 kHz for voice.
- First-run onboarding on a phone: one screen, a "Play" button, touch controls drawn on screen, no Studio.
- Low-memory behaviour: unload the least recently used assets first (see `LruAssetCache` in `core/AssetTools.hpp`).

## Portability risk register (950)

| Risk | Likelihood | Effect | Plan |
|---|---|---|---|
| Vulkan-only graphics blocks macOS, web, iOS | certain | no Apple or web | MoltenVK for macOS first |
| SDL2 used directly in many files | high | porting touches many files | wrap behind a platform layer when a second platform starts |
| Plugin sandbox is OS-specific | certain | each OS needs its own | Windows done, macOS missing |
| Dynamic code (Luau JIT-free, plugins as DLLs) | medium | consoles and iOS forbid loading code | plugins off on those platforms |
| Windows tested in a VM and one laptop | medium | surprises on real PCs | fresh-PC test before release |
| One maintainer | high | slow fixes | keep docs and tests current |

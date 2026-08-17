# Kronos 0.1.0-alpha

This is the first alpha release of Kronos — a Vulkan-based reference
game engine and creator platform, built from source with a 10,898-check
automated test suite backing it. Every feature below is real, working
code in this repository, not a mockup or a stated future plan.

## Kronos Core Engine

- **Vulkan PBR renderer** — cascaded shadow maps, ray-traced shadows via
  inline `VK_KHR_ray_query` where the device supports it (falling back
  to rasterized CSM otherwise), screen-space reflections, HDR bloom, and
  **ACES tone mapping**.
- **Dynamic day/night weather cycle** — time-of-day lighting, weather
  state blending (clear/rain/fog and beyond), with a real, deliberate
  isolation boundary so indoor/preview scenes (Avatar Shop, Home,
  Studio) stay on neutral, weather-independent lighting instead of
  bleeding outdoor conditions into a UI context.
- **ECS architecture** — an EnTT-backed entity/component registry
  (`core::ECS`) shared, unmodified, by `engine_runtime` and `studio`
  alike: what you place and edit in Studio is the exact same live data
  the runtime simulates and renders.
- Jolt physics, a sandboxed Luau scripting VM (per-script memory/time
  budgets, real hot-reload), and real-time multiplayer with client
  prediction/server reconciliation.

## Kronos Studio

- **Command Palette (Ctrl+K)** — a VS Code-style floating action search:
  execute registered commands (`Spawn Baseplate`, `Toggle Physics
  Debug`, `Clear Engine Log`) or type an entity name to jump the
  viewport camera straight to it.
- **Live Lua Script Hot-Reload** — the Script Editor is wired to a real,
  entity-attached `core::Script` component; saving while Play-testing
  reloads just that script's environment in place, without resetting
  physics, the ECS, or the camera.
- **Network Conditioning Bar** — real Latency (0/50/150/300ms) and
  Packet Loss (0/2/5/10%) presets in the toolbar, applied directly to
  `NetworkSession`'s live ENet transport for testing multiplayer code
  under real bad-connection conditions.
- **F3 Performance Profiler** — a toggleable overlay with a live frame-
  time graph, draw call count, GPU memory, and Lua VM memory usage,
  reachable via F3 or View → Performance Overlay.
- **Viewport Surface Snapping (End)** — a real downward raycast from the
  selected entity's own origin, dropping it flush onto the surface
  below; paired with independent Grid Snap (0.25/1.0/5.0m) and Angle
  Snap (15°/45°/90°) toolbar controls for transform gizmo operations.
- Also included: an Explorer/Inspector/Viewport with a real ImGuizmo
  translate/rotate/scale gizmo, multi-selection property editing, a
  right-click "Enter Formula" popup for live math expressions
  (`+10`, `*2`, `180 - 45`) on Position/Rotation/Scale fields, Undo/Redo,
  autosave with multi-slot crash recovery, and a One-Click Package
  Exporter (`File → Package World`).

## Avatar System 2.0

- **Skinned skeletal rigging** — an 18-bone humanoid skeleton with real
  GPU vertex skinning (bone matrices, joint indices/weights), driving
  idle/walk/run/jump animation clips.
- **Vertex-color hair gradients** — real per-vertex color authoring and
  interpolation through the render pipeline, giving hair strands a
  genuine root-to-tip gradient rather than a single flat material color.
- **RuntimeShell state management** — a real, tested state machine
  driving `engine_runtime`'s Home/Session-Browser/In-Game flow, with
  live LAN session discovery and join/leave handling.

## Network & Safety

- **Integrated ENet network conditioning** — real send-side latency and
  packet-loss simulation in the transport layer itself (`ENetTransport`):
  latency delays the actual packet send via a local queue; loss only
  ever drops unreliable sends, since a dropped reliable packet with no
  retry would break the reliability guarantee it's supposed to have on
  a real lossy network too.
- **Local profanity filtering** — a real, leetspeak-aware, word-boundary-
  preserving chat filter (`moderation::ProfanityFilter`), fully wired
  into `NetworkSession`'s chat broadcast path, gated by per-world safety
  settings.
- **zlib-compressed `.kronos` world packaging** — a real, custom deflate-
  compressed archive format bundling scene data, a relative asset
  manifest, scene-authored scripts, and an optional captured thumbnail
  into a single portable file.

## Verification

- 4 build targets (`engine_core`, `studio`, `engine_runtime`,
  `engine_tests`), each rebuilt clean from scratch for this release with
  **zero compiler warnings**.
- **10,898 / 10,898** automated checks passing.
- Both `studio` and `engine_runtime` manually launched and confirmed
  stable post-build.

## Known limitations

This is an alpha. Notably: no CI-stamped build numbering (version is
hand-bumped), SSR is a single-frame raster fallback with no temporal
accumulation, `.kronos` packaging bundles an asset *manifest* rather
than asset file contents, and several Studio panels (Mesh colliders in
Play mode, PhysicsMaterial round-tripping through saved scenes) have
real, stated scope boundaries documented inline in the source. See
`docs/` for the fuller account of what's deliberately out of scope this
pass.

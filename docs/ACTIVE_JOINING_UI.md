# Kronos Platform — Active Joining UI

Implementation notes for [ACTIVE_JOINING_UI_CHECKLIST.md](ACTIVE_JOINING_UI_CHECKLIST.md)
(Sections 1-9, all done — the creator's own checklist, saved verbatim in
that file). `engine_runtime` (the player client), not Studio: Studio's
`NetworkOverlayPlugin` stays the creator-facing debug tool it already
was; this is a new, separate, first-of-its-kind pre-game shell for
`engine_runtime`, which previously booted straight into a hardcoded
bring-up scene with no menu at all.

Session discovery is real LAN broadcast/announce
(`net::LanSessionAnnouncer`/`net::LanSessionBrowser`), not an
internet-wide master/rendezvous server — the latter would need real
hosting/uptime this pass can't operate, the same practical limit that
put the prior Alpha checklist's Sections 10-12 out of scope.

## 1. Platform Home Screen
- [x] "Play" — opens the Session Browser
- [x] "Create" — honestly disabled, tooltip points to Kronos Studio (a
      separate binary; `engine_runtime` has no scene editor)
- [x] "Sessions" — same as "Play"
- [x] "Plugins" — honestly disabled, tooltip points to Kronos Studio
- [x] "Assets" — honestly disabled, tooltip points to Kronos Studio

## 2. Session Browser
- [x] List active sessions — real `net::LanSessionBrowser`, driven
      synchronously once per frame (no background thread), plus a
      "recently played" section from `net::SessionHistory`
- [x] Show name, host, player count, ping — ping is a real, measured
      unicast Echo/EchoReply round trip, not fabricated
- [x] Click to join — drives the real join path below

## 3. Join Flow
- [x] Show loading screen — `ShellState::Loading`
- [x] Perform handshake — real client-initiated `JoinRequest` →
      `JoinAccepted`/`JoinRejected` (protocol version + capacity checked
      server-side before anything is spawned)
- [x] Load scene / spawn player — `spawnNetworkedPlayerEntity` callback
      into `Application::setNetworkedLocalPlayerEntity()`

## 4. Leave Flow
- [x] Disconnect — graceful `ENetTransport::disconnectPeerGracefully()`
      + `flush()`, server notified promptly rather than relying on
      ENet's 5-30s default disconnect-timeout
- [x] Return to home screen — `ShellState::Home`

## 5. Player List Panel
- [x] Show connected players — real roster from
      `NetworkSession::clientKnownPlayers()`, fed by
      `PlayerRosterJoined`/`PlayerRosterLeft` (a joining client gets a
      retroactive snapshot for everyone already present, not just future
      joins)
- [x] Show profile names — real display names from `JoinRequest`, not
      placeholders
- [x] Show status (host, guest) — session-level role only
      (`isServer()`); the hosting process has no `PlayerId` of its own,
      so there's no per-player host flag to show (documented in code,
      see `RuntimeShell::lastJoinedHostDisplayName_`)

## 6. Session Metadata Panel
- [x] Session name — `NetworkSession::sessionName()`
- [x] Version compatibility — `kNetworkProtocolVersion`, exact-match
      integer (deliberately not a semver range like `ProjectFile`)
- [x] Host profile — `lastJoinedHostDisplayName_`, captured from the
      `DiscoveredSession` at join time
- [x] Session ID — `NetworkSession::sessionId()`

## 7. Error UI
- [x] Version mismatch — `JoinFailureReason::VersionMismatch`, shows the
      server's own protocol version
- [x] Session full — real rejection (ENet peer cap over-provisioned by
      `kSessionFullRejectionHeadroom` so the app-level check actually runs)
- [x] Network failure — synthesized `DisconnectReason::ConnectionLost`
      when a peer drops without a prior graceful `Disconnect` message
- [x] Session closed — `DisconnectReason::SessionClosed`, broadcast
      before server `shutdown()` tears the transport down

## 8. Lua Bindings for UI — [LUA_API.md](LUA_API.md#ui--screen-space-hud-drawing-and-shell-control)
- [x] `ui.sessionBrowser()`
- [x] `ui.playerList()`
- [x] `ui.joinSession(id)`
- [x] `ui.leaveSession()`

All four forward through `core::ScriptShellController` to the exact same
`RuntimeShell` methods the native panel buttons call — one real
join/leave code path, not two independently-drifting ones. Real, honest
no-ops (never an error) outside `engine_runtime`'s plain Home Screen
launch path.

## 9. Plugin Hooks — [LUA_API.md](LUA_API.md#events--lifecycle-and-world-callbacks)
- [x] `onSessionJoin`
- [x] `onSessionLeave`
- [x] `onPlayerJoin`
- [x] `onPlayerLeave`

Registered unconditionally in `Scripting::registerBindings()` (so the
`events.*` table looks the same everywhere), fired from
`core::Application::startNetworking()`'s callbacks into
`net::NetworkSession`. Extended the existing `unload()` use-after-free
purge filter to these four new callback vectors — the same fix built for
the original four event types, applied at the same time as these were
added rather than as an afterthought.

## Verification

Real, headless-testable end to end (real loopback ENet, real loopback
UDP, real Luau VM, no mocks — matching this suite's existing discipline
throughout): protocol join/reject/disconnect/roster logic, LAN
announce/discover/prune/ping, scripting event registration/firing/purge,
the pure `ShellState` transition function, and Lua binding call-through
including the no-op-without-a-controller case. Full suite: **9737/9737
checks passing**, zero regressions across the whole build.

**Honestly not GPU/pixel-testable in this sandboxed environment**: the
actual ImGui panel rendering, layout, and click hit-testing of the Home
Screen, Session Browser, Player List, Session Metadata, Error UI, and
Loading screen. These need a live, windowed `./engine_runtime` launch on
real hardware for visual inspection — flagged here rather than silently
skipped or claimed as covered.

# Kronos Platform — Active Joining UI (Final Steps)

Saved verbatim from the creator's own planning checklist.

**Status: all 9 sections done.** See
[ACTIVE_JOINING_UI.md](ACTIVE_JOINING_UI.md) for the full implementation
notes, per-item audit trail, and verification scope (9737/9737 tests
passing).

## 1. Platform Home Screen
- "Play"
- "Create"
- "Sessions"
- "Plugins"
- "Assets"

## 2. Session Browser
- List active sessions
- Show name, host, player count, ping
- Click to join

## 3. Join Flow
- Show loading screen
- Perform handshake
- Load scene
- Spawn player

## 4. Leave Flow
- Disconnect
- Return to home screen

## 5. Player List Panel
- Show connected players
- Show profile names
- Show status (host, guest)

## 6. Session Metadata Panel
- Session name
- Version compatibility
- Host profile
- Session ID

## 7. Error UI
- Version mismatch
- Session full
- Network failure
- Session closed

## 8. Lua Bindings for UI
- `ui.sessionBrowser()`
- `ui.playerList()`
- `ui.joinSession(id)`
- `ui.leaveSession()`

## 9. Plugin Hooks
- `onSessionJoin`
- `onSessionLeave`
- `onPlayerJoin`
- `onPlayerLeave`

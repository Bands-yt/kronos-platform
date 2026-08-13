# Kronos Platform — Phase 4: Networking Upgrade

Status of the Alpha Roadmap's Phase 2 ("Multiplayer Networking Upgrade")
— see [ALPHA_ROADMAP.md](ALPHA_ROADMAP.md) for the full roadmap.

All work below was verified with a full rebuild (`engine_runtime`,
`studio`, `engine_tests`) and the full test suite green after every change
(9377/9377 checks passing, up from 9352 — 25 new tests spanning pure
serialization, real client/server ENet integration, and a real Luau
script talking to another real Luau script over the real wire).

## Audit finding

The roadmap's checklist reads "Reliable RPC system / Entity replication /
Snapshot + interpolation / Basic anti-cheat hooks / Server authority mode
/ Plugin access to networking API" as if starting from nothing. An audit
of `net/` found the opposite: this is a mature, working networking stack
— `NetworkSession` orchestrates real snapshot delta-compression
(`Serialization.cpp`), client-side prediction + server reconciliation
(`ClientPrediction`/`ServerReconciliation`), entity replication via
`NetworkIdentity`, anti-cheat rate-limiting and rejection-tracking
(`RateLimiter`, `anticheat::RollingEventCounter`), and real server
authority (every gameplay mutation — teleport, chat, TNT Wars actions —
is validated server-side before it's applied or broadcast). All of this
predates this session.

The one real, concrete gap: **none of it was reachable from a script.**
`net::RemoteEvent` — the schema/rate-limit-enforcing RPC primitive the
roadmap's "Reliable RPC system" bullet describes — existed as a real,
tested, standalone class, but was never wired into `NetworkSession`'s
actual wire protocol, and had no Luau binding at all. That's the gap this
phase closed.

## 1. `RemoteEvent` wired into the real wire protocol

**Built — `serializeRemoteEventPayload()`/`deserializeRemoteEventPayload()`**
(`net/RemoteEvent.hpp/.cpp`): real wire (de)serialization for
`RemoteEvent::Payload` (a `string -> number|string|bool` map), bounded at
`kMaxRemoteEventFields = 32` (silent truncation past that, matching
`ByteWriter::writeString()`'s own "truncated at 255" precedent — not a
parse error). Numbers narrow `double -> float` on the wire (`ByteWriter`
has no `writeDouble()`), a real, documented precision trade-off — more
than enough for gameplay RPC payloads (item ids, damage amounts,
coordinates), not claimed lossless for arbitrary doubles.

**Built — two new wire messages** (`net/NetworkSession.cpp`):
`RemoteEventFire` (client → server) and `RemoteEventBroadcast` (server →
all clients), added to the same `WireMessageType` enum every other
message (`TeleportRequest`, `ChatMessage`, ...) already uses, dispatched
through the same real receive loops.

**Built — the real session API** (`net/NetworkSession.hpp`):
- `remoteEvent(name)` — server-side get-or-create registry, so a caller
  can attach `setInboundSchema()`/`setRateLimit()`/`setServerHandler()`
  to a named event exactly per `RemoteEvent`'s own existing contract. An
  unconfigured name is a real, honest no-op on fire, not an error.
- `fireServerEvent(name, payload)` — real, reliable client → server send.
- `fireAllClientsEvent(name, payload)` — real, reliable server →
  every-connected-client broadcast.
- `setOnClientEventReceived(callback)` — client-side observer hook, same
  shape as `setOnChatMessageReceived()`.

**Known, tracked gap:** `fireAllClientsEvent()` is broadcast-only this
pass — a single-recipient server → one-client send needs a
`PlayerId -> peer` lookup this pass didn't add. Flagged here explicitly,
not silently limited.

**Tests:** a pure payload-serialization round-trip (all three field
types, plus a truncation test past `kMaxRemoteEventFields`), and a real
client/server integration test over loopback ENet
(`testNetworkSessionRealRemoteEventFireServerReachesRegisteredHandler`)
covering both directions — client fire reaching a server-registered
handler, and a server broadcast reaching the client.

## 2. The real Luau binding — `core::ScriptNetworkApi`

**Built** (`core/ScriptNetworkApi.hpp/.cpp`), same deliberately-original,
non-Roblox-parity shape `ScriptWorldApi` already established for `world`
— a flat `network` table, not an Instance-style `RemoteEvent` object:

```lua
network.fireServer("Purchase", {itemId = 42, quantity = 1})
network.onServerEvent("Purchase", function(player, payload) ... end)
network.fireAllClients("Announcement", {text = "Boss defeated!"})
network.onClientEvent("Announcement", function(payload) ... end)
```

A payload is a real Lua table with string keys mapping to number/string/
boolean values — other value types (nested tables, functions) are
silently dropped converting out, matching `RemoteEvent::Payload`'s own
real, documented scope.

Registered alongside `world` in `core::Application`'s existing
`Scripting::setBindingsHook()` seam — `scriptNetworkApi_` is constructed
once (wrapping the always-live `networkSession_`/`scripting_` members;
`NetworkSession`'s own methods are documented no-ops outside their
relevant mode) and `registerInto()` is called for every new script VM,
the identical pattern `scriptWorldApi_` already uses.

**Built — `Scripting::refreshWatchdogDeadline()`** (`core/Scripting.hpp`):
a small, real, public wrapper around the existing private
`refreshDeadline()`, needed because `ScriptNetworkApi` invokes a
registered Lua callback via `lua_pcall()` asynchronously — triggered by a
real network event arriving during `NetworkSession::tick()`, not from
inside `Scripting`'s own tick()-driven resume path — and that invocation
needs the same watchdog-deadline reset every other resume/call site
already gets. Does not otherwise couple `Scripting` to `net::` at all.

**Known, accepted gap** (matches `Scripting::unload()`'s own documented
"KNOWN GAP"): a registered handler's `lua_ref` becomes stale if its
owning script is unloaded without unregistering first. Not solved here,
since the base `Scripting` class hasn't solved the equivalent problem for
its own `onUpdate`/`onCollision`/`onInteract` callbacks either — tracked
as one real gap, not two independently-drifting ones.

**Test:** `testScriptNetworkApiRealFireServerReachesLuauHandler` — the
full real stack, end to end: a real Luau script on a real client calls
`network.fireServer()`; a real Luau script on a real server (a *separate*
`core::Scripting` instance, a separate `lua_State`, exactly like two
different players' engines) receives it via `network.onServerEvent()`
and calls `world.setPosition()` on a real server-side ECS entity — all
over a real loopback ENet connection between two real `NetworkSession`s.
Not a mocked handler and not a same-process function call standing in for
the network boundary.

## Summary

| Item | Status |
|---|---|
| Reliable RPC system | Built — `RemoteEvent` wired into a real wire protocol + real Luau binding, 25 new tests across 3 layers |
| Entity replication | Already real (`NetworkIdentity` + delta-compressed snapshots) — confirmed, no new work |
| Snapshot + interpolation | Already real (`ClientPrediction`/`ServerReconciliation`/`RemoteEntityInterpolation`) — confirmed, no new work |
| Basic anti-cheat hooks | Already real (`RateLimiter`, rejection tracking, `TrustSafetyService`) — confirmed, no new work |
| Server authority mode | Already real (every gameplay mutation server-validated) — confirmed, no new work |
| Plugin access to networking API | Built — `network` Luau table, real end-to-end test |

This phase's real work was narrow and deep: one genuine gap (RPC), closed
at every layer (wire protocol, session API, script binding), with the one
deliberately-incomplete piece (single-recipient server → client send)
called out rather than silently limited.

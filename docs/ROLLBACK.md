# Rollback matches and deterministic physics

Kronos physics is deterministic: the same scene with the same inputs gives
bit-identical results on every machine, whatever the CPU core count. Jolt is
built with `CROSS_PLATFORM_DETERMINISTIC` (no fused multiply-adds, Jolt's own
trig), and engine code is compiled with `-ffp-contract=off`.

This is what makes rollback netcode possible: every player simulates the
whole world, and nothing waits on a server.

## Playing a match

```
engine_runtime --rollback-host 2                    # host, waits for 2 players
engine_runtime --rollback-join 192.168.1.20         # join that host
```

| Flag | Meaning |
|---|---|
| `--rollback-host <players>` | Host a match for that many players (up to 8). Only the host needs an open port. |
| `--rollback-join <address>` | Join a host. |
| `--rollback-port <port>` | Port to host or join on (default 7790). |
| `--game <slug>` | Play that game's scene instead of the default world. |
| `--rollback-latency <ms>`, `--rollback-loss <percent>` | Simulate a bad connection, for testing. |

While waiting for players the world is frozen. Once everyone has joined,
each player gets a character capsule next to the spawn point and the match
starts. The window title shows your player number, ping and rollback count.

A match ends (and you carry on alone) when:

- a player leaves;
- a joiner's scene doesn't match the host's when the match starts
  ("this copy of the game differs from the host's");
- the players' games drift apart ("went out of sync at frame N"). Peers
  exchange checksums of every confirmed frame, so this is caught within a few
  frames.

## How it works

- `core::Physics::saveState/restoreState/stateHash` snapshot everything a
  step changes: moving bodies, the contact cache, constraints and gravity.
  Static bodies are left out because they never change.
- `core::PhysicsRollback` builds on `net::RollbackSession`. Each frame it
  records the local input and runs the game's step function, then the physics
  step, then takes a snapshot. When a remote input arrives that differs from
  the prediction, it restores the snapshot from before that frame and
  resimulates up to the present. Contacts from each step are handed to the
  next frame's step function, so game logic sees the same contacts after a
  rollback.
- `net::RollbackPeer` exchanges inputs. Each packet repeats every input a peer
  hasn't acknowledged, so packets can be sent unreliably and loss costs
  nothing but a little delay. Peers that run ahead of slower ones skip an
  occasional frame to stay level.
- `net::RollbackNetSession` handles the lobby over ENet. The host relays
  packets between the other players.
- `CharacterController::simulateRollback` is the normal walk, run, jump, slope
  and step-up movement, rewritten to use only deterministic math
  (`core::det::sin/cos/atan2`).

## Replays

`PhysicsRollback` can record a `PhysicsReplay`: the starting state, every
player's input for every frame, and a checksum per frame (`.kreplay` via
`save/load`). `PhysicsReplay::verify` replays it into a fresh world and reports
the first frame whose checksum differs. The engine tests use this to check
that physics stays deterministic across thread counts.

## Rules for game code in a match

Anything that changes physics must happen inside the rollback step, and only
from the frame number, the inputs and the rollback state:

- Don't create or destroy bodies during a match. Snapshots can't bring them
  back, and the match ends with an error if this happens.
- Don't push bodies around from scripts or per-frame app code. Moving
  platforms are driven inside the rollback step during a match for this
  reason.
- Use `core::det::sin/cos/atan2` rather than `std::sin` and friends in
  simulation code. The C library's versions can differ in the last bit
  between operating systems.

Scripts still run during a match and are fine for anything that doesn't
touch physics (UI, sounds, effects). Rollback-aware Luau, where scripts run
inside the simulation with their state rolled back, is a follow-up.

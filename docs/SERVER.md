# kronos_server

`kronos_server` hosts a game without a window. It runs the same simulation as the
Player (physics, Luau, Python, Roblox remotes, DataStores, networking) but needs no
display, no GPU, no ImGui and no SDL library, so it fits in a small container.

```
kronos_server --port 7777 --game <slug> [--server-key <key>]
```

| Option | Meaning |
|---|---|
| `--port N` | UDP port to listen on (default from the engine config) |
| `--game <slug>` | Game to host: a folder under `KRONOS_GAMES_DIR` (or the install's `games/`), or a published slug fetched from the backend. Without it the server hosts an empty ground plane |
| `--server-key <key>` | Sent with a heartbeat to the backend every 10 s, as the provisioner expects |

`SIGINT`/`SIGTERM` stop it cleanly. Logs go to stdout and `kronos_server.log`.
`KRONOS_GAMES_DIR` and `KRONOS_API_URL` work as for the Player.

## How it is built

It is the same engine code. `server/SdlNull.cpp` supplies do-nothing versions of the
few SDL functions shared code still calls (plus a clock and the quit signal), and the
link uses `--as-needed`, so the binary has no `libSDL2`, X11 or video-codec
dependencies. The graphics code is still compiled in but never started. Splitting the
simulation out of `Application` properly is a later step (`Renderer.hpp` is still one class).

`engine/tests/remote_network_smoke.sh <runtime> <port> <kronos_server>` runs the
Remote Check place with this binary as the server.

Linux only for now; the Windows build hasn't been tried.

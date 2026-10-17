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
link uses `--as-needed` (the smoke script checks this with `ldd`: pass the server binary as its third argument), so the binary has no `libSDL2`, X11 or video-codec
dependencies. The graphics code is still compiled in but never started. Splitting the
simulation out of `Application` properly is a later step (`Renderer.hpp` is still one class).

`engine/tests/remote_network_smoke.sh <runtime> <port> <kronos_server>` runs the
Remote Check place with this binary as the server.

## Run it in a container

```
docker build -f docker/kronos-server.Dockerfile -t kronos-server .
docker run --rm -p 7777:7777/udp -v "$PWD/games:/games:ro" kronos-server --port 7777 --game my-game
```

The image builds only `kronos_server` (no Studio) on Ubuntu 24.04 and runs as a normal user.
Game folders go in the `/games` volume (`KRONOS_GAMES_DIR`). `docker/compose.example.yml`
does the same with `docker compose up`. Podman works with the same commands. Players join
with `engine_runtime --client <host>:7777`. Matchmaking and "deploy to the cloud" are not built;
for now you run the container on any machine that can open the UDP port.

Linux only for now; the Windows build hasn't been tried.

## Host it yourself

Kronos has no cloud hosting. Anyone can run a server:

1. Build or download `kronos_server` (Linux), or use the container above.
2. Put the game folder where the server finds it (`KRONOS_GAMES_DIR`).
3. Open the UDP port (default 7777) on the machine and on your router or cloud firewall.
4. Start it: `kronos_server --port 7777 --game my-game`.
5. Players join with `engine_runtime --client <your-address>:7777`.

Things to know:

- Use a normal user, not root. The server needs no files outside its game folder and log.
- Put it behind a firewall that allows only the game port.
- A server counts players, not CPU: start with one core and 512 MB and watch `kronos_server.log`.
- Restart it with `systemd` or `docker run --restart unless-stopped` so a crash comes back.
- Nothing is billed by Kronos. If you rent a machine, the host bills you.

## Kubernetes

Not now. The container image is the only supported way to run the server in a cluster, and that image is new.
Kubernetes would be worth looking at once all of these are true: a matchmaker that can start servers on demand,
metrics from the server (players, tick time, memory), and a person who needs more than one machine. Until then a
single container on one machine is simpler and cheaper.

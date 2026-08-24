// Roblox-style on-demand ("JIT") game server provisioning.
//
// /v1/sessions/allocate calls provisionServerForGame(game) exactly once,
// right after it finds zero alive servers for a game -- see routes.js.
// This spawns a real `engine_runtime --server <port> --game <slug>
// --server-key <key>` process (main.cpp's own --game/--server-key
// handling), registers it in game_servers the same way every other
// server gets registered (a direct INSERT -- there has never been a
// separate "register" endpoint, see game_servers' own schema comment),
// and waits for its first real heartbeat before telling the caller it's
// ready. A server that never becomes healthy in time is killed rather
// than left running unconfirmed.
//
// Deliberately a real, honest no-op when config.engineRuntimePath is
// unset (see config.js's own comment) -- callers must treat `false` the
// same way they always treated "still nothing alive" before this
// feature existed.
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';

import { config } from '../config.js';
import { query } from '../db.js';
import { redis, keys } from '../redis.js';

// One in-flight spawn per game at a time: two allocate requests racing
// for the same empty game must not spin up two servers. Concurrent
// callers for the same game id share the same promise instead.
const inFlightSpawns = new Map(); // gameId -> Promise<boolean>

// Ports this process has itself handed to a still-tracked child, so a
// second spawn (for a different game) picks a different one. Not a
// substitute for a real port-allocation service across multiple backend
// instances -- see this file's own header comment on scope.
const claimedPorts = new Set();

function claimPort() {
  for (let port = config.jitServerPortRangeStart; port <= config.jitServerPortRangeEnd; port += 1) {
    if (!claimedPorts.has(port)) {
      claimedPorts.add(port);
      return port;
    }
  }
  return null;
}

// `child` is checked every poll so a process that exits immediately
// (crashed, failed to bind its port, missing binary) fails FAST -- real,
// observed behavior without this: a dead child before this existed still
// made allocate() (and anything waiting on it, including this file's own
// test) block for the entire timeout before giving up, even though there
// was never going to be a heartbeat to wait for.
async function waitForHeartbeat(serverKey, timeoutSeconds, child) {
  const deadline = Date.now() + timeoutSeconds * 1000;
  while (Date.now() < deadline) {
    const alive = await redis.get(keys.serverHeartbeat(serverKey));
    if (alive) return true;
    if (child.exitCode !== null || child.signalCode !== null) return false;
    await new Promise((resolve) => setTimeout(resolve, 500));
  }
  return false;
}

// Real, honest cleanup for a server that crashed, was killed after
// timing out, or otherwise exited: its game_servers row must stop being
// "registered" immediately, or the next allocate for this game would
// find this same dead row (enabled = TRUE) and never try provisioning a
// fresh one. Liveness itself was already Redis-TTL-driven and needed no
// help here (see game_servers' own schema comment) -- this only affects
// the "is a server allowed to host at all" question, not "is it alive
// right now".
async function disableServerRow(serverKey) {
  try {
    await query(`UPDATE game_servers SET enabled = FALSE WHERE server_key = $1`, [serverKey]);
  } catch (err) {
    console.error('[provisioner] failed to disable dead server row %s: %s', serverKey, err.message);
  }
}

export async function provisionServerForGame(game) {
  if (!config.engineRuntimePath || !config.gamesDir) return false;

  const existing = inFlightSpawns.get(game.id);
  if (existing) return existing;

  const promise = spawnAndWait(game).finally(() => {
    inFlightSpawns.delete(game.id);
  });
  inFlightSpawns.set(game.id, promise);
  return promise;
}

async function spawnAndWait(game) {
  const port = claimPort();
  if (port === null) {
    console.error('[provisioner] no free port in range for game "%s"', game.slug);
    return false;
  }

  const serverKey = `srv-${crypto.randomBytes(12).toString('hex')}`;
  try {
    await query(
      `INSERT INTO game_servers (server_key, game_id, host, port, region, max_players)
       VALUES ($1, $2, $3, $4, 'jit-local', 12)`,
      [serverKey, game.id, config.jitServerHost, port],
    );
  } catch (err) {
    claimedPorts.delete(port);
    console.error('[provisioner] failed to register a server row for "%s": %s', game.slug, err.message);
    return false;
  }

  console.log(
    '[provisioner] spawning "%s" (%s) on port %d as server %s',
    game.title, game.slug, port, serverKey,
  );

  const child = spawn(
    config.engineRuntimePath,
    ['--server', String(port), '--game', game.slug, '--server-key', serverKey],
    {
      env: {
        ...process.env,
        KRONOS_API_URL: config.jitServerApiUrl || config.publicBaseUrl,
        KRONOS_GAMES_DIR: config.gamesDir,
      },
      stdio: 'ignore',
      detached: false,
    },
  );

  let exited = false;
  child.on('exit', (code, signal) => {
    exited = true;
    claimedPorts.delete(port);
    console.log('[provisioner] server %s ("%s") exited (code=%s signal=%s)', serverKey, game.slug, code, signal);
    disableServerRow(serverKey);
  });
  child.on('error', (err) => {
    console.error('[provisioner] failed to spawn engine_runtime for "%s": %s', game.slug, err.message);
  });

  const alive = await waitForHeartbeat(serverKey, config.jitSpawnTimeoutSeconds, child);
  if (!alive && !exited) {
    console.error(
      '[provisioner] server %s ("%s") did not become healthy within %ds -- killing it',
      serverKey, game.slug, config.jitSpawnTimeoutSeconds,
    );
    child.kill();
    await disableServerRow(serverKey);
    claimedPorts.delete(port);
  }
  return alive;
}

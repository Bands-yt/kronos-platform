import express from 'express';

import { config } from '../config.js';
import { query } from '../db.js';
import { asyncRoute, badRequest, notFound, serviceUnavailable } from '../errors.js';
import { redis, keys } from '../redis.js';
import { requireAuth } from '../middleware/auth.js';
import { issueJoinTicket, verifyJoinTicket } from '../auth/tokens.js';

export const sessionRouter = express.Router();

// --- server heartbeat ------------------------------------------------------
//
// Called by each dedicated game server on a timer. Liveness lives in
// Redis with a TTL rather than in Postgres, so a server that dies simply
// stops appearing -- no reaper job, no stale rows, no allocating players
// onto a machine that crashed.
sessionRouter.post(
  '/servers/:serverKey/heartbeat',
  asyncRoute(async (req, res) => {
    const { serverKey } = req.params;
    const players = Math.max(0, Number(req.body?.players) || 0);

    const { rows } = await query(
      `SELECT gs.id, gs.game_id, gs.max_players, gs.enabled
         FROM game_servers gs WHERE gs.server_key = $1`,
      [serverKey],
    );
    if (rows.length === 0) throw notFound('Unknown server key.');
    const server = rows[0];

    const ttl = config.serverHeartbeatTtlSeconds;
    await redis
      .multi()
      .set(keys.serverHeartbeat(serverKey), '1', 'EX', ttl)
      .set(keys.serverPlayers(serverKey), String(players), 'EX', ttl)
      .exec();

    // Recompute the game's total from its live servers only.
    await recomputeGamePlayerCount(server.game_id);

    res.json({ status: 'ok', ttl_seconds: ttl });
  }),
);

async function recomputeGamePlayerCount(gameId) {
  const { rows } = await query(`SELECT server_key FROM game_servers WHERE game_id = $1 AND enabled = TRUE`, [gameId]);
  if (rows.length === 0) {
    await redis.del(keys.gamePlayers(gameId));
    return 0;
  }
  const liveFlags = await redis.mget(rows.map((r) => keys.serverHeartbeat(r.server_key)));
  const liveKeys = rows.filter((_, i) => liveFlags[i] !== null).map((r) => r.server_key);
  if (liveKeys.length === 0) {
    await redis.del(keys.gamePlayers(gameId));
    return 0;
  }
  const playerValues = await redis.mget(liveKeys.map((k) => keys.serverPlayers(k)));
  const total = playerValues.reduce((sum, v) => sum + (Number(v) || 0), 0);
  // Same TTL as a heartbeat: if every server goes quiet, the aggregate
  // expires on its own instead of lingering as a stale number.
  await redis.set(keys.gamePlayers(gameId), String(total), 'EX', config.serverHeartbeatTtlSeconds);
  return total;
}

// --- allocation ------------------------------------------------------------

sessionRouter.post(
  '/allocate',
  requireAuth,
  asyncRoute(async (req, res) => {
    const slug = (req.body?.game_slug || '').toString().trim();
    if (!slug) throw badRequest('game_slug is required.');

    const { rows: games } = await query(`SELECT id, slug, title FROM games WHERE slug = $1 AND published = TRUE`, [slug]);
    if (games.length === 0) throw notFound('No such published game.');
    const game = games[0];

    const { rows: servers } = await query(
      `SELECT server_key, host, port, region, max_players
         FROM game_servers WHERE game_id = $1 AND enabled = TRUE`,
      [game.id],
    );
    if (servers.length === 0) throw serviceUnavailable('No servers are registered for this game yet.');

    // Only consider servers that are actually alive right now.
    const liveFlags = await redis.mget(servers.map((s) => keys.serverHeartbeat(s.server_key)));
    const live = servers.filter((_, i) => liveFlags[i] !== null);
    if (live.length === 0) throw serviceUnavailable('No servers for this game are online right now.');

    const playerValues = await redis.mget(live.map((s) => keys.serverPlayers(s.server_key)));
    const withRoom = live
      .map((s, i) => ({ ...s, players: Number(playerValues[i]) || 0 }))
      .filter((s) => s.players < s.max_players);
    if (withRoom.length === 0) throw serviceUnavailable('Every server for this game is currently full.');

    // Most-loaded-with-room first: packs players together so games feel
    // populated, instead of scattering one player per empty server.
    withRoom.sort((a, b) => b.players - a.players);
    const chosen = withRoom[0];

    // The ticket is what makes this allocation meaningful. Without it a
    // client could connect straight to any server's ip:port and the
    // server would have no way to know whether we sent them.
    const ticket = issueJoinTicket({ userId: req.user.id, gameId: game.id, serverKey: chosen.server_key });

    res.json({
      game: { id: String(game.id), slug: game.slug, title: game.title },
      server: { host: chosen.host, port: chosen.port, region: chosen.region },
      join_ticket: ticket,
      expires_in: config.joinTicketTtlSeconds,
    });
  }),
);

// Called by a game server to validate a ticket a connecting client
// presented. Confirms we issued it, it has not expired, and it was issued
// for THIS server -- a ticket for server A must not open server B.
sessionRouter.post(
  '/verify-ticket',
  asyncRoute(async (req, res) => {
    const payload = verifyJoinTicket(req.body?.join_ticket);
    if (!payload) throw badRequest('Invalid or expired join ticket.');
    const serverKey = (req.body?.server_key || '').toString();
    if (serverKey && payload.srv !== serverKey) throw badRequest('This ticket was issued for a different server.');
    res.json({ valid: true, user_id: payload.uid, game_id: payload.gid, server_key: payload.srv });
  }),
);

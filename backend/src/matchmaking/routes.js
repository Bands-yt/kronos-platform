// Native matchmaking: a FIFO ticket queue in front of the existing
// server-allocation logic (allocatePlayerToGame, sessions/routes.js) --
// this file never picks a server itself, it only decides WHEN a queued
// player's turn comes, then hands off to that same real allocation path
// a plain /v1/sessions/allocate call already uses. Queue state is
// Redis-only (volatile, reconstructible), same "liveness/queues don't
// belong in Postgres" rule game_servers' own schema comment already
// established; only the durable per-player rating survives in Postgres.
import crypto from 'node:crypto';

import express from 'express';

import { config } from '../config.js';
import { query } from '../db.js';
import { asyncRoute, badRequest, forbidden, notFound } from '../errors.js';
import { requireAuth } from '../middleware/auth.js';
import { redis, keys } from '../redis.js';
import { allocatePlayerToGame } from '../sessions/routes.js';

export const matchmakingRouter = express.Router();

async function ratingFor(userId, gameId) {
  const { rows } = await query(`SELECT rating FROM player_ratings WHERE user_id = $1 AND game_id = $2`, [userId, gameId]);
  return rows.length > 0 ? rows[0].rating : 1000;
}

async function loadTicket(ticketId) {
  const raw = await redis.get(keys.matchmakingTicket(ticketId));
  return raw ? JSON.parse(raw) : null;
}

async function saveTicket(ticketId, ticket, ttlSeconds) {
  await redis.set(keys.matchmakingTicket(ticketId), JSON.stringify(ticket), 'EX', ttlSeconds);
}

matchmakingRouter.post(
  '/games/:slug/tickets',
  requireAuth,
  asyncRoute(async (req, res) => {
    const { rows: games } = await query(
      `SELECT id, slug, title, mature FROM games WHERE slug = $1 AND published = TRUE`,
      [req.params.slug],
    );
    if (games.length === 0) throw notFound('No such published game.');
    const game = games[0];

    // Same real age-gate sessions/routes.js's own /allocate applies,
    // checked here too: a ticket queued for a mature game bypasses that
    // check entirely if this route doesn't also apply it, since queued
    // tickets are later allocated straight through allocatePlayerToGame
    // with no second look at who's asking.
    if (game.mature) {
      const { rows: verified } = await query(`SELECT age_verified FROM users WHERE id = $1`, [req.user.id]);
      if (verified.length === 0 || !verified[0].age_verified) {
        throw forbidden('This game is age-restricted and your account is not age-verified.');
      }
    }

    const partySize = Math.min(Math.max(Number(req.body?.party_size) || 1, 1), 16);
    const region = String(req.body?.region || 'default').trim().slice(0, 40) || 'default';

    const ticketId = crypto.randomUUID();
    const ticket = {
      ticket_id: ticketId,
      user_id: req.user.id,
      game_id: game.id,
      region,
      party_size: partySize,
      rating: await ratingFor(req.user.id, game.id),
      status: 'queued',
      created_at: Date.now(),
    };
    const ttl = config.matchmakingTicketTtlSeconds;
    await saveTicket(ticketId, ticket, ttl);
    await redis
      .multi()
      .rpush(keys.matchmakingQueue(game.id, region), ticketId)
      .expire(keys.matchmakingQueue(game.id, region), ttl)
      .exec();

    res.status(201).json({ ticket_id: ticketId, status: 'queued', expires_in: ttl });
  }),
);

// Runs one real matching pass over a single game+region queue: drops any
// ticket that has expired/vanished from Redis, then matches same-band
// (or grace-expired) tickets by calling the exact same allocation path
// /v1/sessions/allocate uses. Called lazily from GET /tickets/:id below
// -- this stack has no cron/queue worker to run it on a timer, same
// "compute it live, on demand" convention catalog/routes.js's own
// livePlayerCounts already follows.
async function runMatchingPass(game, region) {
  const queueKey = keys.matchmakingQueue(game.id, region);
  const ticketIds = await redis.lrange(queueKey, 0, -1);
  if (ticketIds.length === 0) return;

  const raws = await redis.mget(ticketIds.map((id) => keys.matchmakingTicket(id)));
  const tickets = [];
  const staleIds = [];
  ticketIds.forEach((id, i) => {
    if (!raws[i]) { staleIds.push(id); return; }
    const ticket = JSON.parse(raws[i]);
    if (ticket.status !== 'queued') { staleIds.push(id); return; }
    tickets.push(ticket);
  });
  if (staleIds.length > 0) await redis.lrem(queueKey, 0, ...staleIds).catch(() => {});

  const now = Date.now();
  const band = config.matchmakingRatingBandPoints;
  const graceMs = config.matchmakingGraceSeconds * 1000;
  const matchedThisPass = new Set();

  for (const ticket of tickets) {
    if (matchedThisPass.has(ticket.ticket_id)) continue;
    const companion = tickets.find((other) => (
      other.ticket_id !== ticket.ticket_id
      && !matchedThisPass.has(other.ticket_id)
      && Math.abs(other.rating - ticket.rating) <= band
    ));
    const eligible = companion || (now - ticket.created_at >= graceMs);
    if (!eligible) continue;

    const group = companion ? [ticket, companion] : [ticket];
    let capacityRanOut = false;
    for (const t of group) {
      try {
        const result = await allocatePlayerToGame({ id: game.id, slug: game.slug, title: game.title }, t.user_id);
        matchedThisPass.add(t.ticket_id);
        await saveTicket(t.ticket_id, { ...t, status: 'matched', result }, config.matchmakingTicketTtlSeconds);
        await redis.lrem(queueKey, 0, t.ticket_id);
      } catch (err) {
        // No room right now -- leave this (and every later) ticket
        // queued rather than erroring the poller; the next poll tries
        // again.
        capacityRanOut = true;
        break;
      }
    }
    if (capacityRanOut) break;
  }
}

matchmakingRouter.get(
  '/tickets/:ticketId',
  requireAuth,
  asyncRoute(async (req, res) => {
    let ticket = await loadTicket(req.params.ticketId);
    if (!ticket) throw notFound('No such ticket, or it has expired.');
    if (String(ticket.user_id) !== String(req.user.id)) throw forbidden('This ticket belongs to a different player.');

    if (ticket.status === 'queued') {
      const { rows: games } = await query(`SELECT id, slug, title FROM games WHERE id = $1`, [ticket.game_id]);
      if (games.length > 0) {
        await runMatchingPass(games[0], ticket.region);
        ticket = await loadTicket(req.params.ticketId) || ticket;
      }
    }

    if (ticket.status === 'matched') {
      return res.json({ ticket_id: ticket.ticket_id, status: 'matched', ...ticket.result });
    }
    res.json({ ticket_id: ticket.ticket_id, status: 'queued' });
  }),
);

matchmakingRouter.delete(
  '/tickets/:ticketId',
  requireAuth,
  asyncRoute(async (req, res) => {
    const ticket = await loadTicket(req.params.ticketId);
    if (!ticket) return res.json({ status: 'cancelled' });
    if (String(ticket.user_id) !== String(req.user.id)) throw forbidden('This ticket belongs to a different player.');

    await redis.del(keys.matchmakingTicket(req.params.ticketId));
    await redis.lrem(keys.matchmakingQueue(ticket.game_id, ticket.region), 0, req.params.ticketId);
    res.json({ status: 'cancelled' });
  }),
);

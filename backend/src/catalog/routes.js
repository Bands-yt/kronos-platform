import express from 'express';

import { query } from '../db.js';
import { asyncRoute, badRequest, notFound } from '../errors.js';
import { redis, keys } from '../redis.js';
import { optionalAuth } from '../middleware/auth.js';

export const catalogRouter = express.Router();

// Live player counts come from Redis keys that game servers refresh by
// heartbeating. They are NEVER stored in Postgres and never estimated: a
// game with no live servers reports 0, because that is the truth. An
// invented "active players" number is worse than no number -- it is a
// number people will make decisions on.
async function livePlayerCounts(gameIds) {
  const counts = new Map(gameIds.map((id) => [String(id), 0]));
  if (gameIds.length === 0) return counts;
  try {
    const values = await redis.mget(gameIds.map((id) => keys.gamePlayers(id)));
    gameIds.forEach((id, i) => {
      const raw = Number(values[i]);
      counts.set(String(id), Number.isFinite(raw) && raw > 0 ? raw : 0);
    });
  } catch (err) {
    // Redis down: report 0 and say so in the response rather than
    // silently serving numbers we cannot stand behind.
    console.error('[catalog] player counts unavailable: %s', err.message);
    return null;
  }
  return counts;
}

catalogRouter.get(
  '/games',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const limit = Math.min(Math.max(Number(req.query.limit) || 24, 1), 100);
    const cursor = Number(req.query.cursor) || 0;
    const search = (req.query.q || '').toString().trim();

    // Keyset pagination on id, not OFFSET: stable under concurrent
    // publishes and does not degrade on deep pages.
    const params = [limit + 1];
    let where = 'g.published = TRUE';
    if (cursor > 0) {
      params.push(cursor);
      where += ` AND g.id < $${params.length}`;
    }
    if (search) {
      params.push(`%${search}%`);
      where += ` AND g.title ILIKE $${params.length}`;
    }

    const { rows } = await query(
      `SELECT g.id, g.slug, g.title, g.description, g.thumbnail_url, g.created_at,
              u.display_name AS creator_name, u.id AS creator_id
         FROM games g
         JOIN users u ON u.id = g.creator_id
        WHERE ${where}
        ORDER BY g.id DESC
        LIMIT $1`,
      params,
    );

    const hasMore = rows.length > limit;
    const page = hasMore ? rows.slice(0, limit) : rows;
    const counts = await livePlayerCounts(page.map((r) => r.id));

    res.json({
      games: page.map((r) => ({
        id: String(r.id),
        slug: r.slug,
        title: r.title,
        description: r.description,
        thumbnail_url: r.thumbnail_url,
        creator: { id: String(r.creator_id), display_name: r.creator_name },
        active_players: counts ? counts.get(String(r.id)) : null,
      })),
      // Explicitly surfaced so a client can render "player counts
      // unavailable" instead of a confident, wrong 0.
      player_counts_available: counts !== null,
      next_cursor: hasMore ? String(page[page.length - 1].id) : null,
    });
  }),
);

catalogRouter.get(
  '/games/:slug',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const { rows } = await query(
      `SELECT g.id, g.slug, g.title, g.description, g.thumbnail_url, g.created_at,
              u.display_name AS creator_name, u.id AS creator_id
         FROM games g JOIN users u ON u.id = g.creator_id
        WHERE g.slug = $1 AND g.published = TRUE`,
      [req.params.slug],
    );
    if (rows.length === 0) throw notFound('No such published game.');
    const g = rows[0];
    const counts = await livePlayerCounts([g.id]);
    res.json({
      game: {
        id: String(g.id),
        slug: g.slug,
        title: g.title,
        description: g.description,
        thumbnail_url: g.thumbnail_url,
        creator: { id: String(g.creator_id), display_name: g.creator_name },
        active_players: counts ? counts.get(String(g.id)) : null,
      },
      player_counts_available: counts !== null,
    });
  }),
);

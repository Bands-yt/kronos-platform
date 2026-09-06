// Global leaderboards. Score submission is server-authoritative only --
// same server_key-as-credential trust model inventory/routes.js's own
// grant/consume routes already use, never callable with an end-user
// access token (a client cannot be trusted to report its own score).
// Rank is always computed at read time with a SQL window function, never
// stored -- same "compute the real truth, don't cache a stale one"
// convention catalog/routes.js's own livePlayerCounts already
// established, so there is no separately-maintained rank column that can
// drift out of sync with the scores it's supposed to describe.
//
// Higher score is always better (the overwhelmingly common leaderboard
// convention) -- there is no per-board direction setting in this pass;
// a board that genuinely wants "lowest wins" (e.g. a speedrun time)
// should submit a negated score. Documented simplification, not an
// oversight.
import express from 'express';

import { query } from '../db.js';
import { asyncRoute, badRequest, notFound } from '../errors.js';
import { optionalAuth } from '../middleware/auth.js';
import { redis, channels } from '../redis.js';

export const leaderboardsRouter = express.Router();

const BOARD_KEY_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$/;

function validateBoardKey(raw) {
  const boardKey = String(raw || '').trim();
  if (!BOARD_KEY_RE.test(boardKey)) throw badRequest('board_key must be 1-100 characters: letters, numbers, "_", "." or "-".');
  return boardKey;
}

async function requireRegisteredServer(slug, serverKey) {
  const { rows } = await query(
    `SELECT gs.id, gs.game_id FROM game_servers gs JOIN games g ON g.id = gs.game_id
      WHERE gs.server_key = $1 AND g.slug = $2`,
    [serverKey, slug],
  );
  if (rows.length === 0) throw notFound('Unknown server key for that game.');
  return rows[0];
}

leaderboardsRouter.post(
  '/games/:slug/servers/:serverKey/scores',
  asyncRoute(async (req, res) => {
    const server = await requireRegisteredServer(req.params.slug, req.params.serverKey);
    const boardKey = validateBoardKey(req.body?.board_key);
    const userId = Number(req.body?.user_id);
    if (!Number.isFinite(userId)) throw badRequest('user_id must be a real user id.');
    const score = Number(req.body?.score);
    if (!Number.isFinite(score)) throw badRequest('score must be a real number.');
    const metadata = req.body?.metadata && typeof req.body.metadata === 'object' ? req.body.metadata : {};
    const policy = req.body?.policy === 'latest' ? 'latest' : 'best';

    let changed;
    let currentScore;
    if (policy === 'latest') {
      const { rows } = await query(
        `INSERT INTO leaderboard_entries (game_id, board_key, user_id, score, metadata) VALUES ($1, $2, $3, $4, $5)
         ON CONFLICT (game_id, board_key, user_id) DO UPDATE
            SET score = EXCLUDED.score, metadata = EXCLUDED.metadata, updated_at = NOW()
         RETURNING score`,
        [server.game_id, boardKey, userId, score, JSON.stringify(metadata)],
      );
      changed = true;
      currentScore = rows[0].score;
    } else {
      const { rows } = await query(
        `INSERT INTO leaderboard_entries (game_id, board_key, user_id, score, metadata) VALUES ($1, $2, $3, $4, $5)
         ON CONFLICT (game_id, board_key, user_id) DO UPDATE
            SET score = EXCLUDED.score, metadata = EXCLUDED.metadata, updated_at = NOW()
          WHERE EXCLUDED.score > leaderboard_entries.score
         RETURNING score`,
        [server.game_id, boardKey, userId, score, JSON.stringify(metadata)],
      );
      changed = rows.length > 0;
      if (changed) {
        currentScore = rows[0].score;
      } else {
        const existing = await query(
          `SELECT score FROM leaderboard_entries WHERE game_id = $1 AND board_key = $2 AND user_id = $3`,
          [server.game_id, boardKey, userId],
        );
        currentScore = existing.rows[0].score;
      }
    }

    if (changed) {
      await redis.publish(channels.leaderboard(server.game_id, boardKey), JSON.stringify({ user_id: userId, score: currentScore }));
    }
    res.json({ status: changed ? 'updated' : 'unchanged', score: Number(currentScore) });
  }),
);

leaderboardsRouter.get(
  '/games/:slug/:boardKey',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const boardKey = validateBoardKey(req.params.boardKey);
    const { rows: games } = await query(`SELECT id FROM games WHERE slug = $1`, [req.params.slug]);
    if (games.length === 0) throw notFound('No such game.');
    const gameId = games[0].id;

    const limit = Math.min(Math.max(Number(req.query.limit) || 25, 1), 200);
    const aroundUserId = req.query.around_user_id != null ? Number(req.query.around_user_id) : null;
    if (req.query.around_user_id != null && !Number.isFinite(aroundUserId)) {
      throw badRequest('around_user_id must be a real user id.');
    }

    if (aroundUserId !== null) {
      const { rows } = await query(
        `WITH ranked AS (
           SELECT le.user_id, u.display_name, le.score, le.metadata, le.updated_at,
                  RANK() OVER (ORDER BY le.score DESC) AS rank
             FROM leaderboard_entries le JOIN users u ON u.id = le.user_id
            WHERE le.game_id = $1 AND le.board_key = $2
         ), target AS (
           SELECT rank FROM ranked WHERE user_id = $3
         )
         SELECT r.* FROM ranked r, target t
          WHERE r.rank BETWEEN t.rank - $4 AND t.rank + $4
          ORDER BY r.rank`,
        [gameId, boardKey, aroundUserId, Math.floor(limit / 2)],
      );
      return res.json({
        entries: rows.map((r) => ({ ...r, user_id: String(r.user_id), score: Number(r.score) })),
        centered_on: String(aroundUserId),
      });
    }

    const { rows } = await query(
      `SELECT le.user_id, u.display_name, le.score, le.metadata, le.updated_at,
              RANK() OVER (ORDER BY le.score DESC) AS rank
         FROM leaderboard_entries le JOIN users u ON u.id = le.user_id
        WHERE le.game_id = $1 AND le.board_key = $2
        ORDER BY le.score DESC LIMIT $3`,
      [gameId, boardKey, limit],
    );
    res.json({ entries: rows.map((r) => ({ ...r, user_id: String(r.user_id), score: Number(r.score) })) });
  }),
);

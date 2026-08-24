import express from 'express';

import { config } from '../config.js';
import { query } from '../db.js';
import { asyncRoute, badRequest, conflict, forbidden, notFound } from '../errors.js';
import { redis } from '../redis.js';
import { requireAuth } from '../middleware/auth.js';
import { rateLimit } from '../middleware/rateLimit.js';
import { issueJoinTicket } from '../auth/tokens.js';

export const socialRouter = express.Router();

// Presence lives only in Redis, with a TTL a little over twice the 15s
// client heartbeat. A client that dies simply stops appearing -- no reaper,
// no stale "online" rows that outlive the session by hours.
const PRESENCE_TTL_SECONDS = 40;
const presenceKey = (userId) => `presence:${userId}`;

async function readPresence(userIds) {
  if (userIds.length === 0) return new Map();
  const raw = await redis.mget(userIds.map((id) => presenceKey(id)));
  const presence = new Map();
  userIds.forEach((id, i) => {
    if (!raw[i]) {
      presence.set(String(id), { status: 'offline' });
      return;
    }
    try {
      presence.set(String(id), JSON.parse(raw[i]));
    } catch {
      presence.set(String(id), { status: 'offline' });
    }
  });
  return presence;
}

// Guests are deliberately barred from the social graph. Enforced here on
// the server, not merely hidden in the launcher UI -- a client-side
// restriction is a suggestion.
async function assertNotGuest(userId) {
  const { rows } = await query(`SELECT is_guest, account_state, username FROM users WHERE id = $1`, [userId]);
  if (rows.length === 0) throw notFound('Account no longer exists.');
  if (rows[0].is_guest) {
    throw forbidden('Guest accounts cannot use friends. Create a free account to add friends and join their games.');
  }
  if (rows[0].account_state === 'terminated') throw forbidden('This account is suspended.');
  return rows[0];
}

// --- presence heartbeat ----------------------------------------------------

socialRouter.post(
  '/presence/heartbeat',
  requireAuth,
  asyncRoute(async (req, res) => {
    // 'in_studio' is a real, distinct state: a creator with Studio open
    // is present and reachable, but is not in a joinable session, so the
    // UI must not offer a "Join Game" button for them.
    const ALLOWED_PRESENCE = ['online_launcher', 'in_studio', 'in_game'];
    const status = ALLOWED_PRESENCE.includes(req.body?.status) ? req.body.status : 'online_launcher';
    const payload = {
      status,
      current_game_id: req.body?.current_game_id ? String(req.body.current_game_id) : null,
      current_server_id: req.body?.current_server_id ? String(req.body.current_server_id) : null,
    };
    await redis.set(presenceKey(req.user.id), JSON.stringify(payload), 'EX', PRESENCE_TTL_SECONDS);
    res.json({ status: 'ok', ttl_seconds: PRESENCE_TTL_SECONDS });
  }),
);

// --- user search -----------------------------------------------------------

socialRouter.get(
  '/users/search',
  requireAuth,
  rateLimit({ bucket: 'usersearch', limit: 120, windowSeconds: 300 }),
  asyncRoute(async (req, res) => {
    const q = String(req.query.q || '').trim();
    const limit = Math.min(Math.max(Number(req.query.limit) || 20, 1), 50);
    const offset = Math.max(Number(req.query.offset) || 0, 0);

    // A one- or two-character query matches most of the userbase and is
    // not a search, it is a scrape.
    if (q.length < 3) throw badRequest('Enter at least 3 characters to search.');

    // Escape LIKE metacharacters so a query of "%" cannot match everyone.
    const pattern = `%${q.replace(/[\\%_]/g, (m) => '\\' + m)}%`;

    const { rows } = await query(
      `SELECT u.id, u.username, u.display_name,
              f.status AS friendship_status,
              f.requester_id AS friendship_requester
         FROM users u
         LEFT JOIN friendships f
                ON (LEAST(f.requester_id, f.addressee_id) = LEAST(u.id, $2::bigint)
                AND GREATEST(f.requester_id, f.addressee_id) = GREATEST(u.id, $2::bigint))
        WHERE u.username_lower ILIKE $1 ESCAPE '\\'
          AND u.is_guest = FALSE
          AND u.account_state <> 'terminated'
          AND u.id <> $2::bigint
        ORDER BY u.username_lower
        LIMIT $3 OFFSET $4`,
      [pattern, req.user.id, limit + 1, offset],
    );

    const hasMore = rows.length > limit;
    const page = hasMore ? rows.slice(0, limit) : rows;

    res.json({
      results: page.map((r) => ({
        id: String(r.id),
        username: r.username,
        display_name: r.display_name,
        // Lets the client render Add / Pending / Friends without a second
        // round trip per row.
        relationship: r.friendship_status === 'accepted'
          ? 'friends'
          : r.friendship_status === 'pending'
            ? (String(r.friendship_requester) === String(req.user.id) ? 'request_sent' : 'request_received')
            : r.friendship_status === 'blocked'
              ? 'blocked'
              : 'none',
      })),
      next_offset: hasMore ? offset + limit : null,
    });
  }),
);

// Paginated account directory, distinct from /users/search: search
// answers "find this person", this answers "show me the directory".
// Keyset paginated on id so the page boundary stays stable while new
// accounts are being created underneath it -- OFFSET would silently skip
// or repeat rows as the table grows.
socialRouter.get(
  '/users',
  requireAuth,
  rateLimit({ bucket: 'userdir', limit: 120, windowSeconds: 300 }),
  asyncRoute(async (req, res) => {
    const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 200);
    const cursor = Number(req.query.cursor) || 0;

    const params = [limit + 1];
    // Brand-new accounts have no username yet. Excluding them made them
    // invisible in the directory until they claimed a handle, which is a
    // bad first experience for exactly the people most likely to be
    // looking for their friends. They are listed under their display
    // name instead; `username` stays null in the response so a client can
    // still tell the difference and prompt them to claim one.
    let where = "u.is_guest = FALSE AND u.account_state <> 'terminated'";
    if (cursor > 0) {
      params.push(cursor);
      where += ` AND u.id > $${params.length}`;
    }

    const { rows } = await query(
      `SELECT u.id, u.username, u.display_name, u.created_at,
              COALESCE(NULLIF(u.username, ''), u.display_name) AS directory_name
         FROM users u
        WHERE ${where}
        ORDER BY u.id ASC
        LIMIT $1`,
      params,
    );

    const hasMore = rows.length > limit;
    const page = hasMore ? rows.slice(0, limit) : rows;
    const presence = await readPresence(page.map((r) => r.id)).catch(() => null);

    res.json({
      users: page.map((r) => {
        const p = presence ? presence.get(String(r.id)) : null;
        return {
          id: String(r.id),
          username: r.username,
          display_name: r.display_name,
          // What a client should actually render: the handle when there
          // is one, the display name otherwise. Saves every client
          // re-implementing the same fallback slightly differently.
          directory_name: r.directory_name,
          has_username: r.username !== null && r.username !== '',
          status: p?.status || 'offline',
          current_game_id: p?.current_game_id || null,
        };
      }),
      next_cursor: hasMore ? String(page[page.length - 1].id) : null,
      presence_available: presence !== null,
    });
  }),
);

// Aggregate presence counts for the dashboard. Counted from live Redis
// keys only -- a total that includes stale entries is worse than no
// total, because people act on it.
socialRouter.get(
  '/presence/summary',
  requireAuth,
  asyncRoute(async (req, res) => {
    const { rows } = await query(
      `SELECT id FROM users WHERE is_guest = FALSE AND account_state <> 'terminated'`,
    );
    const presence = await readPresence(rows.map((r) => r.id)).catch(() => null);
    if (presence === null) {
      return res.json({ available: false, offline: 0, online_launcher: 0, in_studio: 0, in_game: 0, total_online: 0 });
    }
    const counts = { offline: 0, online_launcher: 0, in_studio: 0, in_game: 0 };
    for (const entry of presence.values()) {
      const status = entry.status || 'offline';
      if (counts[status] === undefined) counts.offline += 1;
      else counts[status] += 1;
    }
    res.json({
      available: true,
      ...counts,
      total_online: counts.online_launcher + counts.in_studio + counts.in_game,
      registered_accounts: rows.length,
    });
  }),
);

// --- friend requests -------------------------------------------------------

socialRouter.post(
  '/friends/request',
  requireAuth,
  rateLimit({ bucket: 'friendreq', limit: 60, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    await assertNotGuest(req.user.id);

    const targetId = String(req.body?.user_id || '');
    if (!/^\d+$/.test(targetId)) throw badRequest('user_id is required.');
    if (targetId === String(req.user.id)) throw badRequest('You cannot add yourself.');

    const target = await query(`SELECT id, is_guest, account_state FROM users WHERE id = $1`, [targetId]);
    if (target.rows.length === 0) throw notFound('No such user.');
    if (target.rows[0].is_guest) throw badRequest('That account cannot receive friend requests.');
    if (target.rows[0].account_state === 'terminated') throw notFound('No such user.');

    const existing = await query(
      `SELECT id, status, requester_id FROM friendships
        WHERE LEAST(requester_id, addressee_id) = LEAST($1::bigint, $2::bigint)
          AND GREATEST(requester_id, addressee_id) = GREATEST($1::bigint, $2::bigint)`,
      [req.user.id, targetId],
    );
    if (existing.rows.length > 0) {
      const row = existing.rows[0];
      // A blocked pair must not be re-openable by the blocked party, and
      // must not reveal that a block is why.
      if (row.status === 'blocked') throw notFound('No such user.');
      if (row.status === 'accepted') throw conflict('You are already friends.');
      if (String(row.requester_id) === String(req.user.id)) throw conflict('A request is already pending.');
      // They already asked us -- treat this as accepting, which is what
      // the user obviously means.
      await query(`UPDATE friendships SET status = 'accepted', updated_at = NOW() WHERE id = $1`, [row.id]);
      return res.json({ status: 'accepted' });
    }

    await query(
      `INSERT INTO friendships (requester_id, addressee_id, status) VALUES ($1, $2, 'pending')`,
      [req.user.id, targetId],
    );
    res.status(201).json({ status: 'pending' });
  }),
);

socialRouter.post(
  '/friends/respond',
  requireAuth,
  asyncRoute(async (req, res) => {
    await assertNotGuest(req.user.id);
    const requesterId = String(req.body?.user_id || '');
    const accept = req.body?.accept === true;
    if (!/^\d+$/.test(requesterId)) throw badRequest('user_id is required.');

    // Only the ADDRESSEE may respond -- the requester accepting their own
    // request would be a one-click way to friend anybody.
    const { rows } = await query(
      `SELECT id FROM friendships
        WHERE requester_id = $1 AND addressee_id = $2 AND status = 'pending'`,
      [requesterId, req.user.id],
    );
    if (rows.length === 0) throw notFound('No pending request from that user.');

    if (accept) {
      await query(`UPDATE friendships SET status = 'accepted', updated_at = NOW() WHERE id = $1`, [rows[0].id]);
      return res.json({ status: 'accepted' });
    }
    await query(`DELETE FROM friendships WHERE id = $1`, [rows[0].id]);
    res.json({ status: 'declined' });
  }),
);

socialRouter.delete(
  '/friends/:userId',
  requireAuth,
  asyncRoute(async (req, res) => {
    const otherId = String(req.params.userId || '');
    if (!/^\d+$/.test(otherId)) throw badRequest('A numeric user id is required.');
    const { rowCount } = await query(
      `DELETE FROM friendships
        WHERE LEAST(requester_id, addressee_id) = LEAST($1::bigint, $2::bigint)
          AND GREATEST(requester_id, addressee_id) = GREATEST($1::bigint, $2::bigint)
          AND status <> 'blocked'`,
      [req.user.id, otherId],
    );
    if (rowCount === 0) throw notFound('You are not friends with that user.');
    res.status(204).end();
  }),
);

// --- follow (one-way, no consent needed) ------------------------------------
//
// Deliberately separate from friendships: following a creator/player
// needs no acceptance step and carries no "request" state -- see
// 004_follows.sql's own comment. POST is idempotent (following someone
// you already follow is just still-following, not an error) so a
// client's Follow button never needs to track local state to avoid a
// double-click error; DELETE is NOT silently a no-op, matching
// DELETE /friends/:userId's own convention -- unfollowing someone you
// don't follow is a real, reportable mistake, not nothing.

socialRouter.post(
  '/follows/:userId',
  requireAuth,
  rateLimit({ bucket: 'follow', limit: 120, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    await assertNotGuest(req.user.id);

    const targetId = String(req.params.userId || '');
    if (!/^\d+$/.test(targetId)) throw badRequest('A numeric user id is required.');
    if (targetId === String(req.user.id)) throw badRequest('You cannot follow yourself.');

    const target = await query(`SELECT id, is_guest, account_state FROM users WHERE id = $1`, [targetId]);
    if (target.rows.length === 0) throw notFound('No such user.');
    if (target.rows[0].is_guest) throw badRequest('That account cannot be followed.');
    if (target.rows[0].account_state === 'terminated') throw notFound('No such user.');

    await query(
      `INSERT INTO follows (follower_id, followee_id) VALUES ($1, $2)
       ON CONFLICT (follower_id, followee_id) DO NOTHING`,
      [req.user.id, targetId],
    );
    res.json({ status: 'following' });
  }),
);

socialRouter.delete(
  '/follows/:userId',
  requireAuth,
  asyncRoute(async (req, res) => {
    const targetId = String(req.params.userId || '');
    if (!/^\d+$/.test(targetId)) throw badRequest('A numeric user id is required.');

    const { rowCount } = await query(
      `DELETE FROM follows WHERE follower_id = $1 AND followee_id = $2`,
      [req.user.id, targetId],
    );
    if (rowCount === 0) throw notFound('You are not following that user.');
    res.status(204).end();
  }),
);

// Shared shape for both list directions below -- only which id column is
// fixed and which is walked differs.
async function listFollowEdge({ fixedColumn, walkedColumn, fixedId, limit, cursor, viewerId }) {
  const params = [fixedId, limit + 1];
  let where = `f.${fixedColumn} = $1 AND other.account_state <> 'terminated'`;
  if (cursor > 0) {
    params.push(cursor);
    where += ` AND other.id > $${params.length}`;
  }
  const { rows } = await query(
    `SELECT other.id, other.username, other.display_name,
            COALESCE(NULLIF(other.username, ''), other.display_name) AS directory_name
       FROM follows f
       JOIN users other ON other.id = f.${walkedColumn}
      WHERE ${where}
      ORDER BY other.id ASC
      LIMIT $2`,
    params,
  );
  const hasMore = rows.length > limit;
  const page = hasMore ? rows.slice(0, limit) : rows;
  const presence = await readPresence(page.map((r) => r.id)).catch(() => null);

  // Only meaningful when a real viewer is asking (not every call site
  // has one) -- lets a profile page render "Following" vs "Follow"
  // against each row without an extra round trip per row.
  let viewerFollows = null;
  if (viewerId && page.length > 0) {
    const { rows: edges } = await query(
      `SELECT followee_id FROM follows WHERE follower_id = $1 AND followee_id = ANY($2::bigint[])`,
      [viewerId, page.map((r) => r.id)],
    );
    viewerFollows = new Set(edges.map((e) => String(e.followee_id)));
  }

  return {
    users: page.map((r) => {
      const p = presence ? presence.get(String(r.id)) : null;
      return {
        id: String(r.id),
        username: r.username,
        display_name: r.display_name,
        directory_name: r.directory_name,
        status: p?.status || 'offline',
        current_game_id: p?.current_game_id || null,
        ...(viewerFollows ? { viewer_is_following: viewerFollows.has(String(r.id)) } : {}),
      };
    }),
    next_cursor: hasMore ? String(page[page.length - 1].id) : null,
    presence_available: presence !== null,
  };
}

socialRouter.get(
  '/follows/:userId/followers',
  requireAuth,
  asyncRoute(async (req, res) => {
    const userId = String(req.params.userId || '');
    if (!/^\d+$/.test(userId)) throw badRequest('A numeric user id is required.');
    const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 200);
    const cursor = Number(req.query.cursor) || 0;
    const result = await listFollowEdge({
      fixedColumn: 'followee_id', walkedColumn: 'follower_id',
      fixedId: userId, limit, cursor, viewerId: req.user.id,
    });
    res.json({ followers: result.users, next_cursor: result.next_cursor, presence_available: result.presence_available });
  }),
);

socialRouter.get(
  '/follows/:userId/following',
  requireAuth,
  asyncRoute(async (req, res) => {
    const userId = String(req.params.userId || '');
    if (!/^\d+$/.test(userId)) throw badRequest('A numeric user id is required.');
    const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 200);
    const cursor = Number(req.query.cursor) || 0;
    const result = await listFollowEdge({
      fixedColumn: 'follower_id', walkedColumn: 'followee_id',
      fixedId: userId, limit, cursor, viewerId: req.user.id,
    });
    res.json({ following: result.users, next_cursor: result.next_cursor, presence_available: result.presence_available });
  }),
);

socialRouter.get(
  '/follows/:userId/counts',
  requireAuth,
  asyncRoute(async (req, res) => {
    const userId = String(req.params.userId || '');
    if (!/^\d+$/.test(userId)) throw badRequest('A numeric user id is required.');
    const { rows } = await query(
      `SELECT
         (SELECT COUNT(*) FROM follows WHERE followee_id = $1) AS followers,
         (SELECT COUNT(*) FROM follows WHERE follower_id = $1) AS following`,
      [userId],
    );
    res.json({ followers: Number(rows[0].followers), following: Number(rows[0].following) });
  }),
);

// --- friends list ----------------------------------------------------------

socialRouter.get(
  '/friends/list',
  requireAuth,
  asyncRoute(async (req, res) => {
    const { rows } = await query(
      `SELECT f.status, f.requester_id, f.addressee_id,
              other.id AS other_id, other.username, other.display_name
         FROM friendships f
         JOIN users other
           ON other.id = CASE WHEN f.requester_id = $1::bigint THEN f.addressee_id ELSE f.requester_id END
        WHERE (f.requester_id = $1::bigint OR f.addressee_id = $1::bigint)
          AND f.status IN ('pending', 'accepted')
          AND other.account_state <> 'terminated'
        ORDER BY other.username_lower`,
      [req.user.id],
    );

    const accepted = rows.filter((r) => r.status === 'accepted');
    const presence = await readPresence(accepted.map((r) => r.other_id)).catch(() => null);

    // Direct-join tickets are minted ONLY for friends actually in a game,
    // and only for the specific server they are on. Issuing one for an
    // offline friend would be minting an entry pass to nothing.
    const friends = accepted.map((r) => {
      const p = presence ? presence.get(String(r.other_id)) : null;
      const status = p?.status || 'offline';
      const entry = {
        id: String(r.other_id),
        username: r.username,
        display_name: r.display_name,
        status,
        current_game_id: p?.current_game_id || null,
        current_server_id: p?.current_server_id || null,
        join_ticket: null,
      };
      if (status === 'in_game' && p?.current_server_id) {
        entry.join_ticket = issueJoinTicket({
          userId: req.user.id,
          gameId: p.current_game_id || '0',
          serverKey: p.current_server_id,
        });
        entry.join_ticket_expires_in = config.joinTicketTtlSeconds;
      }
      return entry;
    });

    res.json({
      friends,
      // Surfaced separately so the launcher can show a request badge.
      incoming_requests: rows
        .filter((r) => r.status === 'pending' && String(r.addressee_id) === String(req.user.id))
        .map((r) => ({ id: String(r.other_id), username: r.username, display_name: r.display_name })),
      outgoing_requests: rows
        .filter((r) => r.status === 'pending' && String(r.requester_id) === String(req.user.id))
        .map((r) => ({ id: String(r.other_id), username: r.username, display_name: r.display_name })),
      // Honest: when Redis is unreachable every friend reads 'offline',
      // and the client should say "presence unavailable" rather than
      // confidently showing everyone as offline.
      presence_available: presence !== null,
    });
  }),
);

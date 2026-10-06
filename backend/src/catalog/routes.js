import express from 'express';

import { config } from '../config.js';
import { query } from '../db.js';
import { asyncRoute, badRequest, conflict, forbidden, notFound } from '../errors.js';
import { redis, keys } from '../redis.js';
import { optionalAuth, requireAuth } from '../middleware/auth.js';
import { rateLimit } from '../middleware/rateLimit.js';
import { mediaUrl } from './releases.js';

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
    // Max 200 per batch: large enough for an infinite-scroll grid to
    // stay ahead of the user, small enough that one request cannot pull
    // the whole catalogue in a single round trip.
    const limit = Math.min(Math.max(Number(req.query.limit) || 24, 1), 200);
    const cursor = Number(req.query.cursor) || 0;
    const search = (req.query.q || '').toString().trim();

    // First, cheap defense-in-depth layer for age-gating -- the real
    // enforcement is at the allocate/matchmaking-ticket entry points
    // (sessions/routes.js, matchmaking/routes.js), which a client
    // already holding a slug could hit directly regardless of what this
    // listing shows. This just keeps a mature game from being
    // advertised by title/thumbnail to a browsing caller who isn't
    // verified in the first place.
    let isAdultVerified = false;
    if (req.user) {
      const { rows } = await query(`SELECT age_verified FROM users WHERE id = $1`, [req.user.id]);
      isAdultVerified = rows.length > 0 && rows[0].age_verified === true;
    }

    // Keyset pagination on id, not OFFSET: stable under concurrent
    // publishes and does not degrade on deep pages.
    const params = [limit + 1];
    let where = "g.published = TRUE AND g.review_status = 'approved'";
    if (!isAdultVerified) where += ' AND g.mature = FALSE';
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
      `SELECT g.id, g.slug, g.title, g.description, g.thumbnail_url, g.created_at, g.mature,
              u.display_name AS creator_name, u.id AS creator_id
         FROM games g JOIN users u ON u.id = g.creator_id
        WHERE g.slug = $1 AND g.published = TRUE AND g.review_status = 'approved'`,
      [req.params.slug],
    );
    if (rows.length === 0) throw notFound('No such published game.');
    const g = rows[0];

    if (g.mature) {
      // Same real live-lookup as the /games list above. Reported as a
      // plain 404 rather than 403: a caller who isn't verified gets no
      // signal that a restricted game exists at this slug at all, same
      // as an unpublished one.
      let isAdultVerified = false;
      if (req.user) {
        const verified = await query(`SELECT age_verified FROM users WHERE id = $1`, [req.user.id]);
        isAdultVerified = verified.rows.length > 0 && verified.rows[0].age_verified === true;
      }
      if (!isAdultVerified) throw notFound('No such published game.');
    }

    const [counts, { rows: screenshots }] = await Promise.all([
      livePlayerCounts([g.id]),
      query(
        `SELECT id, width, height FROM game_media
          WHERE game_id = $1 AND kind = 'screenshot' AND review_status = 'approved' ORDER BY position, id`,
        [g.id],
      ),
    ]);
    res.json({
      game: {
        id: String(g.id),
        slug: g.slug,
        title: g.title,
        description: g.description,
        thumbnail_url: g.thumbnail_url,
        mature: g.mature,
        creator: { id: String(g.creator_id), display_name: g.creator_name },
        active_players: counts ? counts.get(String(g.id)) : null,
        screenshots: screenshots.map((m) => ({ id: String(m.id), url: mediaUrl(m.id), width: m.width, height: m.height })),
      },
      player_counts_available: counts !== null,
    });
  }),
);

// requireOwnedGame lives in ./ownership.js now, shared with assets.js's
// chunk/version/lock/dialogue routes below.

// Kronos ("One-Click Cloud Publishing"): registers a place authored in
// Studio into the public catalogue.
//
// The .kronos scene body itself is deliberately NOT stored in Postgres.
// A place file is an opaque blob that will only grow, and a database is
// the wrong home for one; this stores the metadata the catalogue needs
// and a content hash, leaving the blob to object storage. Storing a
// multi-megabyte scene in a JSONB column is the kind of decision that is
// very hard to walk back once there are real places in it.
catalogRouter.post(
  '/games/publish',
  requireAuth,
  rateLimit({ bucket: 'publish', limit: 30, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    const slug = String(req.body?.slug || '').trim().toLowerCase();
    const title = String(req.body?.title || '').trim();
    const description = String(req.body?.description || '').trim();
    const thumbnailUrl = String(req.body?.thumbnail_url || '').trim();
    const sceneHash = String(req.body?.scene_sha256 || '').trim();
    // Self-reported, same trust level as title/description -- see
    // games.mature's own migration comment. An ops override of a
    // creator's own claim (e.g. flagging an under-labeled game) is a
    // moderation_actions-audited admin decision, not exposed here.
    const mature = req.body?.mature === true;

    if (!/^[a-z0-9][a-z0-9-]{2,63}$/.test(slug)) {
      throw badRequest('Slug must be 3-64 characters: lowercase letters, numbers and hyphens.');
    }
    if (title.length < 1 || title.length > 100) throw badRequest('Title must be 1-100 characters.');
    if (description.length > 2000) throw badRequest('Description must be at most 2000 characters.');
    if (sceneHash && !/^[a-f0-9]{64}$/.test(sceneHash)) {
      throw badRequest('scene_sha256 must be a hex SHA-256 digest.');
    }
    // Only http(s) thumbnails: a javascript:/data: URL here would be
    // rendered by every client that shows the catalogue.
    if (thumbnailUrl && !/^https?:\/\//i.test(thumbnailUrl)) {
      throw badRequest('thumbnail_url must be an http(s) URL.');
    }
    // Guests cannot publish -- enforced server-side, like every other
    // guest restriction.
    const { rows: authors } = await query(
      `SELECT is_guest, account_state FROM users WHERE id = $1`, [req.user.id]);
    if (authors.length === 0) throw notFound('Account no longer exists.');
    if (authors[0].is_guest) throw forbidden('Guest accounts cannot publish games. Create a free account first.');
    if (authors[0].account_state !== 'active') throw forbidden('This account cannot publish right now.');

    // Re-publishing your own slug updates it in place; somebody else's is
    // refused. The WHERE clause on creator_id is what makes that a real
    // ownership check rather than a hope.
    const existing = await query(`SELECT id, creator_id FROM games WHERE slug = $1`, [slug]);
    if (existing.rows.length > 0) {
      if (String(existing.rows[0].creator_id) !== String(req.user.id)) {
        throw conflict('That slug is already taken.');
      }
      // scene_sha256 names an already-confirmed, approved version to make live;
      // packages themselves only arrive through package/confirm.
      if (sceneHash) {
        const { rows: versions } = await query(
          `SELECT id, review_status FROM game_package_versions WHERE game_id = $1 AND sha256 = $2`,
          [existing.rows[0].id, sceneHash],
        );
        if (versions.length === 0) throw badRequest('scene_sha256 does not match any uploaded package -- upload it first.');
        if (versions[0].review_status !== 'approved') throw badRequest('That package version has not been approved yet.');
        await query(
          `UPDATE games g SET current_package_version_id = v.id, scene_sha256 = v.sha256, package_object_key = v.object_key,
                  package_size_bytes = v.size_bytes, package_uploaded_at = v.created_at
             FROM game_package_versions v WHERE v.id = $2 AND g.id = $1`,
          [existing.rows[0].id, versions[0].id],
        );
      }
      // Catalog thumbnails are uploaded images now; a URL is only kept when the
      // game has none, for older clients.
      const updated = await query(
        `UPDATE games
            SET title = $2, description = $3,
                thumbnail_url = CASE WHEN thumbnail_url LIKE $6 THEN thumbnail_url ELSE $4 END,
                published = TRUE, updated_at = NOW(), mature = $7
          WHERE id = $1 AND creator_id = $5
          RETURNING id, slug, review_status`,
        [existing.rows[0].id, title, description, thumbnailUrl, req.user.id, `${mediaUrl('')}%`, mature],
      );
      return res.json({
        status: 'updated',
        game: { id: String(updated.rows[0].id), slug: updated.rows[0].slug, review_status: updated.rows[0].review_status },
      });
    }
    if (sceneHash) throw badRequest('scene_sha256 does not match any uploaded package -- upload it first.');

    const reviewStatus = config.gameReviewRequired ? 'pending' : 'approved';
    const inserted = await query(
      `INSERT INTO games (slug, title, description, creator_id, thumbnail_url, published, mature, review_status)
       VALUES ($1, $2, $3, $4, $5, TRUE, $6, $7)
       RETURNING id, slug, review_status`,
      [slug, title, description, req.user.id, thumbnailUrl, mature, reviewStatus],
    );
    res.status(201).json({
      status: 'published',
      game: { id: String(inserted.rows[0].id), slug: inserted.rows[0].slug, review_status: inserted.rows[0].review_status },
    });
  }),
);

// Package upload, versions, review and catalog images live in ./releases.js.

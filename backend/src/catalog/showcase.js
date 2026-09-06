// Showcase portal: community-submitted creations (games, 3D materials,
// audio drops, movie clips), each carrying a kronos:// deep link so a
// browser click launches straight into the real thing.
//
// A submission is NOT instantly public. featured_at is the real gate on
// visibility (see db/migrations/009_showcase_portal.sql's own comment):
// POST /submit only ever creates a pending row (featured_at NULL), and
// GET /v1/showcase only ever returns rows an admin has actually featured
// -- "submit ... for feature consideration" means consideration, not
// automatic publication.
import express from 'express';

import { query } from '../db.js';
import { asyncRoute, badRequest, notFound } from '../errors.js';
import { optionalAuth, requireAdmin, requireAuth } from '../middleware/auth.js';
import { rateLimit } from '../middleware/rateLimit.js';
import { requireOwnedGame } from './ownership.js';

export const showcaseRouter = express.Router();

const CATEGORIES = ['games', '3d_materials', 'audio_dps', 'movie_clips'];

function formatEntry(row) {
  return {
    id: String(row.id),
    title: row.title,
    description: row.description,
    category: row.category,
    thumbnail_url: row.thumbnail_url,
    kronos_uri: row.kronos_uri,
    likes_count: Number(row.likes_count),
    featured_at: row.featured_at,
    game: { id: String(row.game_id), slug: row.game_slug, title: row.game_title },
    author: { id: String(row.creator_id), display_name: row.creator_name },
  };
}

showcaseRouter.get(
  '/',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const category = req.query.category ? String(req.query.category) : null;
    if (category && !CATEGORIES.includes(category)) {
      throw badRequest(`category must be one of: ${CATEGORIES.join(', ')}.`);
    }
    // Same keyset-on-id convention catalog/routes.js's own GET /games
    // uses -- stable under concurrent submissions, no OFFSET degradation.
    const limit = Math.min(Math.max(Number(req.query.limit) || 24, 1), 200);
    const cursor = Number(req.query.cursor) || 0;

    const params = [limit + 1];
    let where = 'se.featured_at IS NOT NULL';
    if (category) {
      params.push(category);
      where += ` AND se.category = $${params.length}`;
    }
    if (cursor > 0) {
      params.push(cursor);
      where += ` AND se.id < $${params.length}`;
    }

    const { rows } = await query(
      `SELECT se.id, se.title, se.description, se.category, se.thumbnail_url, se.kronos_uri,
              se.likes_count, se.featured_at, se.game_id,
              g.slug AS game_slug, g.title AS game_title, u.id AS creator_id, u.display_name AS creator_name
         FROM showcase_entries se
         JOIN games g ON g.id = se.game_id
         JOIN users u ON u.id = g.creator_id
        WHERE ${where}
        ORDER BY se.id DESC
        LIMIT $1`,
      params,
    );

    const hasMore = rows.length > limit;
    const page = hasMore ? rows.slice(0, limit) : rows;
    res.json({
      entries: page.map(formatEntry),
      next_cursor: hasMore ? String(page[page.length - 1].id) : null,
    });
  }),
);

showcaseRouter.post(
  '/submit',
  requireAuth,
  rateLimit({ bucket: 'showcasesubmit', limit: 20, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    const slug = String(req.body?.slug || '').trim().toLowerCase();
    if (!slug) throw badRequest('slug is required.');
    const category = String(req.body?.category || '').trim();
    if (!CATEGORIES.includes(category)) throw badRequest(`category must be one of: ${CATEGORIES.join(', ')}.`);
    const title = String(req.body?.title || '').trim();
    if (title.length < 1 || title.length > 200) throw badRequest('title must be 1-200 characters.');
    const description = String(req.body?.description || '').trim().slice(0, 2000);
    const thumbnailUrl = String(req.body?.thumbnail_url || '').trim();
    if (thumbnailUrl && !/^https?:\/\//i.test(thumbnailUrl)) throw badRequest('thumbnail_url must be an http(s) URL.');

    // Only the real creator can submit their own work -- reuses the
    // exact same ownership check package/chunk/dialogue routes already
    // use, so "submit your own published asset or game" is enforced
    // server-side, not just implied by the client's own UI.
    const game = await requireOwnedGame(slug, req.user.id);

    let assetVersionId = null;
    if (req.body?.asset_version_id != null) {
      assetVersionId = Number(req.body.asset_version_id);
      const { rows } = await query(`SELECT id FROM asset_versions WHERE id = $1 AND game_id = $2`, [assetVersionId, game.id]);
      if (rows.length === 0) throw badRequest('asset_version_id does not reference a real version of this game.');
    }

    // Server-constructed, matching the real, documented launch URI
    // format (see engine/src/core/KronosLaunchUri.cpp) -- never taken
    // from the client, which has no business claiming what its own
    // deep link is. Built from the already-validated `slug` (the same
    // one requireOwnedGame just checked ownership against), NOT
    // `game.slug` -- requireOwnedGame's own query (catalog/ownership.js)
    // only selects id/creator_id, so `game.slug` is always undefined.
    const kronosUri = `kronos://launch?game=${encodeURIComponent(slug)}&ref=showcase`;

    const { rows: inserted } = await query(
      `INSERT INTO showcase_entries (title, description, category, game_id, asset_version_id, thumbnail_url, kronos_uri, submitted_by)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8)
       RETURNING id`,
      [title, description, category, game.id, assetVersionId, thumbnailUrl, kronosUri, req.user.id],
    );
    res.status(201).json({ status: 'submitted', id: String(inserted[0].id), featured: false });
  }),
);

// Minimal moderation hook: without this, a submission could never
// actually appear (featured_at would stay NULL forever) -- same "real,
// working plumbing end to end" reasoning the chunk-confirm and
// installer-manifest routes already needed to be more than a dead end.
showcaseRouter.post(
  '/:id/feature',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const id = Number(req.params.id);
    if (!Number.isFinite(id)) throw badRequest('Invalid showcase entry id.');
    const { rows } = await query(
      `UPDATE showcase_entries SET featured_at = COALESCE(featured_at, NOW()), updated_at = NOW()
        WHERE id = $1 RETURNING featured_at`,
      [id],
    );
    if (rows.length === 0) throw notFound('No such showcase entry.');
    res.json({ status: 'featured', featured_at: rows[0].featured_at });
  }),
);

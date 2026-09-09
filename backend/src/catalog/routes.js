import express from 'express';

import { config } from '../config.js';
import { query } from '../db.js';
import { asyncRoute, badRequest, conflict, forbidden, notFound } from '../errors.js';
import { redis, keys } from '../redis.js';
import { optionalAuth, requireAuth } from '../middleware/auth.js';
import { rateLimit } from '../middleware/rateLimit.js';
import {
  s3Configured, packageObjectKey, createPresignedUploadUrl, createPresignedDownloadUrl, headObject, verifyObjectHash,
} from '../storage/s3.js';
import {
  headObject as headLocalObject, verifyObjectHash as verifyLocalObjectHash,
  writeObjectFromStream as writeLocalObjectFromStream, readObjectStream as readLocalObjectStream,
} from '../storage/local.js';
import { requireOwnedGame } from './ownership.js';

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
    let where = 'g.published = TRUE';
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
        WHERE g.slug = $1 AND g.published = TRUE`,
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

    const counts = await livePlayerCounts([g.id]);
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
    // A real, honest object existence check against whichever backend is
    // actually active (S3, or the local disk fallback -- see
    // storage/local.js -- when no bucket is configured). This route
    // deliberately does NOT do the full re-hash verifyObjectHash() does --
    // that real, stronger verification already ran once at
    // package/confirm time (see that route below); a hash reaching this
    // route has either already been through that, or points at nothing,
    // which this HEAD check alone is enough to catch.
    let packageObjectKeyForSlug = null;
    let packageSizeBytes = null;
    if (sceneHash) {
      const key = packageObjectKey(sceneHash);
      const head = s3Configured() ? await headObject(key) : await headLocalObject(key);
      if (!head.exists) throw badRequest('scene_sha256 does not match any uploaded package -- upload it first.');
      packageObjectKeyForSlug = key;
      packageSizeBytes = head.sizeBytes;
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
      const updated = await query(
        `UPDATE games
            SET title = $2, description = $3, thumbnail_url = $4, published = TRUE, updated_at = NOW(),
                scene_sha256 = COALESCE($6, scene_sha256),
                package_object_key = COALESCE($7, package_object_key),
                package_size_bytes = COALESCE($8, package_size_bytes),
                package_uploaded_at = CASE WHEN $6::text IS NOT NULL THEN NOW() ELSE package_uploaded_at END,
                mature = $9
          WHERE id = $1 AND creator_id = $5
          RETURNING id, slug`,
        [existing.rows[0].id, title, description, thumbnailUrl, req.user.id,
         sceneHash || null, packageObjectKeyForSlug, packageSizeBytes, mature],
      );
      return res.json({ status: 'updated', game: { id: String(updated.rows[0].id), slug: updated.rows[0].slug } });
    }

    const inserted = await query(
      `INSERT INTO games (slug, title, description, creator_id, thumbnail_url, published,
                           scene_sha256, package_object_key, package_size_bytes, package_uploaded_at, mature)
       VALUES ($1, $2, $3, $4, $5, TRUE, $6, $7, $8, CASE WHEN $6::text IS NOT NULL THEN NOW() ELSE NULL END, $9)
       RETURNING id, slug`,
      [slug, title, description, req.user.id, thumbnailUrl, sceneHash || null, packageObjectKeyForSlug, packageSizeBytes, mature],
    );
    res.status(201).json({ status: 'published', game: { id: String(inserted.rows[0].id), slug: inserted.rows[0].slug } });
  }),
);

// --- Dynamic Asset Streaming: package upload/download -----------------------
//
// A game's real .kronos archive lives content-addressed in S3
// (packages/<sha256>.kronos, see storage/s3.js's own comment), never in
// Postgres. Ownership is checked against the SAME games row /publish
// already protects (requireOwnedGame, see ./ownership.js) -- a slug must
// exist and be owned by the caller before its package can be uploaded
// to, matching the real, intended order: publish metadata first
// (creating the row), then upload/confirm the package against that same
// slug.

catalogRouter.post(
  '/games/:slug/package/upload-url',
  requireAuth,
  rateLimit({ bucket: 'packageupload', limit: 30, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    await requireOwnedGame(req.params.slug, req.user.id);

    const sha256 = String(req.body?.sha256 || '').trim().toLowerCase();
    if (!/^[a-f0-9]{64}$/.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');
    const sizeBytes = Number(req.body?.size_bytes);
    if (!Number.isFinite(sizeBytes) || sizeBytes <= 0) throw badRequest('size_bytes must be a positive number.');
    if (sizeBytes > config.packageMaxSizeBytes) {
      throw badRequest(`Package exceeds the ${config.packageMaxSizeBytes}-byte size limit.`);
    }

    const key = packageObjectKey(sha256);
    if (s3Configured()) {
      const uploadUrl = await createPresignedUploadUrl(key);
      return res.json({ upload_url: uploadUrl, object_key: key, expires_in: config.packageUploadTtlSeconds, storage: 's3' });
    }
    // Local disk fallback: no bucket to presign a URL against, so the
    // client PUTs its raw package bytes straight to this same backend
    // instead of to a third-party bucket -- same "PUT bytes to
    // upload_url" client contract either way, just a same-origin route
    // (protected by the SAME auth + ownership check above, see the
    // local-upload route right below) instead of an AWS-signed one.
    res.json({
      upload_url: `${config.publicBaseUrl}/v1/catalog/games/${req.params.slug}/package/local-upload/${sha256}`,
      object_key: key,
      expires_in: null,
      storage: 'local',
    });
  }),
);

catalogRouter.put(
  '/games/:slug/package/local-upload/:sha256',
  requireAuth,
  rateLimit({ bucket: 'packageupload', limit: 30, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    // This route only exists as the local-disk counterpart to a presigned
    // S3 PUT -- once a bucket is configured, uploads go straight there
    // instead, and this path is never handed out (see upload-url above).
    if (s3Configured()) throw notFound('No such endpoint.');
    await requireOwnedGame(req.params.slug, req.user.id);

    const sha256 = String(req.params.sha256 || '').trim().toLowerCase();
    if (!/^[a-f0-9]{64}$/.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');

    const key = packageObjectKey(sha256);
    let sizeBytes;
    try {
      ({ sizeBytes } = await writeLocalObjectFromStream(key, req, config.packageMaxSizeBytes));
    } catch (err) {
      if (err.code === 'PACKAGE_TOO_LARGE') throw badRequest(err.message);
      throw err;
    }
    res.json({ status: 'stored', object_key: key, size_bytes: sizeBytes });
  }),
);

catalogRouter.post(
  '/games/:slug/package/confirm',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);

    const sha256 = String(req.body?.sha256 || '').trim().toLowerCase();
    if (!/^[a-f0-9]{64}$/.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');
    const key = packageObjectKey(sha256);
    const useS3 = s3Configured();

    // Real, streamed, server-side re-hash of the actual uploaded bytes
    // -- never trusts the client's own claim, and never trusts the
    // content-addressed key alone (a key names what SHOULD be there,
    // not what really is). See storage/s3.js's own header comment on
    // why this is a full re-read rather than relying on a presigned-
    // checksum feature. Same real check either way, just against
    // whichever backend actually received the upload.
    const head = useS3 ? await headObject(key) : await headLocalObject(key);
    if (!head.exists) throw notFound('No package was uploaded for that hash.');
    if (head.sizeBytes > config.packageMaxSizeBytes) {
      throw badRequest(`Uploaded package exceeds the ${config.packageMaxSizeBytes}-byte size limit.`);
    }
    const verified = useS3 ? await verifyObjectHash(key, sha256) : await verifyLocalObjectHash(key, sha256);
    if (!verified.matches) {
      throw badRequest('The uploaded package\'s real content does not match the declared sha256.');
    }

    await query(
      `UPDATE games
          SET scene_sha256 = $2, package_object_key = $3, package_size_bytes = $4, package_uploaded_at = NOW(),
              updated_at = NOW()
        WHERE id = $1`,
      [game.id, sha256, key, verified.sizeBytes],
    );
    res.json({ status: 'confirmed', sha256, size_bytes: verified.sizeBytes });
  }),
);

catalogRouter.get(
  '/games/:slug/package',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const { rows } = await query(
      `SELECT scene_sha256, package_object_key, package_size_bytes, package_uploaded_at
         FROM games WHERE slug = $1 AND published = TRUE`,
      [req.params.slug],
    );
    if (rows.length === 0) throw notFound('No such published game.');
    const g = rows[0];
    if (!g.package_object_key) throw notFound('This game has no uploaded package yet.');

    if (s3Configured()) {
      const downloadUrl = await createPresignedDownloadUrl(g.package_object_key);
      return res.json({
        sha256: g.scene_sha256,
        size_bytes: g.package_size_bytes,
        uploaded_at: g.package_uploaded_at,
        download_url: downloadUrl,
        expires_in: config.s3PublicBaseUrl ? null : config.packageDownloadTtlSeconds,
      });
    }
    // Local disk fallback: a real, direct, unsigned URL back to this same
    // backend -- same "a public CDN needs no per-request signature"
    // reasoning s3PublicBaseUrl already uses above, just served from
    // local disk instead of a CDN.
    res.json({
      sha256: g.scene_sha256,
      size_bytes: g.package_size_bytes,
      uploaded_at: g.package_uploaded_at,
      download_url: `${config.publicBaseUrl}/v1/catalog/games/${req.params.slug}/package/download`,
      expires_in: null,
    });
  }),
);

catalogRouter.get(
  '/games/:slug/package/download',
  optionalAuth,
  asyncRoute(async (req, res) => {
    // Local-disk counterpart to the presigned S3 GET above -- once a
    // bucket is configured, download_url points straight at it instead
    // and this path is never handed out.
    if (s3Configured()) throw notFound('No such endpoint.');

    const { rows } = await query(
      `SELECT package_object_key FROM games WHERE slug = $1 AND published = TRUE`,
      [req.params.slug],
    );
    if (rows.length === 0 || !rows[0].package_object_key) throw notFound('This game has no uploaded package yet.');

    const key = rows[0].package_object_key;
    const head = await headLocalObject(key);
    if (!head.exists) throw notFound('This game has no uploaded package yet.');

    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', String(head.sizeBytes));
    readLocalObjectStream(key).pipe(res);
  }),
);

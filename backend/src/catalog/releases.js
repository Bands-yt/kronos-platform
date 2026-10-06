// Game packages as numbered, reviewable, roll-back-able versions, plus the
// thumbnail and screenshots shown in the catalog, all counted against the
// creator's storage quota.
import crypto from 'node:crypto';

import express from 'express';

import { config } from '../config.js';
import { query, withTransaction } from '../db.js';
import { asyncRoute, badRequest, conflict, notFound, quotaExceeded } from '../errors.js';
import { optionalAuth, requireAdmin, requireAuth } from '../middleware/auth.js';
import { rateLimit } from '../middleware/rateLimit.js';
import { mediaObjectKey, packageObjectKey } from '../storage/objectKey.js';
import * as store from './objectStore.js';
import { inspectPackageStream, PackageError, validateGamePackage } from './packageArchive.js';
import { requireOwnedGame } from './ownership.js';
import { imageInfo } from './imageInfo.js';

export const releasesRouter = express.Router();
export const catalogReviewRouter = express.Router();

const SHA256_RE = /^[a-f0-9]{64}$/;
const MEDIA_KINDS = new Set(['thumbnail', 'screenshot']);
const MEDIA_TYPES = new Set(['image/png', 'image/jpeg']);

const initialReviewStatus = () => (config.gameReviewRequired ? 'pending' : 'approved');

function parseSha(raw) {
  const sha256 = String(raw || '').trim().toLowerCase();
  if (!SHA256_RE.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');
  return sha256;
}

function parseVersionNumber(raw) {
  const n = Number(raw);
  if (!Number.isInteger(n) || n < 1) throw badRequest('Invalid version number.');
  return n;
}

function formatBytes(bytes) {
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let value = bytes;
  let unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit += 1;
  }
  return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

// --- storage quota -----------------------------------------------------------

export async function storageQuotaBytes(userId) {
  const { rows } = await query(`SELECT storage_quota_bytes FROM users WHERE id = $1`, [userId]);
  const override = rows[0]?.storage_quota_bytes;
  return override != null ? Number(override) : config.packageQuotaBytes;
}

// Objects are content-addressed, so the same bytes kept in two versions or
// two games are stored, and counted, once.
export async function storageUsageBytes(userId) {
  const { rows } = await query(
    `SELECT COALESCE(SUM(size_bytes), 0)::bigint AS used FROM (
       SELECT v.sha256, v.size_bytes FROM game_package_versions v JOIN games g ON g.id = v.game_id WHERE g.creator_id = $1
       UNION
       SELECT m.sha256, m.size_bytes FROM game_media m JOIN games g ON g.id = m.game_id WHERE g.creator_id = $1
     ) owned`,
    [userId],
  );
  return Number(rows[0].used);
}

async function accountHoldsSha(userId, sha256) {
  const { rows } = await query(
    `SELECT 1 FROM game_package_versions v JOIN games g ON g.id = v.game_id WHERE g.creator_id = $1 AND v.sha256 = $2
     UNION ALL
     SELECT 1 FROM game_media m JOIN games g ON g.id = m.game_id WHERE g.creator_id = $1 AND m.sha256 = $2
     LIMIT 1`,
    [userId, sha256],
  );
  return rows.length > 0;
}

async function assertQuota(userId, sha256, sizeBytes) {
  if (await accountHoldsSha(userId, sha256)) return;
  const [quota, used] = await Promise.all([storageQuotaBytes(userId), storageUsageBytes(userId)]);
  if (used + sizeBytes > quota) {
    throw quotaExceeded(
      `This upload needs ${formatBytes(sizeBytes)}, but only ${formatBytes(Math.max(0, quota - used))} of your ` +
      `${formatBytes(quota)} storage is free. Delete old versions or screenshots to make room.`,
    );
  }
}

async function storageSummary(userId) {
  const [quota, used] = await Promise.all([storageQuotaBytes(userId), storageUsageBytes(userId)]);
  return { used_bytes: used, quota_bytes: quota };
}

// Deletes the stored object once no version or image anywhere still uses it.
async function removeObjectIfUnreferenced(objectKey) {
  const { rows } = await query(
    `SELECT 1 FROM game_package_versions WHERE object_key = $1
     UNION ALL SELECT 1 FROM game_media WHERE object_key = $1 LIMIT 1`,
    [objectKey],
  );
  if (rows.length === 0) await store.remove(objectKey).catch((err) => console.error('[releases] delete %s: %s', objectKey, err.message));
}

// --- package versions ----------------------------------------------------------

function versionJson(v, currentId) {
  return {
    id: String(v.id),
    version_number: v.version_number,
    sha256: v.sha256,
    size_bytes: Number(v.size_bytes),
    review_status: v.review_status,
    review_note: v.review_note,
    created_at: v.created_at,
    reviewed_at: v.reviewed_at,
    current: currentId != null && String(currentId) === String(v.id),
    manifest: v.manifest,
  };
}

// The legacy package columns on games mirror the live version so older
// clients and queries keep working.
async function makeVersionLive(client, gameId, version) {
  await client.query(
    `UPDATE games
        SET current_package_version_id = $2, scene_sha256 = $3, package_object_key = $4, package_size_bytes = $5,
            package_uploaded_at = $6, review_status = 'approved', updated_at = NOW()
      WHERE id = $1`,
    [gameId, version.id, version.sha256, version.object_key, version.size_bytes, version.created_at],
  );
}

releasesRouter.post(
  '/games/:slug/package/upload-url',
  requireAuth,
  rateLimit({ bucket: 'packageupload', limit: 30, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    await requireOwnedGame(req.params.slug, req.user.id);
    const sha256 = parseSha(req.body?.sha256);
    const sizeBytes = Number(req.body?.size_bytes);
    if (!Number.isFinite(sizeBytes) || sizeBytes <= 0) throw badRequest('size_bytes must be a positive number.');
    if (sizeBytes > config.packageMaxSizeBytes) {
      throw badRequest(`Package exceeds the ${config.packageMaxSizeBytes}-byte size limit.`);
    }
    await assertQuota(req.user.id, sha256, sizeBytes);

    const key = packageObjectKey(sha256);
    const target = await store.uploadTarget(key, `/v1/catalog/games/${req.params.slug}/package/local-upload/${sha256}`);
    res.json({ ...target, object_key: key });
  }),
);

releasesRouter.put(
  '/games/:slug/package/local-upload/:sha256',
  requireAuth,
  rateLimit({ bucket: 'packageupload', limit: 30, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    if (store.usingS3()) throw notFound('No such endpoint.');
    await requireOwnedGame(req.params.slug, req.user.id);
    const key = packageObjectKey(parseSha(req.params.sha256));
    let sizeBytes;
    try {
      ({ sizeBytes } = await store.storeLocalUpload(key, req, config.packageMaxSizeBytes));
    } catch (err) {
      if (err.code === 'PACKAGE_TOO_LARGE') throw badRequest(err.message);
      throw err;
    }
    res.json({ status: 'stored', object_key: key, size_bytes: sizeBytes });
  }),
);

releasesRouter.post(
  '/games/:slug/package/confirm',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const sha256 = parseSha(req.body?.sha256);

    const { rows: existing } = await query(
      `SELECT * FROM game_package_versions WHERE game_id = $1 AND sha256 = $2`, [game.id, sha256],
    );
    if (existing.length > 0) {
      const { rows: g } = await query(`SELECT current_package_version_id FROM games WHERE id = $1`, [game.id]);
      return res.json({
        status: 'confirmed', sha256, size_bytes: Number(existing[0].size_bytes),
        version: versionJson(existing[0], g[0].current_package_version_id),
      });
    }

    const key = packageObjectKey(sha256);
    const head = await store.head(key);
    if (!head.exists) throw notFound('No package was uploaded for that hash.');
    if (head.sizeBytes > config.packageMaxSizeBytes) {
      throw badRequest(`Uploaded package exceeds the ${config.packageMaxSizeBytes}-byte size limit.`);
    }

    // One streamed pass both re-hashes the bytes and validates the archive.
    let inspection;
    try {
      inspection = await inspectPackageStream(await store.openStream(key), { maxBytes: config.packageMaxSizeBytes });
    } catch (err) {
      if (err instanceof PackageError) {
        throw badRequest(err.message.includes('size limit') ? err.message : `The package was rejected: ${err.message}`);
      }
      throw err;
    }
    if (inspection.sha256 !== sha256) {
      throw badRequest('The uploaded package\'s real content does not match the declared sha256.');
    }
    let manifest;
    try {
      manifest = validateGamePackage(inspection);
    } catch (err) {
      if (err instanceof PackageError) throw badRequest(`The package was rejected: ${err.message}`);
      throw err;
    }
    await assertQuota(req.user.id, sha256, inspection.sizeBytes);

    const reviewStatus = initialReviewStatus();
    const { version, currentId } = await withTransaction(async (client) => {
      const { rows: locked } = await client.query(
        `SELECT review_status, current_package_version_id FROM games WHERE id = $1 FOR UPDATE`, [game.id],
      );
      const { rows: next } = await client.query(
        `SELECT COALESCE(MAX(version_number), 0) + 1 AS n FROM game_package_versions WHERE game_id = $1`, [game.id],
      );
      const { rows: inserted } = await client.query(
        `INSERT INTO game_package_versions (game_id, version_number, sha256, object_key, size_bytes, manifest,
                                            review_status, uploaded_by)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING *`,
        [game.id, next[0].n, sha256, key, inspection.sizeBytes, manifest, reviewStatus, req.user.id],
      );
      if (reviewStatus === 'approved') {
        await makeVersionLive(client, game.id, inserted[0]);
        return { version: inserted[0], currentId: inserted[0].id };
      }
      // A creator re-submitting after a rejection goes back into the queue.
      if (locked[0].review_status === 'rejected') {
        await client.query(`UPDATE games SET review_status = 'pending' WHERE id = $1`, [game.id]);
      }
      return { version: inserted[0], currentId: locked[0].current_package_version_id };
    });

    res.json({ status: 'confirmed', sha256, size_bytes: inspection.sizeBytes, version: versionJson(version, currentId) });
  }),
);

async function loadGameWithVersion(slug, versionNumber) {
  const { rows } = await query(
    `SELECT g.id AS game_id, g.creator_id, g.published, g.review_status AS game_review_status,
            g.current_package_version_id, v.*
       FROM games g
       LEFT JOIN game_package_versions v
         ON v.game_id = g.id AND (CASE WHEN $2::int IS NULL THEN v.id = g.current_package_version_id
                                       ELSE v.version_number = $2::int END)
      WHERE g.slug = $1`,
    [slug, versionNumber],
  );
  return rows[0] || null;
}

// Players get the live version of an approved, published game. A creator can
// fetch any of their own versions with ?version=N to test it before review.
async function resolveDownloadableVersion(req) {
  const versionNumber = req.query.version !== undefined ? parseVersionNumber(req.query.version) : null;
  const row = await loadGameWithVersion(req.params.slug, versionNumber);
  if (versionNumber !== null) {
    if (!row || !req.user || String(req.user.id) !== String(row.creator_id)) throw notFound('No such version.');
    if (!row.id) throw notFound('No such version.');
    return row;
  }
  if (!row || !row.published || row.game_review_status !== 'approved') throw notFound('No such published game.');
  if (!row.id) throw notFound('This game has no uploaded package yet.');
  return row;
}

releasesRouter.get(
  '/games/:slug/package',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const v = await resolveDownloadableVersion(req);
    const suffix = req.query.version !== undefined ? `?version=${v.version_number}` : '';
    const link = await store.downloadUrl(v.object_key, `/v1/catalog/games/${req.params.slug}/package/download${suffix}`);
    res.json({
      sha256: v.sha256,
      size_bytes: Number(v.size_bytes),
      uploaded_at: v.created_at,
      version_number: v.version_number,
      review_status: v.review_status,
      download_url: link.url,
      expires_in: link.expires_in,
    });
  }),
);

releasesRouter.get(
  '/games/:slug/package/download',
  optionalAuth,
  asyncRoute(async (req, res) => {
    if (store.usingS3()) throw notFound('No such endpoint.');
    const v = await resolveDownloadableVersion(req);
    const head = await store.head(v.object_key);
    if (!head.exists) throw notFound('This game has no uploaded package yet.');
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', String(head.sizeBytes));
    store.streamLocal(v.object_key).pipe(res);
  }),
);

releasesRouter.get(
  '/games/:slug/package/versions',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const [{ rows: g }, { rows: versions }, storage] = await Promise.all([
      query(`SELECT current_package_version_id, review_status FROM games WHERE id = $1`, [game.id]),
      query(`SELECT * FROM game_package_versions WHERE game_id = $1 ORDER BY version_number DESC LIMIT 200`, [game.id]),
      storageSummary(req.user.id),
    ]);
    res.json({
      versions: versions.map((v) => versionJson(v, g[0].current_package_version_id)),
      game_review_status: g[0].review_status,
      review_required: config.gameReviewRequired,
      storage,
    });
  }),
);

releasesRouter.post(
  '/games/:slug/package/versions/:number/activate',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const number = parseVersionNumber(req.params.number);
    const version = await withTransaction(async (client) => {
      await client.query(`SELECT id FROM games WHERE id = $1 FOR UPDATE`, [game.id]);
      const { rows } = await client.query(
        `SELECT * FROM game_package_versions WHERE game_id = $1 AND version_number = $2`, [game.id, number],
      );
      if (rows.length === 0) throw notFound('No such version.');
      if (rows[0].review_status !== 'approved') {
        throw conflict(`Version ${number} is ${rows[0].review_status}; only approved versions can go live.`);
      }
      await makeVersionLive(client, game.id, rows[0]);
      return rows[0];
    });
    res.json({ status: 'live', version: versionJson(version, version.id) });
  }),
);

releasesRouter.delete(
  '/games/:slug/package/versions/:number',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const number = parseVersionNumber(req.params.number);
    const removed = await withTransaction(async (client) => {
      const { rows: g } = await client.query(
        `SELECT current_package_version_id FROM games WHERE id = $1 FOR UPDATE`, [game.id],
      );
      const { rows } = await client.query(
        `SELECT id, object_key FROM game_package_versions WHERE game_id = $1 AND version_number = $2`, [game.id, number],
      );
      if (rows.length === 0) throw notFound('No such version.');
      if (String(rows[0].id) === String(g[0].current_package_version_id)) {
        throw conflict('This version is live. Make another version live before deleting it.');
      }
      await client.query(`DELETE FROM game_package_versions WHERE id = $1`, [rows[0].id]);
      return rows[0];
    });
    await removeObjectIfUnreferenced(removed.object_key);
    res.json({ status: 'deleted', version_number: number, storage: await storageSummary(req.user.id) });
  }),
);

// --- thumbnails and screenshots -------------------------------------------------

export const mediaUrl = (id) => `${config.publicBaseUrl}/v1/catalog/media/${id}`;

function mediaJson(m) {
  return {
    id: String(m.id),
    kind: m.kind,
    url: mediaUrl(m.id),
    content_type: m.content_type,
    size_bytes: Number(m.size_bytes),
    width: m.width,
    height: m.height,
    position: m.position,
    review_status: m.review_status,
    review_note: m.review_note,
    created_at: m.created_at,
  };
}

function parseMediaRequest(body) {
  const kind = String(body?.kind || '');
  if (!MEDIA_KINDS.has(kind)) throw badRequest('kind must be "thumbnail" or "screenshot".');
  const contentType = String(body?.content_type || '');
  if (!MEDIA_TYPES.has(contentType)) throw badRequest('content_type must be image/png or image/jpeg.');
  return { kind, contentType, sha256: parseSha(body?.sha256) };
}

// The newest approved thumbnail is the game's catalog image; older ones go.
async function makeThumbnailLive(client, gameId, mediaId) {
  const { rows: old } = await client.query(
    `DELETE FROM game_media WHERE game_id = $1 AND kind = 'thumbnail' AND review_status = 'approved' AND id <> $2
     RETURNING object_key`,
    [gameId, mediaId],
  );
  await client.query(`UPDATE games SET thumbnail_url = $2, updated_at = NOW() WHERE id = $1`, [gameId, mediaUrl(mediaId)]);
  return old.map((r) => r.object_key);
}

releasesRouter.post(
  '/games/:slug/media/upload-url',
  requireAuth,
  rateLimit({ bucket: 'mediaupload', limit: 60, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    await requireOwnedGame(req.params.slug, req.user.id);
    const { contentType, sha256 } = parseMediaRequest(req.body);
    const sizeBytes = Number(req.body?.size_bytes);
    if (!Number.isFinite(sizeBytes) || sizeBytes <= 0) throw badRequest('size_bytes must be a positive number.');
    if (sizeBytes > config.mediaMaxSizeBytes) throw badRequest(`Images may be at most ${formatBytes(config.mediaMaxSizeBytes)}.`);
    await assertQuota(req.user.id, sha256, sizeBytes);
    const key = mediaObjectKey(sha256, contentType);
    const file = key.slice('media/'.length);
    const target = await store.uploadTarget(key, `/v1/catalog/games/${req.params.slug}/media/local-upload/${file}`);
    res.json({ ...target, object_key: key });
  }),
);

releasesRouter.put(
  '/games/:slug/media/local-upload/:file',
  requireAuth,
  rateLimit({ bucket: 'mediaupload', limit: 60, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    if (store.usingS3()) throw notFound('No such endpoint.');
    await requireOwnedGame(req.params.slug, req.user.id);
    const match = /^([a-f0-9]{64})\.(png|jpg)$/.exec(String(req.params.file));
    if (!match) throw badRequest('Invalid media file name.');
    const key = mediaObjectKey(match[1], match[2] === 'png' ? 'image/png' : 'image/jpeg');
    let sizeBytes;
    try {
      ({ sizeBytes } = await store.storeLocalUpload(key, req, config.mediaMaxSizeBytes));
    } catch (err) {
      if (err.code === 'PACKAGE_TOO_LARGE') throw badRequest(`Images may be at most ${formatBytes(config.mediaMaxSizeBytes)}.`);
      throw err;
    }
    res.json({ status: 'stored', object_key: key, size_bytes: sizeBytes });
  }),
);

releasesRouter.post(
  '/games/:slug/media/confirm',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const { kind, contentType, sha256 } = parseMediaRequest(req.body);
    const key = mediaObjectKey(sha256, contentType);

    const head = await store.head(key);
    if (!head.exists) throw notFound('No image was uploaded for that hash.');
    const bytes = await store.readAll(key, config.mediaMaxSizeBytes);
    if (bytes === null) throw badRequest(`Images may be at most ${formatBytes(config.mediaMaxSizeBytes)}.`);
    if (crypto.createHash('sha256').update(bytes).digest('hex') !== sha256) {
      throw badRequest('The uploaded image\'s real content does not match the declared sha256.');
    }
    const info = imageInfo(bytes);
    if (!info || info.contentType !== contentType) throw badRequest('The upload is not a valid PNG or JPEG image of the declared type.');
    if (info.width > config.mediaMaxDimension || info.height > config.mediaMaxDimension) {
      throw badRequest(`Images may be at most ${config.mediaMaxDimension}x${config.mediaMaxDimension} pixels.`);
    }
    if (info.width < 16 || info.height < 16) throw badRequest('Images must be at least 16x16 pixels.');
    await assertQuota(req.user.id, sha256, bytes.length);

    const reviewStatus = initialReviewStatus();
    const { media, staleKeys } = await withTransaction(async (client) => {
      await client.query(`SELECT id FROM games WHERE id = $1 FOR UPDATE`, [game.id]);
      let stale = [];
      let position = 0;
      if (kind === 'screenshot') {
        const { rows: count } = await client.query(
          `SELECT COUNT(*)::int AS n, COALESCE(MAX(position), -1) + 1 AS next
             FROM game_media WHERE game_id = $1 AND kind = 'screenshot' AND review_status <> 'rejected'`,
          [game.id],
        );
        if (count[0].n >= config.maxScreenshotsPerGame) {
          throw conflict(`A game can have at most ${config.maxScreenshotsPerGame} screenshots. Delete one first.`);
        }
        position = count[0].next;
      } else {
        // Only one thumbnail waits for review at a time; the newest wins.
        const { rows: dropped } = await client.query(
          `DELETE FROM game_media WHERE game_id = $1 AND kind = 'thumbnail' AND review_status <> 'approved'
           RETURNING object_key`,
          [game.id],
        );
        stale = dropped.map((r) => r.object_key);
      }
      const { rows } = await client.query(
        `INSERT INTO game_media (game_id, kind, sha256, object_key, content_type, size_bytes, width, height, position, review_status)
         VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10) RETURNING *`,
        [game.id, kind, sha256, key, contentType, bytes.length, info.width, info.height, position, reviewStatus],
      );
      if (kind === 'thumbnail' && reviewStatus === 'approved') {
        stale = stale.concat(await makeThumbnailLive(client, game.id, rows[0].id));
      }
      return { media: rows[0], staleKeys: stale };
    });
    for (const staleKey of staleKeys) await removeObjectIfUnreferenced(staleKey);
    res.status(201).json({ status: 'confirmed', media: mediaJson(media) });
  }),
);

releasesRouter.get(
  '/games/:slug/media',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const { rows: games } = await query(
      `SELECT id, creator_id, published, review_status FROM games WHERE slug = $1`, [req.params.slug],
    );
    if (games.length === 0) throw notFound('No such game.');
    const isOwner = req.user && String(req.user.id) === String(games[0].creator_id);
    if (!isOwner && (!games[0].published || games[0].review_status !== 'approved')) throw notFound('No such game.');
    const { rows } = await query(
      `SELECT * FROM game_media WHERE game_id = $1 ${isOwner ? '' : "AND review_status = 'approved'"}
        ORDER BY kind DESC, position, id`,
      [games[0].id],
    );
    res.json({ media: rows.map(mediaJson) });
  }),
);

releasesRouter.get(
  '/media/:id',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const id = String(req.params.id || '');
    if (!/^\d+$/.test(id)) throw notFound('No such image.');
    const { rows } = await query(
      `SELECT m.*, g.creator_id FROM game_media m JOIN games g ON g.id = m.game_id WHERE m.id = $1`, [id],
    );
    if (rows.length === 0) throw notFound('No such image.');
    const m = rows[0];
    if (m.review_status !== 'approved') {
      const isOwner = req.user && String(req.user.id) === String(m.creator_id);
      let isAdmin = false;
      if (req.user && !isOwner) {
        const { rows: u } = await query(`SELECT role FROM users WHERE id = $1`, [req.user.id]);
        isAdmin = u[0]?.role === 'admin';
      }
      if (!isOwner && !isAdmin) throw notFound('No such image.');
    }
    if (store.usingS3()) {
      const link = await store.downloadUrl(m.object_key, '', { contentType: m.content_type });
      return res.redirect(302, link.url);
    }
    const head = await store.head(m.object_key);
    if (!head.exists) throw notFound('No such image.');
    res.setHeader('Content-Type', m.content_type);
    res.setHeader('Content-Length', String(head.sizeBytes));
    res.setHeader('Cache-Control', m.review_status === 'approved' ? 'public, max-age=86400, immutable' : 'private, no-store');
    store.streamLocal(m.object_key).pipe(res);
  }),
);

releasesRouter.delete(
  '/games/:slug/media/:id',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const id = String(req.params.id || '');
    if (!/^\d+$/.test(id)) throw badRequest('Invalid image id.');
    const removed = await withTransaction(async (client) => {
      const { rows } = await client.query(
        `DELETE FROM game_media WHERE id = $1 AND game_id = $2 RETURNING kind, object_key, review_status`, [id, game.id],
      );
      if (rows.length === 0) throw notFound('No such image for that game.');
      if (rows[0].kind === 'thumbnail' && rows[0].review_status === 'approved') {
        await client.query(`UPDATE games SET thumbnail_url = '', updated_at = NOW() WHERE id = $1`, [game.id]);
      }
      return rows[0];
    });
    await removeObjectIfUnreferenced(removed.object_key);
    res.json({ status: 'deleted', storage: await storageSummary(req.user.id) });
  }),
);

releasesRouter.get(
  '/storage',
  requireAuth,
  asyncRoute(async (req, res) => {
    res.json({ storage: await storageSummary(req.user.id), review_required: config.gameReviewRequired });
  }),
);

// --- moderator review queue (mounted under /v1/moderation/catalog) ----------

catalogReviewRouter.get(
  '/pending',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 200);
    const [{ rows: versions }, { rows: media }] = await Promise.all([
      query(
        `SELECT v.*, g.slug, g.title, g.description, g.creator_id, g.review_status AS game_review_status,
                u.display_name AS creator_name
           FROM game_package_versions v JOIN games g ON g.id = v.game_id JOIN users u ON u.id = g.creator_id
          WHERE v.review_status = 'pending'
          ORDER BY v.created_at ASC LIMIT $1`,
        [limit],
      ),
      query(
        `SELECT m.*, g.slug, g.title, g.creator_id, u.display_name AS creator_name
           FROM game_media m JOIN games g ON g.id = m.game_id JOIN users u ON u.id = g.creator_id
          WHERE m.review_status = 'pending'
          ORDER BY m.created_at ASC LIMIT $1`,
        [limit],
      ),
    ]);
    const versionItems = await Promise.all(versions.map(async (v) => {
      const link = await store.downloadUrl(v.object_key, `/v1/moderation/catalog/versions/${v.id}/download`);
      return {
        ...versionJson(v, null),
        game: { slug: v.slug, title: v.title, description: v.description, review_status: v.game_review_status },
        creator: { id: String(v.creator_id), display_name: v.creator_name },
        download_url: link.url,
      };
    }));
    res.json({
      versions: versionItems,
      media: media.map((m) => ({
        ...mediaJson(m),
        game: { slug: m.slug, title: m.title },
        creator: { id: String(m.creator_id), display_name: m.creator_name },
      })),
    });
  }),
);

catalogReviewRouter.get(
  '/versions/:id/download',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    if (store.usingS3()) throw notFound('No such endpoint.');
    const { rows } = await query(`SELECT object_key FROM game_package_versions WHERE id = $1`, [String(req.params.id)]);
    if (rows.length === 0) throw notFound('No such version.');
    const head = await store.head(rows[0].object_key);
    if (!head.exists) throw notFound('No such version.');
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', String(head.sizeBytes));
    store.streamLocal(rows[0].object_key).pipe(res);
  }),
);

function parseReview(req) {
  const id = String(req.params.id || '');
  if (!/^\d+$/.test(id)) throw badRequest('A numeric id is required.');
  if (typeof req.body?.approve !== 'boolean') throw badRequest('approve must be true or false.');
  const note = String(req.body?.note || '').slice(0, 2000);
  if (!req.body.approve && !note.trim()) throw badRequest('Say why when rejecting, so the creator can fix it.');
  return { id, approve: req.body.approve, note };
}

catalogReviewRouter.post(
  '/versions/:id/review',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const { id, approve, note } = parseReview(req);
    const result = await withTransaction(async (client) => {
      const { rows } = await client.query(
        `SELECT v.*, g.creator_id, g.current_package_version_id FROM game_package_versions v
           JOIN games g ON g.id = v.game_id WHERE v.id = $1 FOR UPDATE OF v, g`,
        [id],
      );
      if (rows.length === 0) throw notFound('No such version.');
      const v = rows[0];
      if (v.review_status !== 'pending') throw conflict(`This version was already ${v.review_status}.`);
      const status = approve ? 'approved' : 'rejected';
      const { rows: updated } = await client.query(
        `UPDATE game_package_versions SET review_status = $2, review_note = $3, reviewed_by = $4, reviewed_at = NOW()
          WHERE id = $1 RETURNING *`,
        [id, status, note, req.user.id],
      );
      let live = false;
      if (approve) {
        // Approving goes live unless the creator has since made a newer version live.
        let currentNumber = 0;
        if (v.current_package_version_id) {
          const { rows: cur } = await client.query(
            `SELECT version_number FROM game_package_versions WHERE id = $1`, [v.current_package_version_id],
          );
          currentNumber = cur[0]?.version_number || 0;
        }
        if (v.version_number > currentNumber) {
          await makeVersionLive(client, v.game_id, updated[0]);
          live = true;
        }
      } else {
        const { rows: approved } = await client.query(
          `SELECT 1 FROM game_package_versions WHERE game_id = $1 AND review_status = 'approved' LIMIT 1`, [v.game_id],
        );
        if (approved.length === 0) await client.query(`UPDATE games SET review_status = 'rejected' WHERE id = $1`, [v.game_id]);
      }
      await client.query(
        `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason, package_version_id)
         VALUES ($1, $2, $3, $4, $5)`,
        [req.user.id, v.creator_id, approve ? 'approve_game_version' : 'reject_game_version', note, id],
      );
      return { version: updated[0], live };
    });
    res.json({ status: result.version.review_status, live: result.live, version: versionJson(result.version, null) });
  }),
);

catalogReviewRouter.post(
  '/media/:id/review',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const { id, approve, note } = parseReview(req);
    const { media, staleKeys } = await withTransaction(async (client) => {
      const { rows } = await client.query(
        `SELECT m.*, g.creator_id FROM game_media m JOIN games g ON g.id = m.game_id WHERE m.id = $1 FOR UPDATE OF m, g`, [id],
      );
      if (rows.length === 0) throw notFound('No such image.');
      const m = rows[0];
      if (m.review_status !== 'pending') throw conflict(`This image was already ${m.review_status}.`);
      const { rows: updated } = await client.query(
        `UPDATE game_media SET review_status = $2, review_note = $3, reviewed_by = $4, reviewed_at = NOW()
          WHERE id = $1 RETURNING *`,
        [id, approve ? 'approved' : 'rejected', note, req.user.id],
      );
      let stale = [];
      if (approve && m.kind === 'thumbnail') stale = await makeThumbnailLive(client, m.game_id, m.id);
      await client.query(
        `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason, media_id) VALUES ($1, $2, $3, $4, $5)`,
        [req.user.id, m.creator_id, approve ? 'approve_game_media' : 'reject_game_media', note, id],
      );
      return { media: updated[0], staleKeys: stale };
    });
    for (const staleKey of staleKeys) await removeObjectIfUnreferenced(staleKey);
    res.json({ status: media.review_status, media: mediaJson(media) });
  }),
);

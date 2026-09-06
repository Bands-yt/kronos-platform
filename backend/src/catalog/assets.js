// Chunked Delta-Binary Sync + collaborative scene/dialogue metadata --
// the v0.4.0 "replace Perforce/Git LFS" surface. Shares the exact same
// storage backend (S3 or local disk, see storage/s3.js and
// storage/local.js) and the exact same games-row ownership check
// (requireOwnedGame, ./ownership.js) as the whole-package pipeline in
// ./routes.js -- this file only adds chunk granularity, a version
// manifest, and exclusive locks on top of that already-real pipeline.
import express from 'express';

import { config } from '../config.js';
import { query, withTransaction } from '../db.js';
import { asyncRoute, badRequest, conflict, notFound } from '../errors.js';
import { optionalAuth, requireAuth } from '../middleware/auth.js';
import { rateLimit } from '../middleware/rateLimit.js';
import { chunkObjectKey } from '../storage/objectKey.js';
import {
  s3Configured, createPresignedUploadUrl, createPresignedDownloadUrl, headObject, verifyObjectHash,
} from '../storage/s3.js';
import {
  headObject as headLocalObject, verifyObjectHash as verifyLocalObjectHash,
  writeObjectFromStream as writeLocalObjectFromStream, readObjectStream as readLocalObjectStream,
} from '../storage/local.js';
import { requireOwnedGame } from './ownership.js';

export const assetsRouter = express.Router();

const SHA256_RE = /^[a-f0-9]{64}$/;

// --- chunks ------------------------------------------------------------------
//
// Same three-step shape as the package pipeline (upload-url -> PUT ->
// confirm) -- confirm is what re-hashes server-side and is the only
// thing that ever inserts into asset_chunks, so "known to this service"
// always means "really, verifiably stored", never just "a client claims
// so".

assetsRouter.post(
  '/games/:slug/chunks/upload-url',
  requireAuth,
  rateLimit({ bucket: 'assetchunkupload', limit: 600, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    await requireOwnedGame(req.params.slug, req.user.id);

    const sha256 = String(req.body?.sha256 || '').trim().toLowerCase();
    if (!SHA256_RE.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');
    const sizeBytes = Number(req.body?.size_bytes);
    if (!Number.isFinite(sizeBytes) || sizeBytes <= 0) throw badRequest('size_bytes must be a positive number.');
    if (sizeBytes > config.assetChunkMaxSizeBytes) {
      throw badRequest(`Chunk exceeds the ${config.assetChunkMaxSizeBytes}-byte per-chunk size limit.`);
    }

    // The real dedup win: a chunk this service has already confirmed
    // needs no upload at all, regardless of which game or version asks
    // for it next -- the whole point of a global, content-addressed pool.
    const { rows } = await query(`SELECT size_bytes FROM asset_chunks WHERE sha256 = $1`, [sha256]);
    if (rows.length > 0) {
      return res.json({ needed: false, object_key: chunkObjectKey(sha256), size_bytes: Number(rows[0].size_bytes) });
    }

    const key = chunkObjectKey(sha256);
    if (s3Configured()) {
      const uploadUrl = await createPresignedUploadUrl(key);
      return res.json({
        needed: true, upload_url: uploadUrl, object_key: key, expires_in: config.packageUploadTtlSeconds, storage: 's3',
      });
    }
    res.json({
      needed: true,
      upload_url: `${config.publicBaseUrl}/v1/catalog/games/${req.params.slug}/chunks/local-upload/${sha256}`,
      object_key: key,
      expires_in: null,
      storage: 'local',
    });
  }),
);

assetsRouter.put(
  '/games/:slug/chunks/local-upload/:sha256',
  requireAuth,
  rateLimit({ bucket: 'assetchunkupload', limit: 600, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    if (s3Configured()) throw notFound('No such endpoint.');
    await requireOwnedGame(req.params.slug, req.user.id);

    const sha256 = String(req.params.sha256 || '').trim().toLowerCase();
    if (!SHA256_RE.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');

    const key = chunkObjectKey(sha256);
    let sizeBytes;
    try {
      ({ sizeBytes } = await writeLocalObjectFromStream(key, req, config.assetChunkMaxSizeBytes));
    } catch (err) {
      if (err.code === 'PACKAGE_TOO_LARGE') throw badRequest(err.message);
      throw err;
    }
    res.json({ status: 'stored', object_key: key, size_bytes: sizeBytes });
  }),
);

assetsRouter.post(
  '/games/:slug/chunks/:sha256/confirm',
  requireAuth,
  asyncRoute(async (req, res) => {
    await requireOwnedGame(req.params.slug, req.user.id);

    const sha256 = String(req.params.sha256 || '').trim().toLowerCase();
    if (!SHA256_RE.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');
    const key = chunkObjectKey(sha256);
    const useS3 = s3Configured();

    const head = useS3 ? await headObject(key) : await headLocalObject(key);
    if (!head.exists) throw notFound('No chunk was uploaded for that hash.');
    if (head.sizeBytes > config.assetChunkMaxSizeBytes) {
      throw badRequest(`Uploaded chunk exceeds the ${config.assetChunkMaxSizeBytes}-byte size limit.`);
    }
    const verified = useS3 ? await verifyObjectHash(key, sha256) : await verifyLocalObjectHash(key, sha256);
    if (!verified.matches) throw badRequest('The uploaded chunk\'s real content does not match the declared sha256.');

    await query(
      `INSERT INTO asset_chunks (sha256, object_key, size_bytes) VALUES ($1, $2, $3)
       ON CONFLICT (sha256) DO NOTHING`,
      [sha256, key, verified.sizeBytes],
    );
    res.json({ status: 'confirmed', sha256, size_bytes: verified.sizeBytes });
  }),
);

// Local-disk counterpart to a presigned S3 GET, same convention as
// catalog/routes.js's own package/download route -- only ever handed out
// when no bucket is configured.
assetsRouter.get(
  '/chunks/:sha256/download',
  asyncRoute(async (req, res) => {
    if (s3Configured()) throw notFound('No such endpoint.');
    const sha256 = String(req.params.sha256 || '').trim().toLowerCase();
    if (!SHA256_RE.test(sha256)) throw badRequest('sha256 must be a hex SHA-256 digest.');

    const { rows } = await query(`SELECT object_key FROM asset_chunks WHERE sha256 = $1`, [sha256]);
    if (rows.length === 0) throw notFound('No such chunk.');
    const key = rows[0].object_key;
    const head = await headLocalObject(key);
    if (!head.exists) throw notFound('No such chunk.');

    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', String(head.sizeBytes));
    readLocalObjectStream(key).pipe(res);
  }),
);

// --- versions ------------------------------------------------------------

const MAX_CHUNKS_PER_VERSION = 20000;

assetsRouter.post(
  '/games/:slug/versions',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);

    const chunks = Array.isArray(req.body?.chunks) ? req.body.chunks.map((c) => String(c).trim().toLowerCase()) : null;
    if (!chunks || chunks.length === 0) throw badRequest('chunks must be a non-empty array of sha256 hashes.');
    if (chunks.length > MAX_CHUNKS_PER_VERSION) throw badRequest(`A version may reference at most ${MAX_CHUNKS_PER_VERSION} chunks.`);
    if (!chunks.every((c) => SHA256_RE.test(c))) throw badRequest('Every entry in chunks must be a hex SHA-256 digest.');
    const totalSizeBytes = Number(req.body?.total_size_bytes);
    if (!Number.isFinite(totalSizeBytes) || totalSizeBytes < 0) throw badRequest('total_size_bytes must be a non-negative number.');
    const label = String(req.body?.label || '').slice(0, 200);

    // Every referenced chunk must really be a confirmed, stored chunk --
    // never trust that a manifest naming a hash means the bytes are
    // actually there.
    const distinct = [...new Set(chunks)];
    const { rows: found } = await query(`SELECT sha256 FROM asset_chunks WHERE sha256 = ANY($1::text[])`, [distinct]);
    if (found.length !== distinct.length) {
      const foundSet = new Set(found.map((r) => r.sha256));
      const missing = distinct.filter((c) => !foundSet.has(c)).slice(0, 5);
      throw badRequest(`One or more chunks have not been uploaded/confirmed yet: ${missing.join(', ')}${distinct.length - found.length > 5 ? ', ...' : ''}`);
    }

    const version = await withTransaction(async (client) => {
      // Locks the games row for the duration of the transaction so two
      // concurrent version-creation requests for the same game can never
      // compute the same "next version_number" and collide.
      await client.query(`SELECT id FROM games WHERE id = $1 FOR UPDATE`, [game.id]);
      const { rows: nextRows } = await client.query(
        `SELECT COALESCE(MAX(version_number), 0) + 1 AS next FROM asset_versions WHERE game_id = $1`, [game.id],
      );
      const versionNumber = nextRows[0].next;
      const { rows: inserted } = await client.query(
        `INSERT INTO asset_versions (game_id, version_number, label, chunk_sha256s, total_size_bytes, created_by)
         VALUES ($1, $2, $3, $4, $5, $6)
         RETURNING id, version_number, label, total_size_bytes, created_at`,
        [game.id, versionNumber, label, chunks, totalSizeBytes, req.user.id],
      );
      await client.query(
        `UPDATE games SET current_asset_version_id = $1, updated_at = NOW() WHERE id = $2`,
        [inserted[0].id, game.id],
      );
      return inserted[0];
    });

    res.status(201).json({
      status: 'created',
      version: {
        id: String(version.id), version_number: version.version_number, label: version.label,
        total_size_bytes: Number(version.total_size_bytes), created_at: version.created_at, chunk_count: chunks.length,
      },
    });
  }),
);

assetsRouter.get(
  '/games/:slug/versions',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const { rows: games } = await query(`SELECT id, current_asset_version_id FROM games WHERE slug = $1`, [req.params.slug]);
    if (games.length === 0) throw notFound('No such game.');

    const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 200);
    const { rows } = await query(
      `SELECT id, version_number, label, total_size_bytes, created_at, array_length(chunk_sha256s, 1) AS chunk_count
         FROM asset_versions WHERE game_id = $1 ORDER BY version_number DESC LIMIT $2`,
      [games[0].id, limit],
    );
    res.json({
      versions: rows.map((v) => ({
        id: String(v.id), version_number: v.version_number, label: v.label,
        total_size_bytes: Number(v.total_size_bytes), created_at: v.created_at, chunk_count: v.chunk_count,
        current: games[0].current_asset_version_id !== null && String(games[0].current_asset_version_id) === String(v.id),
      })),
    });
  }),
);

async function loadVersionForGame(slug, versionId) {
  const { rows } = await query(
    `SELECT av.id, av.game_id, av.version_number, av.chunk_sha256s, g.slug, g.published, g.creator_id, g.current_asset_version_id
       FROM asset_versions av JOIN games g ON g.id = av.game_id
      WHERE g.slug = $1 AND av.id = $2`,
    [slug, versionId],
  );
  if (rows.length === 0) throw notFound('No such version for that game.');
  return rows[0];
}

// The delta computation itself: given what the caller already has,
// return only the chunks it's missing, each with a real download URL
// minted just for it -- a chunk the caller already has costs nothing
// (no presign call, no URL) to report back.
//
// A published game's current version is public (optionalAuth), same as
// GET /games/:slug/package; anything else (an older version, or any
// version of an unpublished/WIP game) is a real work-in-progress asset
// and requires proving ownership -- this repo has no multi-collaborator
// ACL yet (games has a single creator_id), so "owner" is the only real
// notion of "someone allowed to see this" available today.
assetsRouter.post(
  '/games/:slug/versions/:versionId/sync-plan',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const versionId = Number(req.params.versionId);
    if (!Number.isFinite(versionId)) throw badRequest('Invalid version id.');
    const version = await loadVersionForGame(req.params.slug, versionId);

    const isCurrentPublished = version.published && String(version.current_asset_version_id) === String(version.id);
    if (!isCurrentPublished) {
      if (!req.user || String(req.user.id) !== String(version.creator_id)) {
        throw notFound('No such version for that game.');
      }
    }

    const have = new Set(
      (Array.isArray(req.body?.have) ? req.body.have : [])
        .map((s) => String(s).trim().toLowerCase())
        .filter((s) => SHA256_RE.test(s)),
    );

    const distinct = [...new Set(version.chunk_sha256s)];
    const { rows: sizeRows } = await query(`SELECT sha256, size_bytes FROM asset_chunks WHERE sha256 = ANY($1::text[])`, [distinct]);
    const sizeBySha = new Map(sizeRows.map((r) => [r.sha256, Number(r.size_bytes)]));
    const useS3 = s3Configured();

    const chunks = await Promise.all(distinct.map(async (sha) => {
      const needed = !have.has(sha);
      const entry = { sha256: sha, size_bytes: sizeBySha.get(sha) ?? null, needed };
      if (needed) {
        const key = chunkObjectKey(sha);
        entry.download_url = useS3
          ? await createPresignedDownloadUrl(key)
          : `${config.publicBaseUrl}/v1/catalog/chunks/${sha}/download`;
      }
      return entry;
    }));

    res.json({
      version_id: String(version.id), version_number: version.version_number,
      chunk_order: version.chunk_sha256s, chunks,
    });
  }),
);

assetsRouter.post(
  '/games/:slug/versions/:versionId/rollback',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const versionId = Number(req.params.versionId);
    if (!Number.isFinite(versionId)) throw badRequest('Invalid version id.');

    const { rows } = await query(`SELECT id, version_number FROM asset_versions WHERE id = $1 AND game_id = $2`, [versionId, game.id]);
    if (rows.length === 0) throw notFound('No such version for that game.');

    // Instant: this only ever repoints a pointer. Every chunk the target
    // version needs is already sitting in storage (nothing was ever
    // deleted when a newer version was created), so there is no data to
    // move and nothing that can partially fail.
    await query(`UPDATE games SET current_asset_version_id = $1, updated_at = NOW() WHERE id = $2`, [rows[0].id, game.id]);
    res.json({ status: 'rolled_back', version_id: String(rows[0].id), version_number: rows[0].version_number });
  }),
);

// --- asset locks (Perforce-style exclusive checkout) --------------------

function normalizeAssetPath(raw) {
  const path = String(raw || '').trim();
  if (path.length === 0 || path.length > 500) throw badRequest('asset_path must be 1-500 characters.');
  return path;
}

assetsRouter.post(
  '/games/:slug/locks',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const assetPath = normalizeAssetPath(req.body?.asset_path);
    const expiresAt = new Date(Date.now() + config.assetLockTtlSeconds * 1000);

    // Atomic acquire: succeeds when nobody holds this path, the existing
    // hold has expired, or the same caller is refreshing their own lock
    // -- never a read-then-write race between two collaborators (or two
    // of the same creator's own Studio sessions) checking out the same
    // path at once.
    const { rows } = await query(
      `INSERT INTO asset_locks (game_id, asset_path, locked_by, expires_at)
       VALUES ($1, $2, $3, $4)
       ON CONFLICT (game_id, asset_path) DO UPDATE
          SET locked_by = EXCLUDED.locked_by, locked_at = NOW(), expires_at = EXCLUDED.expires_at
        WHERE asset_locks.expires_at <= NOW() OR asset_locks.locked_by = EXCLUDED.locked_by
       RETURNING locked_by, expires_at`,
      [game.id, assetPath, req.user.id, expiresAt],
    );
    if (rows.length === 0) throw conflict('This asset is already locked by someone else.');
    res.json({ status: 'locked', asset_path: assetPath, expires_at: rows[0].expires_at });
  }),
);

assetsRouter.get(
  '/games/:slug/locks',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const { rows } = await query(
      `SELECT asset_path, locked_by, locked_at, expires_at
         FROM asset_locks WHERE game_id = $1 AND expires_at > NOW()
        ORDER BY locked_at DESC`,
      [game.id],
    );
    res.json({ locks: rows.map((l) => ({ ...l, locked_by: String(l.locked_by) })) });
  }),
);

assetsRouter.delete(
  '/games/:slug/locks',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const assetPath = normalizeAssetPath(req.body?.asset_path);
    // The owner controls the whole game (there is no separate
    // collaborator ACL in this schema today -- see the sync-plan
    // route's own comment), so releasing any lock on their own game is
    // always allowed, not just releasing your own.
    await query(`DELETE FROM asset_locks WHERE game_id = $1 AND asset_path = $2`, [game.id, assetPath]);
    res.json({ status: 'released', asset_path: assetPath });
  }),
);

// --- collaborative scene documents ---------------------------------------

assetsRouter.get(
  '/games/:slug/scenes',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const { rows } = await query(
      `SELECT path, current_version_id, updated_by, updated_at FROM scene_documents WHERE game_id = $1 ORDER BY path`,
      [game.id],
    );
    res.json({ scenes: rows.map((s) => ({ ...s, current_version_id: s.current_version_id ? String(s.current_version_id) : null })) });
  }),
);

assetsRouter.post(
  '/games/:slug/scenes',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const path = normalizeAssetPath(req.body?.path);
    const currentVersionId = req.body?.current_version_id != null ? Number(req.body.current_version_id) : null;
    if (currentVersionId !== null) {
      const { rows } = await query(`SELECT id FROM asset_versions WHERE id = $1 AND game_id = $2`, [currentVersionId, game.id]);
      if (rows.length === 0) throw badRequest('current_version_id does not reference a real version of this game.');
    }
    await query(
      `INSERT INTO scene_documents (game_id, path, current_version_id, updated_by)
       VALUES ($1, $2, $3, $4)
       ON CONFLICT (game_id, path) DO UPDATE
          SET current_version_id = EXCLUDED.current_version_id, updated_by = EXCLUDED.updated_by, updated_at = NOW()`,
      [game.id, path, currentVersionId, req.user.id],
    );
    res.json({ status: 'saved', path });
  }),
);

// --- dialogue / voiceover phoneme metadata --------------------------------
//
// Data-only storage + CRUD for lip-sync timing -- generating phonemes
// from audio is a real ML/audio-processing project of its own and is
// not built here (see db/migrations/008_platform_services.sql's own
// comment); this is the surface a real generator, or a human sound
// designer, would write into.

const MAX_PHONEMES_PER_LINE = 5000;

assetsRouter.post(
  '/games/:slug/dialogue',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const lineKey = String(req.body?.line_key || '').trim();
    if (lineKey.length === 0 || lineKey.length > 200) throw badRequest('line_key must be 1-200 characters.');
    const locale = String(req.body?.locale || 'en').trim().slice(0, 20) || 'en';
    const transcript = String(req.body?.transcript || '').slice(0, 5000);
    const durationMs = req.body?.duration_ms != null ? Number(req.body.duration_ms) : null;
    if (durationMs !== null && (!Number.isFinite(durationMs) || durationMs < 0)) throw badRequest('duration_ms must be a non-negative number.');

    let audioSha256 = null;
    let audioObjectKey = null;
    if (req.body?.audio_sha256) {
      audioSha256 = String(req.body.audio_sha256).trim().toLowerCase();
      if (!SHA256_RE.test(audioSha256)) throw badRequest('audio_sha256 must be a hex SHA-256 digest.');
      const { rows } = await query(`SELECT object_key FROM asset_chunks WHERE sha256 = $1`, [audioSha256]);
      if (rows.length === 0) throw badRequest('audio_sha256 does not match any uploaded/confirmed chunk -- upload it first.');
      audioObjectKey = rows[0].object_key;
    }

    const { rows: upserted } = await query(
      `INSERT INTO dialogue_lines (game_id, line_key, locale, transcript, audio_object_key, audio_sha256, duration_ms)
       VALUES ($1, $2, $3, $4, $5, $6, $7)
       ON CONFLICT (game_id, line_key, locale) DO UPDATE
          SET transcript = EXCLUDED.transcript, audio_object_key = EXCLUDED.audio_object_key,
              audio_sha256 = EXCLUDED.audio_sha256, duration_ms = EXCLUDED.duration_ms, updated_at = NOW()
       RETURNING id`,
      [game.id, lineKey, locale, transcript, audioObjectKey, audioSha256, durationMs],
    );
    res.json({ status: 'saved', id: String(upserted[0].id), line_key: lineKey, locale });
  }),
);

assetsRouter.get(
  '/games/:slug/dialogue',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const limit = Math.min(Math.max(Number(req.query.limit) || 100, 1), 500);
    const { rows } = await query(
      `SELECT id, line_key, locale, transcript, audio_sha256, duration_ms, updated_at
         FROM dialogue_lines WHERE game_id = $1 ORDER BY line_key, locale LIMIT $2`,
      [game.id, limit],
    );
    res.json({ lines: rows.map((l) => ({ ...l, id: String(l.id) })) });
  }),
);

async function requireOwnedDialogueLine(slug, userId, lineId) {
  const game = await requireOwnedGame(slug, userId);
  const { rows } = await query(`SELECT id FROM dialogue_lines WHERE id = $1 AND game_id = $2`, [lineId, game.id]);
  if (rows.length === 0) throw notFound('No such dialogue line for that game.');
  return { gameId: game.id, lineId: rows[0].id };
}

assetsRouter.post(
  '/games/:slug/dialogue/:lineId/phonemes',
  requireAuth,
  asyncRoute(async (req, res) => {
    const lineId = Number(req.params.lineId);
    if (!Number.isFinite(lineId)) throw badRequest('Invalid dialogue line id.');
    await requireOwnedDialogueLine(req.params.slug, req.user.id, lineId);

    const phonemes = Array.isArray(req.body?.phonemes) ? req.body.phonemes : null;
    if (!phonemes) throw badRequest('phonemes must be an array.');
    if (phonemes.length > MAX_PHONEMES_PER_LINE) throw badRequest(`A dialogue line may have at most ${MAX_PHONEMES_PER_LINE} phonemes.`);
    const normalized = phonemes.map((p, i) => {
      const sequenceIndex = Number(p?.sequence_index ?? i);
      const phoneme = String(p?.phoneme || '').trim().slice(0, 20);
      const startMs = Number(p?.start_ms);
      const endMs = Number(p?.end_ms);
      if (!phoneme) throw badRequest(`phonemes[${i}].phoneme is required.`);
      if (!Number.isFinite(startMs) || !Number.isFinite(endMs) || endMs < startMs) {
        throw badRequest(`phonemes[${i}] must have start_ms <= end_ms.`);
      }
      return { sequenceIndex, phoneme, startMs, endMs };
    });

    // Whole-line replace inside one transaction: a client re-submitting
    // a corrected timing track must never see a half-old/half-new set.
    await withTransaction(async (client) => {
      await client.query(`DELETE FROM dialogue_phonemes WHERE dialogue_line_id = $1`, [lineId]);
      for (const p of normalized) {
        await client.query(
          `INSERT INTO dialogue_phonemes (dialogue_line_id, sequence_index, phoneme, start_ms, end_ms)
           VALUES ($1, $2, $3, $4, $5)`,
          [lineId, p.sequenceIndex, p.phoneme, p.startMs, p.endMs],
        );
      }
    });
    res.json({ status: 'saved', count: normalized.length });
  }),
);

assetsRouter.get(
  '/games/:slug/dialogue/:lineId/phonemes',
  requireAuth,
  asyncRoute(async (req, res) => {
    const lineId = Number(req.params.lineId);
    if (!Number.isFinite(lineId)) throw badRequest('Invalid dialogue line id.');
    await requireOwnedDialogueLine(req.params.slug, req.user.id, lineId);

    const { rows } = await query(
      `SELECT sequence_index, phoneme, start_ms, end_ms FROM dialogue_phonemes
        WHERE dialogue_line_id = $1 ORDER BY sequence_index`,
      [lineId],
    );
    res.json({ phonemes: rows });
  }),
);

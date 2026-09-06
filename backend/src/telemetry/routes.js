// Telemetry & Minidump Handler. A crash dump's real bytes are content-
// addressed straight into whichever storage backend is active (S3 or
// local disk, see storage/s3.js / storage/local.js), exactly like a
// package or chunk -- never in Postgres, which only gets the metadata.
//
// Automated stack trace analysis is a pluggable, honestly-optional
// hook: config.minidumpStackwalkPath is unset by default (no
// minidump-stackwalk binary or symbol store ships with this repo), so a
// report simply stays at status "received" -- a real, honest state, not
// a fabricated trace. Set both minidumpStackwalkPath and
// minidumpSymbolsDir on a deployment that has a real one, and this
// spawns it for real after upload.
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';

import express from 'express';

import { config } from '../config.js';
import { query } from '../db.js';
import { asyncRoute, badRequest, forbidden, notFound } from '../errors.js';
import { optionalAuth, requireAuth } from '../middleware/auth.js';
import { requireOwnedGame } from '../catalog/ownership.js';
import { minidumpObjectKey } from '../storage/objectKey.js';
import { s3Configured, putObjectBuffer, getObjectStream } from '../storage/s3.js';
import {
  writeObjectFromStream as writeLocalObjectFromStream, readObjectStream as readLocalObjectStream,
} from '../storage/local.js';

export const telemetryRouter = express.Router();

telemetryRouter.post(
  '/crash',
  // Scoped to this route only -- the global body parser in server.js is
  // JSON-only and has no reason to accept an arbitrary binary body.
  express.raw({ type: '*/*', limit: config.crashReportMaxSizeBytes }),
  optionalAuth,
  asyncRoute(async (req, res) => {
    const buffer = Buffer.isBuffer(req.body) ? req.body : Buffer.alloc(0);
    if (buffer.length === 0) throw badRequest('Request body must be the raw minidump bytes.');

    let gameId = null;
    const gameSlug = (req.query.game_slug || '').toString().trim();
    if (gameSlug) {
      const { rows } = await query(`SELECT id FROM games WHERE slug = $1`, [gameSlug]);
      if (rows.length > 0) gameId = rows[0].id;
    }
    const engineVersion = (req.query.engine_version || '').toString().slice(0, 100);
    const platform = (req.query.platform || '').toString().slice(0, 100);

    const sha256 = crypto.createHash('sha256').update(buffer).digest('hex');
    const key = minidumpObjectKey(sha256);
    if (s3Configured()) {
      await putObjectBuffer(key, buffer);
    } else {
      await writeLocalObjectFromStream(key, Readable.from(buffer), config.crashReportMaxSizeBytes);
    }

    const { rows: inserted } = await query(
      `INSERT INTO crash_reports (sha256, object_key, size_bytes, game_id, user_id, engine_version, platform)
       VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id`,
      [sha256, key, buffer.length, gameId, req.user?.id || null, engineVersion, platform],
    );
    const crashReportId = inserted[0].id;

    // Fire-and-forget: a crash reporter on a player's machine should
    // never block waiting for symbolication (which may not even be
    // configured at all -- see this file's own header comment).
    processMinidump(crashReportId, key).catch((err) => {
      console.error('[telemetry] minidump processing crashed for report %s: %s', crashReportId, err.message);
    });

    res.status(202).json({ status: 'received', id: String(crashReportId), sha256 });
  }),
);

async function processMinidump(crashReportId, storageKey) {
  if (!config.minidumpStackwalkPath) return; // real, honest no-op -- see header comment.

  await query(`UPDATE crash_reports SET status = 'processing' WHERE id = $1`, [crashReportId]);
  const tmpPath = path.join(os.tmpdir(), `kronos-crash-${crashReportId}-${crypto.randomBytes(6).toString('hex')}.dmp`);
  try {
    const source = s3Configured() ? await getObjectStream(storageKey) : readLocalObjectStream(storageKey);
    await pipeline(source, fs.createWriteStream(tmpPath));

    const stackTrace = await new Promise((resolve, reject) => {
      const args = config.minidumpSymbolsDir ? [tmpPath, config.minidumpSymbolsDir] : [tmpPath];
      const child = spawn(config.minidumpStackwalkPath, args);
      let stdout = '';
      child.stdout.on('data', (chunk) => { stdout += chunk; });
      child.on('error', reject);
      child.on('exit', (code) => (code === 0 ? resolve(stdout) : reject(new Error(`minidump-stackwalk exited with code ${code}`))));
    });

    await query(
      `UPDATE crash_reports SET status = 'processed', stack_trace = $2, processed_at = NOW() WHERE id = $1`,
      [crashReportId, stackTrace.slice(0, 200_000)],
    );
  } catch (err) {
    console.error('[telemetry] symbolication failed for report %s: %s', crashReportId, err.message);
    await query(`UPDATE crash_reports SET status = 'failed', processed_at = NOW() WHERE id = $1`, [crashReportId]);
  } finally {
    await fsp.unlink(tmpPath).catch(() => {});
  }
}

telemetryRouter.get(
  '/crash/:id',
  requireAuth,
  asyncRoute(async (req, res) => {
    const id = Number(req.params.id);
    if (!Number.isFinite(id)) throw badRequest('Invalid crash report id.');

    const { rows } = await query(
      `SELECT cr.id, cr.sha256, cr.size_bytes, cr.game_id, cr.engine_version, cr.platform,
              cr.status, cr.stack_trace, cr.received_at, cr.processed_at, g.creator_id
         FROM crash_reports cr LEFT JOIN games g ON g.id = cr.game_id
        WHERE cr.id = $1`,
      [id],
    );
    if (rows.length === 0) throw notFound('No such crash report.');
    const report = rows[0];

    // Viewable by the owner of the game it was reported against, or an
    // admin -- a report with no game_id (no game_slug was supplied at
    // /crash time) has no real owner at all, so only an admin can see it.
    const { rows: userRows } = await query(`SELECT role FROM users WHERE id = $1`, [req.user.id]);
    const isAdmin = userRows.length > 0 && userRows[0].role === 'admin';
    const isOwner = report.creator_id !== null && String(report.creator_id) === String(req.user.id);
    if (!isAdmin && !isOwner) throw forbidden('Not permitted.');

    res.json({
      crash_report: {
        id: String(report.id), sha256: report.sha256, size_bytes: Number(report.size_bytes),
        game_id: report.game_id !== null ? String(report.game_id) : null,
        engine_version: report.engine_version, platform: report.platform, status: report.status,
        stack_trace: report.stack_trace, received_at: report.received_at, processed_at: report.processed_at,
      },
    });
  }),
);

telemetryRouter.get(
  '/games/:slug/crashes',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const limit = Math.min(Math.max(Number(req.query.limit) || 25, 1), 200);
    const { rows } = await query(
      `SELECT id, sha256, size_bytes, engine_version, platform, status, received_at
         FROM crash_reports WHERE game_id = $1 ORDER BY received_at DESC LIMIT $2`,
      [game.id, limit],
    );
    res.json({
      crash_reports: rows.map((r) => ({ ...r, id: String(r.id), size_bytes: Number(r.size_bytes) })),
    });
  }),
);

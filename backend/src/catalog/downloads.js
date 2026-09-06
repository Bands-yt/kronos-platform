// Web Download portal: serves the creator-tool desktop apps (Kronos
// Studio, 3D Maker, Movie Maker, Audio) for win64 and linux_x86_64.
//
// Unlike the game client's own installer (download.js, GitHub-Releases-
// backed), these builds live content-addressed... except they are NOT
// content-addressed by this backend at all -- a real build is placed at
// a deterministic key (installerObjectKey: installers/<app>/<platform>/
// <version>.bin) by an out-of-band release process, the same "does not
// live in this repo or on this server by default" convention
// download.js's own header comment already establishes. This file only
// ever reports what is REALLY sitting there: an app/platform combo with
// no real object at that key is omitted from the manifest entirely
// (never a fabricated version/hash), same "an invented number is worse
// than no number" rule catalog/routes.js's own livePlayerCounts follows.
//
// The sha256 in the manifest is never trusted from the filename or
// path -- it's computed server-side from the real bytes (same
// verifyObjectHash reasoning storage/s3.js's own header comment gives),
// cached per (key, size) so a "download manifest" page never re-hashes
// an unchanged multi-hundred-MB build on every request.
import crypto from 'node:crypto';

import express from 'express';

import { config } from '../config.js';
import { asyncRoute, notFound } from '../errors.js';
import { installerObjectKey } from '../storage/objectKey.js';
import { s3Configured, createPresignedDownloadUrl, headObject, getObjectStream } from '../storage/s3.js';
import { headObject as headLocalObject, readObjectStream as readLocalObjectStream } from '../storage/local.js';

export const downloadsRouter = express.Router();

const APPS = [
  { app: 'studio', name: 'Kronos Studio' },
  { app: '3d-maker', name: 'Kronos 3D Maker' },
  { app: 'movie-maker', name: 'Kronos Movie Maker' },
  { app: 'audio', name: 'Kronos Audio' },
];
const PLATFORMS = ['win64', 'linux_x86_64'];

function findApp(slug) {
  return APPS.find((a) => a.app === slug);
}

const hashCache = new Map(); // `${key}:${sizeBytes}` -> sha256 hex

async function hashOf(key, sizeBytes, useS3) {
  const cacheKey = `${key}:${sizeBytes}`;
  const cached = hashCache.get(cacheKey);
  if (cached) return cached;

  const hash = crypto.createHash('sha256');
  const stream = useS3 ? await getObjectStream(key) : readLocalObjectStream(key);
  for await (const chunk of stream) hash.update(chunk);
  const sha256 = hash.digest('hex');
  hashCache.set(cacheKey, sha256);
  return sha256;
}

downloadsRouter.get(
  '/latest',
  asyncRoute(async (req, res) => {
    const version = config.desktopAppsVersion;
    const useS3 = s3Configured();
    const apps = [];

    for (const { app, name } of APPS) {
      for (const platform of PLATFORMS) {
        const key = installerObjectKey(app, platform, version);
        const head = useS3 ? await headObject(key) : await headLocalObject(key);
        if (!head.exists) continue; // no real build published for this combo -- omitted, not faked.

        const sha256 = await hashOf(key, head.sizeBytes, useS3);
        const downloadUrl = useS3
          ? await createPresignedDownloadUrl(key)
          : `${config.publicBaseUrl}/v1/downloads/installers/${app}/${platform}`;
        apps.push({
          app, name, platform, version, sha256, size_bytes: head.sizeBytes, download_url: downloadUrl,
        });
      }
    }

    res.json({ version, apps });
  }),
);

downloadsRouter.get(
  '/installers/:app/:platform',
  asyncRoute(async (req, res) => {
    const appEntry = findApp(req.params.app);
    if (!appEntry || !PLATFORMS.includes(req.params.platform)) throw notFound('No such app/platform.');

    const version = config.desktopAppsVersion;
    const key = installerObjectKey(req.params.app, req.params.platform, version);
    const useS3 = s3Configured();
    const head = useS3 ? await headObject(key) : await headLocalObject(key);
    if (!head.exists) throw notFound(`No ${version} build published for ${appEntry.name} on ${req.params.platform} yet.`);

    if (useS3) {
      const downloadUrl = await createPresignedDownloadUrl(key);
      return res.redirect(302, downloadUrl);
    }
    // No bucket configured, so there is no second URL to redirect to --
    // same local-disk fallback shape catalog/routes.js's own package/
    // download route already uses, just served directly from this one
    // endpoint instead of a separate sub-route.
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', String(head.sizeBytes));
    readLocalObjectStream(key).pipe(res);
  }),
);

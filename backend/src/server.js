import path from 'node:path';
import { fileURLToPath } from 'node:url';

import express from 'express';

import { config } from './config.js';
import { pool } from './db.js';
import { redis } from './redis.js';
import { HttpError, asyncRoute } from './errors.js';
import { downloadWindowsInstaller } from './download.js';
import { authRouter } from './auth/routes.js';
import { avatarRouter } from './avatar/routes.js';
import { catalogRouter } from './catalog/routes.js';
import { assetsRouter } from './catalog/assets.js';
import { inventoryRouter } from './inventory/routes.js';
import { leaderboardsRouter } from './leaderboards/routes.js';
import { matchmakingRouter } from './matchmaking/routes.js';
import { moderationRouter } from './moderation/routes.js';
import { sessionRouter } from './sessions/routes.js';
import { socialRouter } from './social/routes.js';
import { telemetryRouter } from './telemetry/routes.js';
import { authPageRouter } from './web/authPage.js';
import { attachWebSocketGateway } from './realtime/gateway.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const publicDir = path.join(__dirname, '..', 'public');

export function createApp() {
  const app = express();

  app.disable('x-powered-by');
  // Bounded body size: an auth endpoint has no reason to accept a large
  // payload, and an unbounded parser is a trivial memory-exhaustion vector.
  app.use(express.json({ limit: '64kb' }));

  app.use((_req, res, next) => {
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('Referrer-Policy', 'no-referrer');
    next();
  });

  // The developer-facing engine landing page. Serves public/index.html at
  // '/' and any other static asset placed alongside it (including the
  // game discovery/management dashboard below); falls through (via
  // next()) for every path that isn't a real file, so this never shadows
  // the API routes or the 404 handler below.
  app.use(express.static(publicDir, { index: 'index.html', maxAge: '5m' }));

  // The game discovery/management dashboard used to be served at '/' --
  // it now lives at public/discover.html (still reachable directly at
  // /discover.html via the static middleware above); this is just the
  // clean-URL alias.
  app.get('/discover', (_req, res) => {
    res.sendFile(path.join(publicDir, 'discover.html'));
  });

  // The Windows installer's direct download route -- see download.js's
  // own header comment for the real local-file / GitHub-release / release-
  // page fallback chain. Both paths are the same real download; the /v1
  // one exists for a client (e.g. the launcher) that expects every real
  // endpoint under /v1, the bare one for a plain marketing link.
  app.get('/download', asyncRoute(downloadWindowsInstaller));
  app.get('/v1/download/windows', asyncRoute(downloadWindowsInstaller));

  app.get('/healthz', async (_req, res) => {
    const health = { status: 'ok', postgres: false, redis: false };
    try {
      await pool.query('SELECT 1');
      health.postgres = true;
    } catch { /* reported as false */ }
    try {
      await redis.ping();
      health.redis = true;
    } catch { /* reported as false */ }
    if (!health.postgres) health.status = 'degraded';
    res.status(health.postgres ? 200 : 503).json(health);
  });

  app.use('/v1/auth', authRouter);
  app.use('/v1/avatar', avatarRouter);
  app.use('/v1/catalog', catalogRouter);
  app.use('/v1/catalog', assetsRouter);
  app.use('/v1/inventory', inventoryRouter);
  app.use('/v1/leaderboards', leaderboardsRouter);
  app.use('/v1/matchmaking', matchmakingRouter);
  app.use('/v1', moderationRouter);
  app.use('/v1/sessions', sessionRouter);
  app.use('/v1', socialRouter);
  app.use('/v1/telemetry', telemetryRouter);
  // The browser sign-in page the launcher hands off to.
  app.use('/', authPageRouter);

  // SPA catch-all: any GET that fell through everything above (not a
  // real static file, not /healthz, not a real /v1/* or /auth/* route)
  // is real browser navigation to a path the dashboard's own client-side
  // router owns, not the server -- serve the same index.html and let it
  // resolve the route itself. The storefront's current navigation is
  // location.hash-based (#admin), which never reaches the server at
  // all, so nothing hits this today; it exists for direct-linking a
  // future real path (e.g. a bookmarked /games/<slug>) and so an
  // unrecognised top-level path shows the app shell instead of a bare
  // JSON error.
  //
  // Scoped to GET (app.get, not app.use) and excludes /v1/*: a POST or
  // an unmatched API path has no business receiving an HTML response,
  // and /v1/nope must keep 404ing as JSON -- see test/api.test.js's own
  // "unknown endpoints 404" case, which this must not break.
  app.get('*', (req, res, next) => {
    if (req.path.startsWith('/v1/')) return next();
    res.sendFile(path.join(publicDir, 'index.html'));
  });

  app.use((_req, res) => res.status(404).json({ error: { code: 'not_found', message: 'No such endpoint.' } }));

  // Central error handler. Anything that is not an explicit HttpError is
  // treated as a bug: it is logged in full server-side and reported to the
  // caller as a bare 500, so an unexpected stack trace or SQL fragment can
  // never leak out through an error body.
  app.use((err, _req, res, _next) => {
    if (err instanceof HttpError) {
      return res.status(err.status).json({ error: { code: err.code, message: err.message, details: err.details } });
    }
    if (err?.type === 'entity.parse.failed') {
      return res.status(400).json({ error: { code: 'bad_request', message: 'Malformed JSON body.' } });
    }
    if (err?.type === 'entity.too.large') {
      return res.status(400).json({ error: { code: 'bad_request', message: 'Request body too large.' } });
    }
    console.error('[error] unhandled:', err);
    res.status(500).json({ error: { code: 'internal', message: 'Something went wrong.' } });
  });

  return app;
}

// Only listen when run directly, so tests can import createApp() without
// binding a port.
if (import.meta.url === `file://${process.argv[1]}`) {
  const app = createApp();
  const server = app.listen(config.port, () => {
    console.log(`[kronos-backend] listening on :${config.port}`);
    if (!config.googleClientId) {
      console.warn('[kronos-backend] GOOGLE_CLIENT_ID is unset -- Google sign-in will refuse all tokens.');
    }
  });
  // The realtime gateway needs the raw http.Server (WebSocket upgrades
  // happen below Express entirely) -- see realtime/gateway.js's own
  // header comment. Not wired up for createApp()-only test servers,
  // which construct their own listener separately (see test/realtime.test.js).
  attachWebSocketGateway(server);
}

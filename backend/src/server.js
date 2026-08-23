import path from 'node:path';
import { fileURLToPath } from 'node:url';

import express from 'express';

import { config } from './config.js';
import { pool } from './db.js';
import { redis } from './redis.js';
import { HttpError } from './errors.js';
import { authRouter } from './auth/routes.js';
import { catalogRouter } from './catalog/routes.js';
import { sessionRouter } from './sessions/routes.js';
import { socialRouter } from './social/routes.js';
import { authPageRouter } from './web/authPage.js';

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

  // The management dashboard. Serves public/index.html at '/' and any
  // other static asset placed alongside it; falls through (via next())
  // for every path that isn't a real file, so this never shadows the API
  // routes or the 404 handler below.
  app.use(express.static(publicDir, { index: 'index.html', maxAge: '5m' }));

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
  app.use('/v1/catalog', catalogRouter);
  app.use('/v1/sessions', sessionRouter);
  app.use('/v1', socialRouter);
  // The browser sign-in page the launcher hands off to.
  app.use('/', authPageRouter);

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
    console.error('[error] unhandled:', err);
    res.status(500).json({ error: { code: 'internal', message: 'Something went wrong.' } });
  });

  return app;
}

// Only listen when run directly, so tests can import createApp() without
// binding a port.
if (import.meta.url === `file://${process.argv[1]}`) {
  const app = createApp();
  app.listen(config.port, () => {
    console.log(`[kronos-backend] listening on :${config.port}`);
    if (!config.googleClientId) {
      console.warn('[kronos-backend] GOOGLE_CLIENT_ID is unset -- Google sign-in will refuse all tokens.');
    }
  });
}

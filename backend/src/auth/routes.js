import express from 'express';

import { config } from '../config.js';
import { query } from '../db.js';
import { asyncRoute, badRequest, conflict, unauthorized } from '../errors.js';
import { rateLimit } from '../middleware/rateLimit.js';
import { requireAuth } from '../middleware/auth.js';
import { sendPasswordResetEmail, sendVerificationEmail } from '../email/mailer.js';
import { GoogleTokenError, verifyGoogleIdToken } from './google.js';
import { burnTimingForUnknownUser, hashPassword, validatePasswordStrength, verifyPassword } from './passwords.js';
import {
  RefreshTokenReuseError,
  consumeOneShotToken,
  issueAccessToken,
  issueOneShotToken,
  issueRefreshToken,
  revokeAllForUser,
  revokeRefreshToken,
  rotateRefreshToken,
} from './tokens.js';

export const authRouter = express.Router();

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

function normalizeEmail(value) {
  return typeof value === 'string' ? value.trim().toLowerCase() : '';
}

function publicUser(row) {
  return {
    id: String(row.id),
    email: row.email,
    display_name: row.display_name,
    email_verified: row.email_verified,
  };
}

async function issueSession(res, user, req) {
  const accessToken = await issueAccessToken(user);
  const refresh = await issueRefreshToken(user.id, { userAgent: req.get('user-agent') || null });
  res.json({
    user: publicUser(user),
    access_token: accessToken,
    token_type: 'Bearer',
    expires_in: config.accessTokenTtlSeconds,
    refresh_token: refresh.token,
  });
}

// --- signup ----------------------------------------------------------------

authRouter.post(
  '/signup',
  rateLimit({ bucket: 'signup', limit: 10, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    const email = normalizeEmail(req.body?.email);
    const password = req.body?.password;
    const displayName = (req.body?.display_name || '').toString().trim() || email.split('@')[0];

    if (!EMAIL_RE.test(email)) throw badRequest('A valid email address is required.');
    const weak = validatePasswordStrength(password);
    if (weak) throw badRequest(weak);
    if (displayName.length > 64) throw badRequest('Display name must be at most 64 characters.');

    const passwordHash = await hashPassword(password);

    let row;
    try {
      const inserted = await query(
        `INSERT INTO users (email, email_lower, display_name, password_hash)
         VALUES ($1, $2, $3, $4)
         RETURNING id, email, display_name, email_verified`,
        [req.body.email.trim(), email, displayName, passwordHash],
      );
      row = inserted.rows[0];
    } catch (err) {
      // 23505 = unique_violation. This is the one place we knowingly
      // confirm an address is taken: signup cannot usefully hide it (the
      // user has to be told why they cannot proceed), whereas login and
      // password-reset below deliberately reveal nothing.
      if (err.code === '23505') throw conflict('An account with that email already exists.');
      throw err;
    }

    const verification = await issueOneShotToken('email_verification_tokens', row.id, config.emailVerificationTtlSeconds);
    await sendVerificationEmail(row.email, verification.token);

    res.status(201);
    await issueSession(res, row, req);
  }),
);

// --- login -----------------------------------------------------------------

authRouter.post(
  '/login',
  rateLimit({ bucket: 'login', limit: 20, windowSeconds: 900 }),
  asyncRoute(async (req, res) => {
    const email = normalizeEmail(req.body?.email);
    const password = req.body?.password;
    if (!email || typeof password !== 'string') throw badRequest('Email and password are required.');

    const { rows } = await query(
      `SELECT id, email, display_name, email_verified, password_hash, disabled_at
         FROM users WHERE email_lower = $1`,
      [email],
    );

    // Identical response and comparable timing whether the account is
    // missing, password-less (Google-only), disabled, or simply wrong.
    // Anything else turns this endpoint into an account-enumeration oracle.
    if (rows.length === 0 || !rows[0].password_hash) {
      await burnTimingForUnknownUser(password);
      throw unauthorized('Incorrect email or password.');
    }
    const user = rows[0];
    const ok = await verifyPassword(password, user.password_hash);
    if (!ok || user.disabled_at) throw unauthorized('Incorrect email or password.');

    await issueSession(res, user, req);
  }),
);

// --- Google sign-in --------------------------------------------------------

authRouter.post(
  '/google',
  rateLimit({ bucket: 'google', limit: 30, windowSeconds: 900 }),
  asyncRoute(async (req, res) => {
    const idToken = req.body?.id_token;
    if (!idToken) throw badRequest('id_token is required.');

    let identity;
    try {
      // Signature/issuer/audience/expiry all verified here -- see
      // google.js for why this is non-negotiable.
      identity = await verifyGoogleIdToken(idToken);
    } catch (err) {
      if (err instanceof GoogleTokenError) throw unauthorized(err.message);
      throw err;
    }

    // Link by the stable `sub` first. Fall back to matching a verified
    // email so an existing password account can adopt Google sign-in --
    // safe only because verifyGoogleIdToken already required
    // email_verified.
    const existing = await query(
      `SELECT id, email, display_name, email_verified, google_sub, disabled_at
         FROM users WHERE google_sub = $1 OR email_lower = $2`,
      [identity.sub, identity.email.toLowerCase()],
    );

    let user;
    if (existing.rows.length === 0) {
      const inserted = await query(
        `INSERT INTO users (email, email_lower, display_name, google_sub, email_verified)
         VALUES ($1, $2, $3, $4, TRUE)
         RETURNING id, email, display_name, email_verified`,
        [identity.email, identity.email.toLowerCase(), identity.displayName, identity.sub],
      );
      user = inserted.rows[0];
    } else {
      user = existing.rows[0];
      if (user.disabled_at) throw unauthorized('This account has been disabled.');
      if (!user.google_sub) {
        const updated = await query(
          `UPDATE users SET google_sub = $1, email_verified = TRUE, updated_at = NOW()
            WHERE id = $2
            RETURNING id, email, display_name, email_verified`,
          [identity.sub, user.id],
        );
        user = updated.rows[0];
      }
    }

    await issueSession(res, user, req);
  }),
);

// --- refresh / logout ------------------------------------------------------

authRouter.post(
  '/refresh',
  rateLimit({ bucket: 'refresh', limit: 120, windowSeconds: 900 }),
  asyncRoute(async (req, res) => {
    const presented = req.body?.refresh_token;
    if (!presented) throw badRequest('refresh_token is required.');

    let rotated;
    try {
      rotated = await rotateRefreshToken(presented, { userAgent: req.get('user-agent') || null });
    } catch (err) {
      if (err instanceof RefreshTokenReuseError) {
        throw unauthorized('This session has been revoked. Please sign in again.');
      }
      throw err;
    }
    if (!rotated) throw unauthorized('Invalid or expired refresh token.');

    const { rows } = await query(
      `SELECT id, email, display_name, email_verified FROM users WHERE id = $1`,
      [rotated.userId],
    );
    if (rows.length === 0) throw unauthorized('Account no longer exists.');

    res.json({
      user: publicUser(rows[0]),
      access_token: await issueAccessToken(rows[0]),
      token_type: 'Bearer',
      expires_in: config.accessTokenTtlSeconds,
      refresh_token: rotated.token,
    });
  }),
);

authRouter.post(
  '/logout',
  asyncRoute(async (req, res) => {
    if (req.body?.refresh_token) await revokeRefreshToken(req.body.refresh_token);
    // Always 204: whether that specific token existed is not something a
    // caller needs (or should be able) to probe.
    res.status(204).end();
  }),
);

authRouter.post(
  '/logout-all',
  requireAuth,
  asyncRoute(async (req, res) => {
    const revoked = await revokeAllForUser(req.user.id);
    res.json({ revoked_sessions: revoked });
  }),
);

// --- password reset --------------------------------------------------------

authRouter.post(
  '/request-password-reset',
  rateLimit({ bucket: 'pwreset', limit: 5, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    const email = normalizeEmail(req.body?.email);

    if (EMAIL_RE.test(email)) {
      const { rows } = await query(`SELECT id, email FROM users WHERE email_lower = $1`, [email]);
      if (rows.length > 0) {
        const reset = await issueOneShotToken('password_reset_tokens', rows[0].id, config.passwordResetTtlSeconds);
        await sendPasswordResetEmail(rows[0].email, reset.token);
      }
    }

    // Unconditionally identical response. Telling the caller whether the
    // address exists would make this endpoint a free account-enumeration
    // tool, and it is unauthenticated by necessity.
    res.json({ status: 'If that email has a Kronos account, a reset link is on its way.' });
  }),
);

authRouter.post(
  '/reset-password',
  rateLimit({ bucket: 'pwreset-confirm', limit: 20, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    const token = req.body?.token;
    const password = req.body?.password;
    if (!token) throw badRequest('token is required.');
    const weak = validatePasswordStrength(password);
    if (weak) throw badRequest(weak);

    const userId = await consumeOneShotToken('password_reset_tokens', token);
    if (!userId) throw badRequest('That reset link is invalid, expired, or has already been used.');

    await query(`UPDATE users SET password_hash = $1, updated_at = NOW() WHERE id = $2`,
                [await hashPassword(password), userId]);

    // Every existing session dies with a password change. If the reset
    // was because the account was compromised, leaving the attacker's
    // sessions alive would defeat the entire exercise.
    const revoked = await revokeAllForUser(userId);
    res.json({ status: 'Password updated.', revoked_sessions: revoked });
  }),
);

// --- email verification ----------------------------------------------------

authRouter.post(
  '/verify-email',
  rateLimit({ bucket: 'verify', limit: 20, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    const token = req.body?.token || req.query?.token;
    if (!token) throw badRequest('token is required.');

    const userId = await consumeOneShotToken('email_verification_tokens', token);
    if (!userId) throw badRequest('That confirmation link is invalid, expired, or has already been used.');

    await query(`UPDATE users SET email_verified = TRUE, updated_at = NOW() WHERE id = $1`, [userId]);
    res.json({ status: 'Email confirmed.' });
  }),
);

authRouter.get(
  '/me',
  requireAuth,
  asyncRoute(async (req, res) => {
    const { rows } = await query(
      `SELECT id, email, display_name, email_verified FROM users WHERE id = $1`,
      [req.user.id],
    );
    if (rows.length === 0) throw unauthorized('Account no longer exists.');
    res.json({ user: publicUser(rows[0]) });
  }),
);

import { forbidden, unauthorized } from '../errors.js';
import { verifyAccessToken } from '../auth/tokens.js';
import { query } from '../db.js';

// Requires a valid access token. Attaches req.user on success.
export async function requireAuth(req, _res, next) {
  const header = req.get('authorization') || '';
  if (!header.startsWith('Bearer ')) return next(unauthorized('Missing bearer token.'));
  try {
    const payload = await verifyAccessToken(header.slice('Bearer '.length).trim());
    req.user = { id: payload.sub, email: payload.email, displayName: payload.display_name };
    next();
  } catch {
    // Deliberately opaque: distinguishing "expired" from "bad signature"
    // in the response body tells an attacker which of their guesses is
    // closer. The client just refreshes and retries on any 401.
    next(unauthorized('Invalid or expired access token.'));
  }
}

// Requires an admin. Must run after requireAuth (reads req.user.id).
// Deliberately a real, live DB lookup on every request rather than an
// `admin` claim baked into the access token: a token is valid for up to
// accessTokenTtlSeconds after being minted, and a promotion (or, more
// importantly, a REVOCATION) of admin rights must take effect on the
// very next request, not wait out however long a stale token has left.
// Same "never trust a claim that can go stale" reasoning social/
// routes.js's own assertNotGuest() already applies to is_guest/
// account_state.
export async function requireAdmin(req, _res, next) {
  if (!req.user) return next(unauthorized('Authentication required.'));
  try {
    const { rows } = await query(`SELECT role, account_state FROM users WHERE id = $1`, [req.user.id]);
    if (rows.length === 0 || rows[0].account_state === 'terminated' || rows[0].role !== 'admin') {
      return next(forbidden('Admin access required.'));
    }
    next();
  } catch (err) {
    next(err);
  }
}

// Same, but tolerates anonymous callers -- used by the catalogue, which
// is browsable without an account.
export async function optionalAuth(req, _res, next) {
  const header = req.get('authorization') || '';
  if (!header.startsWith('Bearer ')) return next();
  try {
    const payload = await verifyAccessToken(header.slice('Bearer '.length).trim());
    req.user = { id: payload.sub, email: payload.email, displayName: payload.display_name };
  } catch {
    // An invalid token on an optional-auth route is treated as anonymous
    // rather than as an error.
  }
  next();
}

import { unauthorized } from '../errors.js';
import { verifyAccessToken } from '../auth/tokens.js';

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

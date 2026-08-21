import { createRemoteJWKSet, jwtVerify } from 'jose';

import { config } from '../config.js';

// ---------------------------------------------------------------------------
// The fix.
//
// The C++ client (engine/src/core/GoogleAuth.cpp) decodes the Google ID
// token's payload WITHOUT verifying its signature, and says so plainly in
// its own comment. That was defensible while the token never left the
// client: the client had just received it over TLS directly from Google's
// token endpoint, and it was only being used to fill in a local display
// name.
//
// The moment a backend accepts an ID token from a client and derives an
// account identity from it, that reasoning collapses. A JWT is three
// base64 segments; anyone can hand-write a payload claiming
// `sub: "<victim's google id>"` and post it here. Without signature
// verification, that is a complete account-takeover primitive against
// every Google-linked account -- no password, no phishing, just a forged
// string.
//
// So the server verifies, properly and non-negotiably:
//   1. Signature, against Google's published JWKS, restricted to RS256.
//      (Restricting the algorithm matters: accepting `alg: none`, or
//      allowing HS256 with the public key used as an HMAC secret, are
//      both classic JWT forgery routes.)
//   2. Issuer is really Google.
//   3. Audience is really OUR client id -- otherwise a token minted for
//      any other Google application would be accepted here.
//   4. Expiry (jwtVerify enforces exp/nbf itself).
//   5. email_verified, before trusting the email for account linking.
// ---------------------------------------------------------------------------

// Google's key set. createRemoteJWKSet caches keys and refetches on
// rotation, so this is one background fetch rather than a per-login
// round trip to Google.
const GOOGLE_JWKS_URL = new URL('https://www.googleapis.com/oauth2/v3/certs');
const jwks = createRemoteJWKSet(GOOGLE_JWKS_URL, {
  cooldownDuration: 30_000,
  timeoutDuration: 5_000,
});

// Google mints tokens with either issuer; both are legitimate.
const VALID_ISSUERS = ['https://accounts.google.com', 'accounts.google.com'];

export class GoogleTokenError extends Error {}

export async function verifyGoogleIdToken(idToken, { jwksOverride } = {}) {
  if (!config.googleClientId) {
    // Fail closed. With no configured client id there is no audience to
    // check against, and accepting a token we cannot bind to our own
    // application would defeat the entire point of this module.
    throw new GoogleTokenError('Google sign-in is not configured on this server (GOOGLE_CLIENT_ID unset).');
  }
  if (typeof idToken !== 'string' || idToken.split('.').length !== 3) {
    throw new GoogleTokenError('Malformed Google ID token.');
  }

  let payload;
  try {
    ({ payload } = await jwtVerify(idToken, jwksOverride || jwks, {
      issuer: VALID_ISSUERS,
      audience: config.googleClientId,
      algorithms: ['RS256'],
      clockTolerance: 30,
    }));
  } catch (err) {
    throw new GoogleTokenError(`Google ID token failed verification: ${err.message}`);
  }

  if (!payload.sub) {
    throw new GoogleTokenError('Google ID token carries no subject claim.');
  }
  // `sub` is the stable identifier and the only thing safe to key an
  // account on. An email address can change hands; a Google subject
  // cannot. Email is used for display and first-time linking only.
  if (!payload.email) {
    throw new GoogleTokenError('Google ID token carries no email claim.');
  }
  if (payload.email_verified !== true) {
    throw new GoogleTokenError('Google has not verified this account\'s email address.');
  }

  return {
    sub: String(payload.sub),
    email: String(payload.email),
    displayName: payload.name ? String(payload.name) : String(payload.email).split('@')[0],
  };
}

// Central configuration. Everything secret comes from the environment;
// nothing secret has a usable default.
//
// The service refuses to start in production without real secrets rather
// than silently falling back to a development value -- a signing key that
// quietly defaults to a well-known string is indistinguishable from no
// authentication at all.

const isProduction = process.env.NODE_ENV === 'production';

function required(name, devFallback) {
  const value = process.env[name];
  if (value) return value;
  if (isProduction) {
    throw new Error(`${name} must be set in production -- refusing to start with an insecure default.`);
  }
  if (devFallback === undefined) {
    throw new Error(`${name} must be set (no safe development default exists for it).`);
  }
  return devFallback;
}

export const config = {
  isProduction,
  port: Number(process.env.PORT || 8080),

  databaseUrl: process.env.DATABASE_URL || 'postgres://kronos:devpass@127.0.0.1:55432/kronos',
  redisUrl: process.env.REDIS_URL || 'redis://127.0.0.1:56379',

  // Access tokens are deliberately short-lived: they are bearer tokens
  // that we cannot revoke individually once issued, so their blast radius
  // is bounded by time instead. Revocation happens at the refresh layer,
  // which IS server-side state.
  accessTokenTtlSeconds: Number(process.env.ACCESS_TOKEN_TTL || 15 * 60),
  refreshTokenTtlSeconds: Number(process.env.REFRESH_TOKEN_TTL || 30 * 24 * 60 * 60),
  jwtIssuer: process.env.JWT_ISSUER || 'kronos-platform',
  jwtAudience: process.env.JWT_AUDIENCE || 'kronos-client',
  // HS256 with a shared secret is fine while one service both issues and
  // verifies. Move to RS256/EdDSA the moment a second service needs to
  // verify without being able to mint.
  jwtSecret: required('JWT_SECRET', 'dev-only-insecure-jwt-secret-change-me'),

  // Signs the short-lived join tickets a client hands to a game server.
  // Separate from jwtSecret on purpose: game servers must be able to
  // verify tickets without being handed the key that mints login tokens.
  joinTicketSecret: required('JOIN_TICKET_SECRET', 'dev-only-insecure-ticket-secret-change-me'),
  joinTicketTtlSeconds: Number(process.env.JOIN_TICKET_TTL || 60),

  // Google OAuth. The client id is not secret, but it IS security-
  // relevant: it is the audience every Google ID token is checked
  // against, which is what stops a token minted for some other
  // application from being accepted here.
  googleClientId: process.env.GOOGLE_CLIENT_ID || '',

  passwordResetTtlSeconds: Number(process.env.PASSWORD_RESET_TTL || 60 * 60),
  emailVerificationTtlSeconds: Number(process.env.EMAIL_VERIFICATION_TTL || 24 * 60 * 60),

  // How long a game server's heartbeat stays valid. A server that stops
  // heartbeating disappears from allocation automatically.
  serverHeartbeatTtlSeconds: Number(process.env.SERVER_HEARTBEAT_TTL || 30),

  publicBaseUrl: process.env.PUBLIC_BASE_URL || 'http://localhost:8080',
};

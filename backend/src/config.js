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

  // "Open in Kronos": a real, single-use code bridging an authenticated
  // browser session to a freshly-launched desktop client, which starts
  // with no session of its own. Deliberately short -- the whole real
  // round trip (click, OS launches the process, process parses the
  // deep-link URI, process exchanges the code) is normally seconds --
  // same "short bridge used almost immediately" reasoning as
  // joinTicketTtlSeconds above, not the hour/day windows password reset
  // and email verification get.
  launchHandoffTtlSeconds: Number(process.env.LAUNCH_HANDOFF_TTL || 60),

  // How long a game server's heartbeat stays valid. A server that stops
  // heartbeating disappears from allocation automatically.
  serverHeartbeatTtlSeconds: Number(process.env.SERVER_HEARTBEAT_TTL || 30),

  publicBaseUrl: process.env.PUBLIC_BASE_URL || 'http://localhost:8080',

  // Roblox-style on-demand ("JIT") game server provisioning: when
  // /v1/sessions/allocate finds zero alive servers for a published
  // game, the backend spawns a real `engine_runtime --server` process
  // itself instead of just 503ing. engineRuntimePath is deliberately
  // NOT required(...): most deployments of this Node service (including
  // every existing docker-compose.prod.yml `api` container, which is
  // Node-only and has no C++ build in it at all) have no engine binary
  // anywhere on their filesystem, and that is a real, valid, honest
  // configuration -- provisioning just stays off (see
  // provisioner.js's own comment), same as every other real-but-
  // optional integration in this file (googleClientId, etc). Set both
  // on a host where the built binary and games/ checkout actually live
  // alongside this service to turn it on.
  engineRuntimePath: process.env.ENGINE_RUNTIME_PATH || '',
  gamesDir: process.env.KRONOS_GAMES_DIR || '',
  // The URL a JIT-spawned server (running on this same host, see
  // provisioner.js) should use to reach this backend for its own
  // heartbeat. Deliberately separate from publicBaseUrl: that field is
  // for user-facing links (e.g. email verification) and may point
  // through a reverse proxy/external domain that a purely local process
  // has no real reason to round-trip through. Falls back to
  // publicBaseUrl when unset, which is still a real, correct answer
  // (just not the fastest one) for a deployment that hasn't configured
  // this separately.
  jitServerApiUrl: process.env.JIT_SERVER_API_URL || '',
  jitServerHost: process.env.JIT_SERVER_HOST || '127.0.0.1',
  jitServerPortRangeStart: Number(process.env.JIT_SERVER_PORT_RANGE_START || 30000),
  jitServerPortRangeEnd: Number(process.env.JIT_SERVER_PORT_RANGE_END || 30999),
  // How long allocate() waits for a freshly-spawned server's first real
  // heartbeat before giving up on it. engine_runtime heartbeats every
  // ~10s once networking + the requested game's real scene have both
  // loaded (see main.cpp's own heartbeat-hook comment); this leaves
  // real headroom for a cold Vulkan/physics/scripting startup on top of
  // that, observed in practice to land well under this.
  jitSpawnTimeoutSeconds: Number(process.env.JIT_SPAWN_TIMEOUT || 45),

  // Dynamic Asset Streaming: S3-compatible object storage for a
  // published game's real .kronos package archive. Deliberately NOT
  // required(...), same "not configured is a real, honest no-op"
  // convention engineRuntimePath already established -- most
  // deployments of this Node service, and every test run in this
  // sandbox unless explicitly pointed at a real bucket, have no S3
  // credentials at all, and that is a real, valid state: the package
  // upload/download routes just answer 503 rather than crashing.
  s3Bucket: process.env.S3_BUCKET || '',
  s3Region: process.env.S3_REGION || 'us-east-1',
  // Empty means "real AWS S3" (the SDK's own default endpoint
  // resolution). Non-empty points at any S3-compatible service --
  // MinIO, Cloudflare R2, Backblaze B2 -- which is also exactly how
  // this gets tested for real in this sandbox with no real AWS account.
  s3Endpoint: process.env.S3_ENDPOINT || '',
  s3AccessKeyId: process.env.S3_ACCESS_KEY_ID || '',
  s3SecretAccessKey: process.env.S3_SECRET_ACCESS_KEY || '',
  // Path-style addressing (https://host/bucket/key) rather than
  // virtual-hosted-style (https://bucket.host/key) -- real AWS S3
  // supports both, but most self-hosted S3-compatible services (MinIO
  // included) only support path-style, and virtual-hosted-style against
  // one of those just fails DNS resolution.
  s3ForcePathStyle: process.env.S3_FORCE_PATH_STYLE === 'true',
  // Optional CDN in front of the bucket -- when set, download URLs are
  // real, direct, unsigned CDN URLs instead of a presigned S3 GET
  // (a public CDN needs no per-request signature; presigning one would
  // just be a URL nobody ever needed to expire).
  s3PublicBaseUrl: (process.env.S3_PUBLIC_BASE_URL || '').replace(/\/+$/, ''),

  // Long enough for a real multi-hundred-MB package to actually finish
  // uploading over a real creator's real upload bandwidth, short enough
  // that a leaked presigned URL is not a standing liability.
  packageUploadTtlSeconds: Number(process.env.PACKAGE_UPLOAD_TTL || 300),
  packageDownloadTtlSeconds: Number(process.env.PACKAGE_DOWNLOAD_TTL || 3600),
  // A real sanity bound, not a product decision about how big a Kronos
  // world is allowed to be -- just cheap insurance against a client
  // (buggy or malicious) declaring an absurd size and this service
  // handing out a presigned URL for it.
  packageMaxSizeBytes: Number(process.env.PACKAGE_MAX_SIZE_BYTES || 500 * 1024 * 1024),
};

# Kronos Backend

Account, catalogue, and session-allocation service for the Kronos platform.
Node.js + Express, PostgreSQL for durable state, Redis for volatile state.

## Why Node rather than C++/Crow

The engine is C++; this service deliberately is not. Authentication is
security-critical and almost entirely made of things that are dangerous to
hand-roll — password KDFs, JWT verification, JWKS fetching and key rotation,
TLS, connection pooling. Node has mature, audited implementations of all of
them. Writing that in Crow would mean reimplementing a lot of security
primitives for no benefit beyond language uniformity.

## Running locally

```bash
podman run -d --rm --name kronos-pg    -e POSTGRES_PASSWORD=devpass -e POSTGRES_USER=kronos -e POSTGRES_DB=kronos -p 55432:5432 docker.io/library/postgres:16-alpine
podman run -d --rm --name kronos-redis -p 56379:6379 docker.io/library/redis:7-alpine

npm install
npm run migrate
npm start
```

Tests run against real Postgres and Redis — nothing is mocked:

```bash
GOOGLE_CLIENT_ID=test-client-id.apps.googleusercontent.com npm test
```

## Endpoints

### Auth (`/v1/auth`)
| Method | Path | Notes |
|---|---|---|
| POST | `/signup` | Creates an account, sends a confirmation email, returns a session |
| POST | `/login` | Email + password |
| POST | `/google` | Exchanges a **verified** Google ID token for a session |
| POST | `/refresh` | Rotates the refresh token |
| POST | `/logout` | Revokes one refresh token |
| POST | `/logout-all` | Revokes every session for the user |
| POST | `/request-password-reset` | Always returns the same response |
| POST | `/reset-password` | Single-use token; revokes all sessions |
| POST | `/verify-email` | Single-use token |
| GET | `/me` | Current user |

### Catalogue (`/v1/catalog`)
| Method | Path | Notes |
|---|---|---|
| GET | `/games` | Keyset-paginated grid: title, creator, thumbnail, live player count |
| GET | `/games/:slug` | One game |

### Sessions (`/v1/sessions`)
| Method | Path | Notes |
|---|---|---|
| POST | `/servers/:key/heartbeat` | Game servers call this on a timer |
| POST | `/allocate` | Returns host/port + a signed join ticket |
| POST | `/verify-ticket` | Game servers validate a client's ticket |

## Security decisions worth knowing

**Google ID tokens are verified, not decoded.** The C++ client decodes the
ID token payload without checking its signature — fine while the token never
leaves the client, but a total account-takeover hole the moment a server
trusts it, since anyone can hand-write a JWT payload. This service verifies
the signature against Google's JWKS, pins the algorithm to RS256, and checks
issuer, audience and expiry. There is a test that feeds it a forged token and
asserts nobody gets logged in.

**Passwords** use scrypt from Node's stdlib (N=2^15, ~32 MB per hash) rather
than an argon2 native module — memory-hard, nothing to compile, nothing extra
to trust. The stored format records its own parameters so they can be raised
later without invalidating existing hashes.

**Refresh tokens** are random opaque strings stored as SHA-256 hashes,
rotated on every use, and revocable. Replaying a rotated token is treated as
theft and revokes the entire token family.

**No account enumeration.** Login and password-reset return identical
responses whether or not the account exists, and login burns comparable CPU
on the unknown-account path so timing does not leak it either.

**Player counts are never invented.** They come only from live server
heartbeats in Redis with a TTL. No heartbeats means the count is 0 — or, if
Redis is unreachable, `player_counts_available: false` so clients can say
"unavailable" rather than draw a confident wrong number.

**Allocation issues a signed join ticket** bound to one server and expiring in
60 seconds. Without it a client could connect straight to any server's
ip:port and the server would have no way to know we sent them.

## Not done yet

- No HTTPS termination here; run it behind a TLS-terminating proxy.
- No email provider wired in — `src/email/mailer.js` is the hook; the default
  transport logs instead of sending.
- No admin/publishing endpoints yet: `games` and `game_servers` rows are
  inserted directly for now.
- Rate limiting is per-IP and fails open if Redis is down.

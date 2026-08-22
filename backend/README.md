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
| GET | `/games` | Keyset-paginated grid (limit max 200): title, creator, thumbnail, live player count |
| POST | `/games/publish` | One-click publish from Studio; owner-only re-publish |
| GET | `/games/:slug` | One game |

### Sessions (`/v1/sessions`)
| Method | Path | Notes |
|---|---|---|
| POST | `/servers/:key/heartbeat` | Game servers call this on a timer |
| POST | `/allocate` | Returns host/port + a signed join ticket |
| POST | `/verify-ticket` | Game servers validate a client's ticket |

### Social (`/v1`)
| Method | Path | Notes |
|---|---|---|
| GET | `/users/search?q=` | 3-char minimum, LIKE metacharacters escaped |
| POST | `/friends/request` | Rejects self, guests, blocked pairs |
| POST | `/friends/respond` | Only the addressee may accept |
| GET | `/friends/list` | Enriched with Redis presence + direct-join tickets |
| DELETE | `/friends/:userId` | Remove friend |
| POST | `/presence/heartbeat` | 15s client heartbeat, 40s TTL; offline/launcher/studio/in-game |
| GET | `/users` | Keyset-paginated account directory (limit max 200) with live presence |
| GET | `/presence/summary` | Aggregate live counts for the dashboard |
| POST | `/auth/guest` | Zero-friction guest account, no email |
| POST | `/auth/username` | Claim/change handle; clears `requires_rename` |

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

## Account lifecycle

**Bans are multi-layer.** A termination records hashed `email`, `hwid` and
`ip` identifiers, so changing only the email does not evade it. Values are
hashed because an email and an IP are both personal data and this table only
needs to answer "have I seen this before?".

Two honest caveats: hardware fingerprints are personal data under GDPR and
are defeated by a reinstall or a VM, so `hwid` is a speed bump against
casual evasion rather than an identity system; and IP bans should usually be
given an `expires_at`, because addresses get reassigned and a permanent one
eventually punishes a stranger.

**Disposable email filtering** uses a small embedded list. The real lists run
to tens of thousands of domains, go stale constantly, and will eventually
block somebody's legitimate provider — so it is friction, deliberately not
the only thing standing between a banned user and a new account.

**Usernames are locked for 30 days** after termination, then released to the
public pool by `npm run recycle-usernames` (idempotent; run it hourly).

**Appeals:**
- Granted *inside* 30 days → account restored, username retained.
- Granted *after* the handle was recycled and taken → account restored with
  its data, flagged `requires_rename`, forcing a free username choice at next
  login. We cannot take a handle back off whoever now holds it, and silently
  reinstating with a NULL username would look like data loss.

**Guests** are barred from friends, search visibility and publishing **on the
server**, not merely in the launcher UI — a client-side restriction is a
suggestion.

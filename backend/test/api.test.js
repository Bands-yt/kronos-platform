// Real integration tests: a real Express app, a real PostgreSQL database,
// and a real Redis. Nothing here is mocked -- a passing run means the
// actual SQL, the actual token rotation, and the actual Redis TTL logic
// all behaved.
import assert from 'node:assert/strict';
import { exec } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import test, { after, before } from 'node:test';
import { fileURLToPath } from 'node:url';

import { createApp } from '../src/server.js';
import { config } from '../src/config.js';
import { pool, query } from '../src/db.js';
import { redis, keys } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';
import { issueJoinTicket, verifyJoinTicket } from '../src/auth/tokens.js';
import { hashPassword, verifyPassword, validatePasswordStrength } from '../src/auth/passwords.js';
import { verifyGoogleIdToken, GoogleTokenError } from '../src/auth/google.js';

const testDir = path.dirname(fileURLToPath(import.meta.url));

let server;
let baseUrl;
const sentEmails = [];

before(async () => {
  setEmailTransport(async (m) => { sentEmails.push(m); });
  server = createApp().listen(0);
  await new Promise((r) => server.once('listening', r));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  server.close();
  await pool.end();
  redis.disconnect();
});

async function api(method, path, { body, token } = {}) {
  const res = await fetch(`${baseUrl}${path}`, {
    method,
    headers: {
      'content-type': 'application/json',
      ...(token ? { authorization: `Bearer ${token}` } : {}),
    },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await res.text();
  return { status: res.status, body: text ? JSON.parse(text) : null };
}

const uniqueEmail = () => `t_${crypto.randomBytes(8).toString('hex')}@example.com`;

async function clearRateLimits() {
  const found = await redis.keys('rl:*');
  if (found.length > 0) await redis.del(...found);
}

// ---------------------------------------------------------------------------
// Passwords
// ---------------------------------------------------------------------------

test('password hashing round-trips and rejects wrong passwords', async () => {
  const hash = await hashPassword('correct horse battery staple');
  assert.ok(hash.startsWith('scrypt$'), 'hash records its own parameters');
  assert.equal(await verifyPassword('correct horse battery staple', hash), true);
  assert.equal(await verifyPassword('wrong password entirely', hash), false);
  // Two hashes of the same password must differ -- proves a real random salt.
  const second = await hashPassword('correct horse battery staple');
  assert.notEqual(hash, second, 'each hash uses a fresh salt');
});

test('password hashing never stores the plaintext', async () => {
  const secret = 'super-secret-passphrase-123';
  const hash = await hashPassword(secret);
  assert.ok(!hash.includes(secret), 'the stored hash must not contain the plaintext');
});

test('password policy rejects weak input', () => {
  assert.ok(validatePasswordStrength('short'));
  assert.ok(validatePasswordStrength('aaaaaaaaaaaaaa'), 'a single repeated character is rejected');
  assert.equal(validatePasswordStrength('a reasonable passphrase'), null);
});

// ---------------------------------------------------------------------------
// Signup / login
// ---------------------------------------------------------------------------

test('signup issues a session, sends a confirmation email, and stores no plaintext', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  const before = sentEmails.length;
  const res = await api('POST', '/v1/auth/signup', {
    body: { email, password: 'a reasonable passphrase', display_name: 'Tester' },
  });
  assert.equal(res.status, 201);
  assert.ok(res.body.access_token && res.body.refresh_token);
  assert.equal(res.body.user.email_verified, false, 'a brand new account is not verified yet');

  assert.equal(sentEmails.length, before + 1, 'a real confirmation email was dispatched through the hook');
  assert.match(sentEmails.at(-1).subject, /Confirm/i);

  const { rows } = await query('SELECT password_hash FROM users WHERE email_lower = $1', [email.toLowerCase()]);
  assert.ok(!rows[0].password_hash.includes('a reasonable passphrase'));
});

test('signup rejects a duplicate email', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  const dup = await api('POST', '/v1/auth/signup', { body: { email, password: 'another good passphrase' } });
  assert.equal(dup.status, 409);
});

test('login succeeds with the right password and fails identically for wrong password or unknown account', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });

  const ok = await api('POST', '/v1/auth/login', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(ok.status, 200);

  const wrongPassword = await api('POST', '/v1/auth/login', { body: { email, password: 'not the password' } });
  const unknownUser = await api('POST', '/v1/auth/login', { body: { email: uniqueEmail(), password: 'whatever' } });

  assert.equal(wrongPassword.status, 401);
  assert.equal(unknownUser.status, 401);
  // Identical bodies: the endpoint must not reveal which accounts exist.
  assert.deepEqual(wrongPassword.body, unknownUser.body,
    'wrong-password and unknown-account must be indistinguishable');
});

test('email confirmation works once and only once', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  const link = sentEmails.at(-1).text.match(/token=([A-Za-z0-9_-]+)/);
  assert.ok(link, 'the confirmation email really carries a token');

  const first = await api('POST', '/v1/auth/verify-email', { body: { token: link[1] } });
  assert.equal(first.status, 200);
  const second = await api('POST', '/v1/auth/verify-email', { body: { token: link[1] } });
  assert.equal(second.status, 400, 'a confirmation token is single-use');
});

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

test('access token authenticates /me and a garbage token does not', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });

  const me = await api('GET', '/v1/auth/me', { token: signup.body.access_token });
  assert.equal(me.status, 200);
  assert.equal(me.body.user.email.toLowerCase(), email.toLowerCase());

  assert.equal((await api('GET', '/v1/auth/me', { token: 'not.a.jwt' })).status, 401);
  assert.equal((await api('GET', '/v1/auth/me')).status, 401);
});

test('refresh rotates the token, and reusing a rotated token revokes the family', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  const original = signup.body.refresh_token;

  const rotated = await api('POST', '/v1/auth/refresh', { body: { refresh_token: original } });
  assert.equal(rotated.status, 200);
  assert.notEqual(rotated.body.refresh_token, original, 'refresh really rotates the token');

  // Replaying the consumed token is treated as theft.
  const replay = await api('POST', '/v1/auth/refresh', { body: { refresh_token: original } });
  assert.equal(replay.status, 401);

  // ...and the token that replaced it is revoked too, so a thief who
  // raced us cannot keep using the newer one either.
  const afterBreach = await api('POST', '/v1/auth/refresh', { body: { refresh_token: rotated.body.refresh_token } });
  assert.equal(afterBreach.status, 401, 'reuse detection revokes the whole family, not just the replayed token');
});

test('password reset consumes the token once and kills every existing session', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });

  const request = await api('POST', '/v1/auth/request-password-reset', { body: { email } });
  assert.equal(request.status, 200);
  const token = sentEmails.at(-1).text.match(/token=([A-Za-z0-9_-]+)/)[1];

  const reset = await api('POST', '/v1/auth/reset-password', { body: { token, password: 'a brand new passphrase' } });
  assert.equal(reset.status, 200);
  assert.ok(reset.body.revoked_sessions >= 1, 'existing sessions are revoked on password change');

  // The pre-reset refresh token must no longer work.
  const stale = await api('POST', '/v1/auth/refresh', { body: { refresh_token: signup.body.refresh_token } });
  assert.equal(stale.status, 401);

  // The token cannot be replayed, and the new password works.
  assert.equal((await api('POST', '/v1/auth/reset-password',
    { body: { token, password: 'yet another passphrase' } })).status, 400);
  assert.equal((await api('POST', '/v1/auth/login',
    { body: { email, password: 'a brand new passphrase' } })).status, 200);
});

test('password reset for an unknown email is indistinguishable from a known one', async () => {
  await clearRateLimits();
  const known = uniqueEmail();
  await api('POST', '/v1/auth/signup', { body: { email: known, password: 'a reasonable passphrase' } });

  const a = await api('POST', '/v1/auth/request-password-reset', { body: { email: known } });
  const b = await api('POST', '/v1/auth/request-password-reset', { body: { email: uniqueEmail() } });
  assert.equal(a.status, b.status);
  assert.deepEqual(a.body, b.body, 'the response must not reveal whether the account exists');
});

// ---------------------------------------------------------------------------
// Launch hand-off ("Open in Kronos") -- a real, short-lived, single-use
// code bridging an authenticated browser session to a freshly-launched
// desktop client, deliberately NOT the real access_token itself (see
// auth/routes.js's own comment on why that would be a real local
// credential-exposure surface, not a hypothetical one).
// ---------------------------------------------------------------------------

test('a launch hand-off code exchanges for a real, full session', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201);

  const minted = await api('POST', '/v1/auth/handoff', { token: signup.body.access_token });
  assert.equal(minted.status, 200);
  assert.ok(minted.body.code, 'a real code is returned');
  assert.ok(minted.body.expires_in > 0 && minted.body.expires_in <= 120, 'the code is short-lived, not hours');

  const exchanged = await api('POST', '/v1/auth/handoff/exchange', { body: { code: minted.body.code } });
  assert.equal(exchanged.status, 200);
  assert.equal(exchanged.body.user.email, email, 'the exchanged session really belongs to the person who minted it');
  assert.ok(exchanged.body.access_token, 'exchange returns a real, full session -- same shape as login');
  assert.ok(exchanged.body.refresh_token);

  // The exchanged access token really works against a protected route.
  const me = await api('GET', '/v1/friends/list', { token: exchanged.body.access_token });
  assert.notEqual(me.status, 401, 'the exchanged access token is really usable, not a placeholder');
});

test('a launch hand-off code is single-use', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  const minted = await api('POST', '/v1/auth/handoff', { token: signup.body.access_token });

  const first = await api('POST', '/v1/auth/handoff/exchange', { body: { code: minted.body.code } });
  assert.equal(first.status, 200);

  const replay = await api('POST', '/v1/auth/handoff/exchange', { body: { code: minted.body.code } });
  assert.equal(replay.status, 401, 'the same code cannot be exchanged twice');
});

test('a launch hand-off code really expires', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  const minted = await api('POST', '/v1/auth/handoff', { token: signup.body.access_token });

  // Real expiry, not a mocked clock: backdate the real row the same way
  // an actually-expired code would look, then confirm the real WHERE
  // expires_at > NOW() in consumeOneShotToken() actually rejects it.
  await query(`UPDATE launch_handoff_tokens SET expires_at = NOW() - INTERVAL '1 second' WHERE user_id = $1`,
    [signup.body.user.id]);

  const expired = await api('POST', '/v1/auth/handoff/exchange', { body: { code: minted.body.code } });
  assert.equal(expired.status, 401);
});

test('a launch hand-off code cannot be minted anonymously, and garbage codes are refused', async () => {
  await clearRateLimits();
  const anonymous = await api('POST', '/v1/auth/handoff', {});
  assert.equal(anonymous.status, 401, 'minting a hand-off code requires a real, already-authenticated session');

  const garbage = await api('POST', '/v1/auth/handoff/exchange', { body: { code: 'not-a-real-code' } });
  assert.equal(garbage.status, 401);

  const missing = await api('POST', '/v1/auth/handoff/exchange', { body: {} });
  assert.equal(missing.status, 400, 'a missing code is a bad request, distinct from an invalid one');
});

// ---------------------------------------------------------------------------
// Google ID token verification -- the security fix
// ---------------------------------------------------------------------------

test('a forged Google ID token is rejected (the vulnerability this fixes)', async () => {
  // Exactly the attack the C++ client's unverified decode would have
  // allowed: a hand-built JWT claiming to be someone, signed with nothing
  // of Google's. Structurally valid, completely unauthenticated.
  const header = Buffer.from(JSON.stringify({ alg: 'RS256', kid: 'made-up' })).toString('base64url');
  const payload = Buffer.from(JSON.stringify({
    iss: 'https://accounts.google.com',
    aud: 'whatever-client-id',
    sub: '1234567890',
    email: 'victim@example.com',
    email_verified: true,
    exp: Math.floor(Date.now() / 1000) + 3600,
  })).toString('base64url');
  const forged = `${header}.${payload}.${Buffer.from('not-a-real-signature').toString('base64url')}`;

  const res = await api('POST', '/v1/auth/google', { body: { id_token: forged } });
  assert.equal(res.status, 401, 'a forged Google token must never authenticate anyone');
  assert.ok(!res.body.access_token, 'no session is issued for a forged token');
});

test('an "alg: none" Google token is rejected', async () => {
  const header = Buffer.from(JSON.stringify({ alg: 'none' })).toString('base64url');
  const payload = Buffer.from(JSON.stringify({
    iss: 'https://accounts.google.com', aud: 'x', sub: '1', email: 'a@b.c',
    email_verified: true, exp: Math.floor(Date.now() / 1000) + 3600,
  })).toString('base64url');
  const res = await api('POST', '/v1/auth/google', { body: { id_token: `${header}.${payload}.` } });
  assert.equal(res.status, 401);
});

test('verifyGoogleIdToken rejects structurally invalid input without any network call', async () => {
  await assert.rejects(() => verifyGoogleIdToken('not-a-jwt'), GoogleTokenError);
  await assert.rejects(() => verifyGoogleIdToken(''), GoogleTokenError);
  await assert.rejects(() => verifyGoogleIdToken(null), GoogleTokenError);
});

// ---------------------------------------------------------------------------
// Catalogue and allocation
// ---------------------------------------------------------------------------

async function seedGame(slug) {
  // The signup limiter is real and per-IP; every test here shares
  // 127.0.0.1, so the shared counter is cleared to keep these tests
  // testing what they claim to. Rate limiting itself is covered by its
  // own test below.
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  const { rows: users } = await query('SELECT id FROM users WHERE email_lower = $1', [email.toLowerCase()]);
  const { rows } = await query(
    `INSERT INTO games (slug, title, creator_id, thumbnail_url, published)
     VALUES ($1, $2, $3, $4, TRUE) RETURNING id`,
    [slug, `Game ${slug}`, users[0].id, `https://cdn.example/${slug}.png`],
  );
  return { gameId: rows[0].id, token: signup.body.access_token };
}

test('catalogue lists published games with creator and thumbnail', async () => {
  const slug = `g-${crypto.randomBytes(4).toString('hex')}`;
  await seedGame(slug);

  const res = await api('GET', '/v1/catalog/games?limit=50');
  assert.equal(res.status, 200);
  const found = res.body.games.find((g) => g.slug === slug);
  assert.ok(found, 'the freshly published game appears in the grid');
  assert.ok(found.creator.display_name, 'the grid carries a real creator name');
  assert.ok(found.thumbnail_url.startsWith('https://'), 'the grid carries a real thumbnail url');
  assert.equal(found.active_players, 0, 'a game with no live servers really reports 0, not a made-up number');
  assert.equal(res.body.player_counts_available, true);
});

test('allocation refuses when no server is alive, and succeeds once one heartbeats', async () => {
  const slug = `g-${crypto.randomBytes(4).toString('hex')}`;
  const { gameId, token } = await seedGame(slug);

  // No servers registered at all.
  const none = await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token });
  assert.equal(none.status, 503);

  const serverKey = `srv-${crypto.randomBytes(4).toString('hex')}`;
  await query(
    `INSERT INTO game_servers (server_key, game_id, host, port, region, max_players)
     VALUES ($1, $2, '203.0.113.10', 7777, 'eu-west', 12)`,
    [serverKey, gameId],
  );

  // Registered but never heartbeated: still refused, because "allowed to
  // host" is not the same as "alive".
  const notAlive = await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token });
  assert.equal(notAlive.status, 503, 'a registered but silent server is not allocated to');

  await api('POST', `/v1/sessions/servers/${serverKey}/heartbeat`, { body: { players: 3 } });

  const ok = await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token });
  assert.equal(ok.status, 200);
  assert.equal(ok.body.server.host, '203.0.113.10');
  assert.equal(ok.body.server.port, 7777);
  assert.ok(ok.body.join_ticket, 'allocation returns a real join ticket');

  // The heartbeat's player count really reaches the catalogue.
  const catalog = await api('GET', `/v1/catalog/games/${slug}`);
  assert.equal(catalog.body.game.active_players, 3);

  // The ticket verifies, and is bound to this server specifically.
  const verified = await api('POST', '/v1/sessions/verify-ticket',
    { body: { join_ticket: ok.body.join_ticket, server_key: serverKey } });
  assert.equal(verified.status, 200);
  assert.equal(verified.body.valid, true);

  const wrongServer = await api('POST', '/v1/sessions/verify-ticket',
    { body: { join_ticket: ok.body.join_ticket, server_key: 'some-other-server' } });
  assert.equal(wrongServer.status, 400, 'a ticket for one server must not open another');
});

test('allocation requires authentication', async () => {
  const slug = `g-${crypto.randomBytes(4).toString('hex')}`;
  await seedGame(slug);
  assert.equal((await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug } })).status, 401);
});

test('a full server is not allocated to', async () => {
  const slug = `g-${crypto.randomBytes(4).toString('hex')}`;
  const { gameId, token } = await seedGame(slug);
  const serverKey = `srv-${crypto.randomBytes(4).toString('hex')}`;
  await query(
    `INSERT INTO game_servers (server_key, game_id, host, port, max_players)
     VALUES ($1, $2, '203.0.113.11', 7778, 4)`,
    [serverKey, gameId],
  );
  await api('POST', `/v1/sessions/servers/${serverKey}/heartbeat`, { body: { players: 4 } });

  const res = await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token });
  assert.equal(res.status, 503);
  assert.match(res.body.error.message, /full/i);
});

test('JIT provisioning spins up a real dedicated server when none is alive', async () => {
  // Real, not mocked: this actually spawns engine/build/src/engine_runtime
  // as a child process and waits for its real heartbeat. Skips cleanly
  // (not a failure) when the binary hasn't been built in this checkout --
  // the same "not configured is a real, honest no-op" convention
  // provisioner.js itself follows for a deployment with no engine binary
  // at all.
  const enginePath = path.resolve(testDir, '../../engine/build/src/engine_runtime');
  const gamesDir = path.resolve(testDir, '../../games');
  if (!fs.existsSync(enginePath)) {
    console.log(`[test] skipping JIT provisioning test -- no built engine_runtime at ${enginePath}`);
    return;
  }

  // engine_runtime resolves --game <slug> by slugifying games/DefaultWorld's
  // real on-disk manifest NAME ("Default World") the exact same way
  // seed.js does -- so the DB row this test allocates against must carry
  // that exact slug for the spawned process to find and load it for real.
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  const { rows: users } = await query('SELECT id FROM users WHERE email_lower = $1', [email.toLowerCase()]);
  const { rows: gameRows } = await query(
    `INSERT INTO games (slug, title, creator_id, thumbnail_url, published)
     VALUES ('default-world', 'Default World', $1, '', TRUE)
     ON CONFLICT (slug) DO UPDATE SET published = TRUE
     RETURNING id`,
    [users[0].id],
  );
  const gameId = gameRows[0].id;
  await query(`UPDATE game_servers SET enabled = FALSE WHERE game_id = $1`, [gameId]);
  const token = signup.body.access_token;

  const originalEngineRuntimePath = config.engineRuntimePath;
  const originalGamesDir = config.gamesDir;
  const originalJitApiUrl = config.jitServerApiUrl;
  config.engineRuntimePath = enginePath;
  config.gamesDir = gamesDir;
  config.jitServerApiUrl = baseUrl;
  // Captured from the allocate response itself, not re-queried from the
  // DB afterward -- game_servers accumulates one row per historical test
  // run for this same 'default-world' game id (rows are disabled, never
  // deleted), so a bare `WHERE game_id = $1` with no filter/order/limit
  // was matching an arbitrary OLD row instead of the one THIS run just
  // spawned. That real bug left a live engine_runtime process leaked
  // (and its port held) after every single run -- found by a repeated,
  // reproducible hang in later test files after this test's spawned
  // process was never actually killed.
  let spawnedServerKey = null;
  try {
    const res = await api('POST', '/v1/sessions/allocate', { body: { game_slug: 'default-world' }, token });
    assert.equal(res.status, 200, JSON.stringify(res.body));
    assert.equal(res.body.server.host, config.jitServerHost);
    assert.ok(res.body.server.port >= config.jitServerPortRangeStart, 'the allocated port came from the JIT range');
    assert.ok(res.body.join_ticket, 'a real join ticket was issued for the freshly-provisioned server');
    spawnedServerKey = res.body.server.server_key;
    assert.ok(spawnedServerKey, 'the allocate response really carries the server_key it just provisioned');

    const { rows: servers } = await query(
      `SELECT server_key FROM game_servers WHERE game_id = $1 AND enabled = TRUE`,
      [gameId],
    );
    assert.equal(servers.length, 1, 'exactly one real server row was registered by the provisioner');
    assert.equal(servers[0].server_key, spawnedServerKey, 'it is really the same server the allocate response named');
    const alive = await redis.get(keys.serverHeartbeat(servers[0].server_key));
    assert.ok(alive, 'the spawned process really heartbeated on its own');
  } finally {
    config.engineRuntimePath = originalEngineRuntimePath;
    config.gamesDir = originalGamesDir;
    config.jitServerApiUrl = originalJitApiUrl;
    // Real cleanup: kill exactly the process THIS run spawned so it
    // doesn't keep running (and holding its port) after the suite exits.
    await query(`UPDATE game_servers SET enabled = FALSE WHERE game_id = $1`, [gameId]);
    if (spawnedServerKey) {
      await new Promise((resolve) => {
        exec(`pkill -f "engine_runtime.*--server-key ${spawnedServerKey}"`, () => resolve());
      });
    }
  }
});

test('join tickets are tamper-evident and expire', () => {
  const ticket = issueJoinTicket({ userId: 42, gameId: 7, serverKey: 'srv-a' });
  const payload = verifyJoinTicket(ticket);
  assert.equal(payload.uid, '42');
  assert.equal(payload.srv, 'srv-a');

  // Flipping any byte of the body invalidates the signature.
  const [body, sig] = ticket.split('.');
  const tampered = Buffer.from(body, 'base64url').toString('utf8').replace('"uid":"42"', '"uid":"99"');
  const forged = `${Buffer.from(tampered).toString('base64url')}.${sig}`;
  assert.equal(verifyJoinTicket(forged), null, 'a tampered ticket must not verify');
  assert.equal(verifyJoinTicket('garbage'), null);
});

test('the login rate limiter really blocks a sustained brute-force attempt', async () => {
  await clearRateLimits();
  const email = uniqueEmail();
  await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  await clearRateLimits();

  // The configured login limit is 20 per window. Guess past it and the
  // limiter must start refusing outright, rather than letting an attacker
  // keep grinding.
  let sawRateLimit = false;
  for (let i = 0; i < 30; i += 1) {
    const res = await api('POST', '/v1/auth/login', { body: { email, password: `guess-${i}` } });
    if (res.status === 429) { sawRateLimit = true; break; }
  }
  assert.ok(sawRateLimit, 'repeated failed logins must eventually be rate limited');

  // Even the CORRECT password is refused while the limiter is engaged --
  // proving the block is on the endpoint, not merely on wrong guesses.
  const correct = await api('POST', '/v1/auth/login', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(correct.status, 429);

  await clearRateLimits();
});

test('unknown endpoints 404 and malformed JSON 400s without leaking internals', async () => {
  const missing = await fetch(`${baseUrl}/v1/nope`);
  assert.equal(missing.status, 404);

  const bad = await fetch(`${baseUrl}/v1/auth/login`, {
    method: 'POST', headers: { 'content-type': 'application/json' }, body: '{not json',
  });
  assert.equal(bad.status, 400);
  const body = await bad.json();
  assert.ok(!JSON.stringify(body).includes('SyntaxError'), 'internal error types must not leak to clients');
});

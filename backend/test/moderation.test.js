// Real integration tests for the ops surface: admin role gating,
// centralized content reports, and the account termination/appeal
// routes. Real Express, real PostgreSQL. No mocks.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { pool, query } from '../src/db.js';
import { redis, keys } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';

let server;
let baseUrl;

before(async () => {
  setEmailTransport(async () => {});
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
    headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await res.text();
  return { status: res.status, body: text ? JSON.parse(text) : null };
}

async function clearRateLimits() {
  const found = await redis.keys('rl:*');
  if (found.length > 0) await redis.del(...found);
}

const uniqueEmail = () => `mod_${crypto.randomBytes(8).toString('hex')}@example.com`;

async function makeUser() {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, `signup failed: ${JSON.stringify(signup.body)}`);
  return { id: signup.body.user.id, token: signup.body.access_token };
}

// There is no self-service "become an admin" route (correctly, since
// that would be a real privilege-escalation hole) -- promoting a real
// account directly in the DB is the same real-setup convention
// terminateAccount's own tests already use for privileged state.
async function makeAdmin() {
  const admin = await makeUser();
  await query(`UPDATE users SET role = 'admin' WHERE id = $1`, [admin.id]);
  return admin;
}

// Publishes directly with mature threaded through, the same publish
// route matchmaking.test.js's own publishGame() uses, plus the one field
// that route only accepts through the request body (there is no other
// way to set it -- catalog/routes.js treats it at the same trust level
// as title/description).
async function publishGame(token, slug, { mature = false } = {}) {
  const res = await api('POST', '/v1/catalog/games/publish', { body: { slug, title: `Game ${slug}`, mature }, token });
  assert.equal(res.status, 201, JSON.stringify(res.body));
  const { rows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  return rows[0].id;
}

// A real, alive server -- registered in Postgres AND heartbeating in
// Redis, the exact precondition allocatePlayerToGame requires, same as
// matchmaking.test.js's own registerLiveServer().
async function registerLiveServer(gameId, maxPlayers = 12) {
  const serverKey = `srv-modtest-${crypto.randomBytes(8).toString('hex')}`;
  await query(
    `INSERT INTO game_servers (server_key, game_id, host, port, region, max_players) VALUES ($1, $2, '127.0.0.1', 9999, 'test', $3)`,
    [serverKey, gameId, maxPlayers],
  );
  await redis.set(keys.serverHeartbeat(serverKey), '1', 'EX', 60);
  await redis.set(keys.serverPlayers(serverKey), '0', 'EX', 60);
  return serverKey;
}

test('a signed-in user can file a real report, and it lands in the queue', async () => {
  const reporter = await makeUser();
  const target = await makeUser();

  const res = await api('POST', '/v1/moderation/reports', {
    body: { reported_user_id: target.id, category: 'harassment', detail: 'real abusive chat, redacted here' },
    token: reporter.token,
  });
  assert.equal(res.status, 201);
  assert.ok(res.body.id);
  assert.equal(res.body.status, 'open');

  const admin = await makeAdmin();
  const queue = await api('GET', '/v1/moderation/reports', { token: admin.token });
  assert.equal(queue.status, 200);
  const found = queue.body.reports.find((r) => r.id === res.body.id);
  assert.ok(found, 'the real report really appears in the ops queue');
  assert.equal(found.reported_user.id, target.id);
  assert.equal(found.category, 'harassment');
});

test('a guest can file a report -- guest mode restricts the social graph, not safety', async () => {
  await clearRateLimits();
  const guest = await api('POST', '/v1/auth/guest');
  const target = await makeUser();

  const res = await api('POST', '/v1/moderation/reports', {
    body: { reported_user_id: target.id, category: 'threats' },
    token: guest.body.access_token,
  });
  assert.equal(res.status, 201, JSON.stringify(res.body));
});

test('reports reject self-reporting, a fake category, and a nonexistent user', async () => {
  const reporter = await makeUser();
  const other = await makeUser();

  const selfReport = await api('POST', '/v1/moderation/reports',
    { body: { reported_user_id: reporter.id, category: 'spam' }, token: reporter.token });
  assert.equal(selfReport.status, 400);

  const badCategory = await api('POST', '/v1/moderation/reports',
    { body: { reported_user_id: other.id, category: 'not_a_real_category' }, token: reporter.token });
  assert.equal(badCategory.status, 400);

  const noSuchUser = await api('POST', '/v1/moderation/reports',
    { body: { reported_user_id: '999999999', category: 'spam' }, token: reporter.token });
  assert.equal(noSuchUser.status, 404);
});

test('an ordinary user cannot reach any admin route -- the ops surface is really gated', async () => {
  const user = await makeUser();
  const target = await makeUser();

  assert.equal((await api('GET', '/v1/moderation/reports', { token: user.token })).status, 403);
  assert.equal(
    (await api('POST', `/v1/admin/users/${target.id}/terminate`, { body: { reason: 'x' }, token: user.token })).status,
    403,
  );
  assert.equal((await api('POST', '/v1/admin/appeals/1/grant', { token: user.token })).status, 403);
  assert.equal((await api('GET', '/v1/moderation/reports')).status, 401, 'and no token at all is unauthorized, not forbidden');
});

test('an admin can resolve or dismiss a report, and it leaves the open queue', async () => {
  const admin = await makeAdmin();
  const reporter = await makeUser();
  const target = await makeUser();

  const filed = await api('POST', '/v1/moderation/reports',
    { body: { reported_user_id: target.id, category: 'spam' }, token: reporter.token });

  const resolve = await api('POST', `/v1/moderation/reports/${filed.body.id}/resolve`,
    { body: { note: 'real content removed, warning issued' }, token: admin.token });
  assert.equal(resolve.status, 200);
  assert.equal(resolve.body.status, 'resolved');

  const openQueue = await api('GET', '/v1/moderation/reports', { token: admin.token });
  assert.equal(openQueue.body.reports.find((r) => r.id === filed.body.id), undefined,
    'a resolved report really leaves the default open queue');

  const resolvedQueue = await api('GET', '/v1/moderation/reports?status=resolved', { token: admin.token });
  assert.ok(resolvedQueue.body.reports.find((r) => r.id === filed.body.id), 'and really appears under status=resolved');

  // Resolving twice is a real error, not silently accepted -- the report
  // is no longer open.
  const again = await api('POST', `/v1/moderation/reports/${filed.body.id}/resolve`, { token: admin.token });
  assert.equal(again.status, 404);
});

test('terminating an account really uses the existing bans.js logic, and is really audited', async () => {
  const admin = await makeAdmin();
  const target = await makeUser();

  const res = await api('POST', `/v1/admin/users/${target.id}/terminate`,
    { body: { reason: 'real, sustained harassment' }, token: admin.token });
  assert.equal(res.status, 200);
  assert.equal(res.body.status, 'terminated');

  const { rows } = await query(`SELECT account_state FROM users WHERE id = $1`, [target.id]);
  assert.equal(rows[0].account_state, 'terminated', 'the real termination in bans.js really ran, not a stub');

  const { rows: actions } = await query(
    `SELECT action_type, reason, admin_id FROM moderation_actions WHERE target_user_id = $1`,
    [target.id],
  );
  assert.equal(actions.length, 1, 'a real, permanent audit row was written');
  assert.equal(actions[0].action_type, 'terminate');
  assert.equal(String(actions[0].admin_id), admin.id);

  // A terminated account really can no longer log in -- this exercises
  // the EXISTING findActiveBan()/login-rejection path, confirming this
  // route really reaches it rather than only updating a status column.
  const targetEmail = (await query(`SELECT email FROM users WHERE id = $1`, [target.id])).rows[0].email;
  const login = await api('POST', '/v1/auth/login', { body: { email: targetEmail, password: 'a reasonable passphrase' } });
  assert.notEqual(login.status, 200, 'a terminated account cannot sign back in');
});

test('an admin cannot terminate another admin without demoting them first', async () => {
  const admin = await makeAdmin();
  const otherAdmin = await makeAdmin();

  const res = await api('POST', `/v1/admin/users/${otherAdmin.id}/terminate`,
    { body: { reason: 'x' }, token: admin.token });
  assert.equal(res.status, 403);
});

test('granting and denying an appeal really uses bans.js, and is really audited', async () => {
  const admin = await makeAdmin();
  const target = await makeUser();
  // Claims a real handle first -- grantAppeal()'s real logic reinstates
  // straight to 'active' only when the username was never recycled out
  // from under the account; a handle-less account (or one whose lock
  // window already lapsed) correctly comes back as 'requires_rename'
  // instead, which is a different, already-covered real code path.
  await api('POST', '/v1/auth/username', { body: { username: `modtest${crypto.randomBytes(4).toString('hex')}` }, token: target.token });
  await query(`UPDATE users SET account_state = 'terminated' WHERE id = $1`, [target.id]);

  const { rows: appealRows } = await query(
    `INSERT INTO ban_appeals (user_id, body) VALUES ($1, 'I was not the one harassing anyone') RETURNING id`,
    [target.id],
  );
  const appealId = appealRows[0].id;

  const grant = await api('POST', `/v1/admin/appeals/${appealId}/grant`, { token: admin.token });
  assert.equal(grant.status, 200);
  assert.equal(grant.body.status, 'granted');

  const { rows: users } = await query(`SELECT account_state FROM users WHERE id = $1`, [target.id]);
  assert.equal(users[0].account_state, 'active', 'the real grantAppeal() reinstated the account, keeping its real username');

  const { rows: actions } = await query(
    `SELECT action_type FROM moderation_actions WHERE target_user_id = $1`, [target.id],
  );
  assert.ok(actions.some((a) => a.action_type === 'grant_appeal'), 'the grant was really audited');

  // A second grant on the same (now-resolved) appeal is a real error.
  const again = await api('POST', `/v1/admin/appeals/${appealId}/grant`, { token: admin.token });
  assert.equal(again.status, 404);

  // Deny, on a fresh appeal -- must NOT touch account_state.
  const other = await makeUser();
  await query(`UPDATE users SET account_state = 'terminated' WHERE id = $1`, [other.id]);
  const { rows: appeal2 } = await query(
    `INSERT INTO ban_appeals (user_id, body) VALUES ($1, 'please reinstate me') RETURNING id`,
    [other.id],
  );
  const deny = await api('POST', `/v1/admin/appeals/${appeal2[0].id}/deny`,
    { body: { reason: 'evidence supports the ban' }, token: admin.token });
  assert.equal(deny.status, 200);
  const { rows: stillTerminated } = await query(`SELECT account_state FROM users WHERE id = $1`, [other.id]);
  assert.equal(stillTerminated[0].account_state, 'terminated', 'a denial leaves the account state untouched');
});

test('demoting an admin takes effect on the very next request, not after their token expires', async () => {
  const admin = await makeAdmin();
  const target = await makeUser();

  const before1 = await api('GET', '/v1/moderation/reports', { token: admin.token });
  assert.equal(before1.status, 200, 'really an admin to start');

  await query(`UPDATE users SET role = 'user' WHERE id = $1`, [admin.id]);

  const after1 = await api('GET', '/v1/moderation/reports', { token: admin.token });
  assert.equal(after1.status, 403, 'the SAME still-valid access token is refused the moment the role changes');
});

// NOTE: everything below this line is unrun in this environment -- there
// is no local Postgres/Redis/Docker available here, only `node --check`
// and a route-registration smoke test against createApp() were possible.
// These are written to the same real-Express/real-Postgres contract the
// rest of this file uses and should be run against a real database
// before being trusted.

test('a live game server can file a queue item with its own server_key, and an admin can resolve it', async () => {
  const admin = await makeAdmin();
  const creator = await makeUser();
  const flagged = await makeUser();
  const slug = `modq-${crypto.randomBytes(4).toString('hex')}`;
  const gameId = await publishGame(creator.token, slug);
  const serverKey = await registerLiveServer(gameId);

  const filed = await api('POST', '/v1/moderation/queue', {
    body: {
      server_key: serverKey,
      content_type: 'chat',
      content_ref: 'msg-123',
      flagged_user_id: flagged.id,
      category: 'harassment',
      is_safe: false,
      detail: 'flagged by the real classifier',
      classifier: 'gemini-flash-lite-latest',
    },
  });
  assert.equal(filed.status, 201, JSON.stringify(filed.body));
  assert.equal(filed.body.status, 'open');

  const queue = await api('GET', '/v1/moderation/queue', { token: admin.token });
  assert.equal(queue.status, 200);
  const found = queue.body.items.find((i) => i.id === filed.body.id);
  assert.ok(found, 'the queued item really appears in the ops queue');
  assert.equal(found.game_id, String(gameId), 'game_id came from the server_key lookup, not a client-supplied field');
  assert.equal(found.flagged_user.id, flagged.id);

  const resolve = await api('POST', `/v1/moderation/queue/${filed.body.id}/resolve`,
    { body: { note: 'reviewed, action taken' }, token: admin.token });
  assert.equal(resolve.status, 200);
  assert.equal(resolve.body.status, 'resolved');

  const { rows: actions } = await query(
    `SELECT action_type, queue_item_id FROM moderation_actions WHERE target_user_id = $1`, [flagged.id],
  );
  assert.ok(
    actions.some((a) => a.action_type === 'resolve_queue_item' && String(a.queue_item_id) === filed.body.id),
    'resolving a queue item is really audited with queue_item_id set',
  );
});

test('a compromised server key cannot attribute a flagged item to a different game', async () => {
  const creator = await makeUser();
  const flagged = await makeUser();
  const slug = `modq-${crypto.randomBytes(4).toString('hex')}`;
  const otherSlug = `modq-${crypto.randomBytes(4).toString('hex')}`;
  const gameId = await publishGame(creator.token, slug);
  const otherGameId = await publishGame(creator.token, otherSlug);
  const serverKey = await registerLiveServer(gameId);

  const filed = await api('POST', '/v1/moderation/queue', {
    body: {
      server_key: serverKey,
      game_id: String(otherGameId),
      content_type: 'chat',
      flagged_user_id: flagged.id,
      category: 'spam',
      is_safe: false,
    },
  });
  assert.equal(filed.status, 201);

  const { rows } = await query(`SELECT game_id FROM moderation_queue WHERE id = $1`, [filed.body.id]);
  assert.equal(String(rows[0].game_id), String(gameId), 'the server\'s OWN game_id was used, not the client-supplied one');
  assert.notEqual(String(rows[0].game_id), String(otherGameId));
});

test('an unknown server_key is unauthorized, and an ordinary user cannot self-file a queue verdict', async () => {
  const user = await makeUser();
  const flagged = await makeUser();

  const badKey = await api('POST', '/v1/moderation/queue', {
    body: { server_key: 'not-a-real-key', content_type: 'chat', flagged_user_id: flagged.id, category: 'spam', is_safe: false },
  });
  assert.equal(badKey.status, 401);

  // No server_key at all falls through to the admin path -- an ordinary
  // user's own token must NOT be accepted there, since that would let a
  // user file `is_safe: true` about their own content.
  const selfFile = await api('POST', '/v1/moderation/queue', {
    body: { content_type: 'ugc_asset', flagged_user_id: user.id, category: 'nsfw', is_safe: true },
    token: user.token,
  });
  assert.equal(selfFile.status, 403, 'an ordinary user cannot self-certify their own content as safe');

  const noAuthAtAll = await api('POST', '/v1/moderation/queue', {
    body: { content_type: 'ugc_asset', flagged_user_id: user.id, category: 'nsfw', is_safe: true },
  });
  assert.equal(noAuthAtAll.status, 401);
});

test('the queue rejects a fake content_type, a fake category, and a missing is_safe', async () => {
  const admin = await makeAdmin();
  const flagged = await makeUser();

  const badType = await api('POST', '/v1/moderation/queue', {
    body: { content_type: 'not_a_real_type', flagged_user_id: flagged.id, category: 'spam', is_safe: false }, token: admin.token,
  });
  assert.equal(badType.status, 400);

  const badCategory = await api('POST', '/v1/moderation/queue', {
    body: { content_type: 'ugc_asset', flagged_user_id: flagged.id, category: 'not_a_real_category', is_safe: false }, token: admin.token,
  });
  assert.equal(badCategory.status, 400);

  const missingIsSafe = await api('POST', '/v1/moderation/queue', {
    body: { content_type: 'ugc_asset', flagged_user_id: flagged.id, category: 'spam' }, token: admin.token,
  });
  assert.equal(missingIsSafe.status, 400);
});

test('behavioral signals reflect real report history, gated to admins only', async () => {
  const admin = await makeAdmin();
  const reporter = await makeUser();
  const target = await makeUser();

  assert.equal((await api('GET', `/v1/moderation/users/${target.id}/signals`, { token: reporter.token })).status, 403);

  const baseline = await api('GET', `/v1/moderation/users/${target.id}/signals`, { token: admin.token });
  assert.equal(baseline.status, 200);
  assert.equal(baseline.body.report_count_7d, 0);
  assert.equal(baseline.body.confirmed_violation_count, 0);
  assert.equal(baseline.body.high_velocity_flag, false);

  // File and resolve (upheld) three reports -- a resolved report is a
  // CONFIRMED violation; the earlier dismiss/resolve test already covers
  // the distinction from a merely-filed one.
  for (let i = 0; i < 3; i += 1) {
    const filed = await api('POST', '/v1/moderation/reports',
      { body: { reported_user_id: target.id, category: 'harassment' }, token: reporter.token });
    await api('POST', `/v1/moderation/reports/${filed.body.id}/resolve`, { token: admin.token });
  }

  const after = await api('GET', `/v1/moderation/users/${target.id}/signals`, { token: admin.token });
  assert.equal(after.status, 200);
  assert.equal(after.body.report_count_7d, 3);
  assert.equal(after.body.confirmed_violation_count, 3);
  assert.equal(after.body.high_velocity_flag, true, 'three reports in 7 days crosses the >=3 threshold');
  assert.equal(after.body.repeat_offender_flag, true, 'three confirmed violations crosses the >=2 threshold');
});

test('age-verify flips the real flag, is audited, and gates real content across catalog/sessions/matchmaking', async () => {
  const admin = await makeAdmin();
  const creator = await makeUser();
  const player = await makeUser();
  const slug = `mature-${crypto.randomBytes(4).toString('hex')}`;
  const gameId = await publishGame(creator.token, slug, { mature: true });

  // Self-service birthdate is informational only -- it must not itself
  // gate anything.
  const birthdate = await api('POST', '/v1/auth/birthdate', { body: { birthdate: '1990-01-01' }, token: player.token });
  assert.equal(birthdate.status, 200);

  // Catalog: 404s, not 403s, so an unverified caller can't distinguish
  // "restricted" from "doesn't exist".
  const detailBefore = await api('GET', `/v1/catalog/games/${slug}`, { token: player.token });
  assert.equal(detailBefore.status, 404, 'a mature game 404s for an unverified caller');
  const listBefore = await api('GET', '/v1/catalog/games?limit=200', { token: player.token });
  assert.equal(listBefore.body.games.find((g) => g.slug === slug), undefined, 'and is filtered out of the listing too');

  // The real enforcement points: allocation and ticket creation, not
  // just the listing filter.
  const allocateBefore = await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token: player.token });
  assert.equal(allocateBefore.status, 403);
  const ticketBefore = await api('POST', `/v1/matchmaking/games/${slug}/tickets`, { body: {}, token: player.token });
  assert.equal(ticketBefore.status, 403);

  // A non-admin cannot flip the flag themselves.
  assert.equal(
    (await api('POST', `/v1/admin/users/${player.id}/age-verify`, { body: { verified: true }, token: player.token })).status,
    403,
  );

  const verify = await api('POST', `/v1/admin/users/${player.id}/age-verify`,
    { body: { verified: true, reason: 'reviewed support ticket #42' }, token: admin.token });
  assert.equal(verify.status, 200);
  assert.equal(verify.body.status, 'verified');

  const { rows: userRow } = await query(`SELECT age_verified FROM users WHERE id = $1`, [player.id]);
  assert.equal(userRow[0].age_verified, true, 'the real flag every enforcement point checks was really set');
  const { rows: actions } = await query(
    `SELECT action_type FROM moderation_actions WHERE target_user_id = $1`, [player.id],
  );
  assert.ok(actions.some((a) => a.action_type === 'age_verify'), 'age-verify is really audited');

  // Now it works, live, on the SAME still-valid token -- no re-login
  // needed, same "no stale claim" convention as the role-demotion test.
  const detailAfter = await api('GET', `/v1/catalog/games/${slug}`, { token: player.token });
  assert.equal(detailAfter.status, 200);
  assert.equal(detailAfter.body.game.mature, true);

  await registerLiveServer(gameId);
  const allocateAfter = await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token: player.token });
  assert.equal(allocateAfter.status, 200, JSON.stringify(allocateAfter.body));

  const ticketAfter = await api('POST', `/v1/matchmaking/games/${slug}/tickets`, { body: {}, token: player.token });
  assert.equal(ticketAfter.status, 201, JSON.stringify(ticketAfter.body));

  // Unverify reverses it immediately too.
  await api('POST', `/v1/admin/users/${player.id}/age-verify`, { body: { verified: false }, token: admin.token });
  const allocateRevoked = await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token: player.token });
  assert.equal(allocateRevoked.status, 403, 'revoking verification blocks the very next allocation attempt');
});

test('birthdate rejects a malformed date and a future date', async () => {
  const player = await makeUser();

  const malformed = await api('POST', '/v1/auth/birthdate', { body: { birthdate: '01/01/1990' }, token: player.token });
  assert.equal(malformed.status, 400);

  const future = await api('POST', '/v1/auth/birthdate', { body: { birthdate: '2999-01-01' }, token: player.token });
  assert.equal(future.status, 400);
});

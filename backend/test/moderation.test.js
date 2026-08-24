// Real integration tests for the ops surface: admin role gating,
// centralized content reports, and the account termination/appeal
// routes. Real Express, real PostgreSQL. No mocks.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { pool, query } from '../src/db.js';
import { redis } from '../src/redis.js';
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

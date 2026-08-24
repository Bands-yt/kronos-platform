// Real integration tests for the social graph, guest mode, and the
// ban/username lifecycle. Real Express, real PostgreSQL, real Redis.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { pool, query } from '../src/db.js';
import { redis } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';
import {
  claimUsername,
  grantAppeal,
  isDisposableEmailDomain,
  recycleExpiredUsernames,
  terminateAccount,
} from '../src/moderation/bans.js';

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

const uniqueEmail = () => `s_${crypto.randomBytes(8).toString('hex')}@example.com`;

// Creates a real account with a real username, returning its session.
async function makeUser(usernamePrefix = 'user') {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, `signup failed: ${JSON.stringify(signup.body)}`);
  const username = `${usernamePrefix}${crypto.randomBytes(4).toString('hex')}`;
  const claim = await api('POST', '/v1/auth/username', { body: { username }, token: signup.body.access_token });
  assert.equal(claim.status, 200, `username claim failed: ${JSON.stringify(claim.body)}`);
  return { id: signup.body.user.id, email, username, token: signup.body.access_token };
}

// ---------------------------------------------------------------------------
// Guest mode
// ---------------------------------------------------------------------------

test('a guest account is created with no email and no password', async () => {
  await clearRateLimits();
  const res = await api('POST', '/v1/auth/guest');
  assert.equal(res.status, 201);
  assert.ok(res.body.access_token, 'a guest gets a real usable session');
  assert.equal(res.body.user.email, null, 'a guest really has no email address');
  assert.match(res.body.user.display_name, /^Guest /);
});

test('guests are barred from the friend graph BY THE SERVER, not just the UI', async () => {
  await clearRateLimits();
  const guest = await api('POST', '/v1/auth/guest');
  const target = await makeUser('target');

  const attempt = await api('POST', '/v1/friends/request',
    { body: { user_id: target.id }, token: guest.body.access_token });
  assert.equal(attempt.status, 403);
  assert.match(attempt.body.error.message, /guest/i);

  // ...and a guest is not discoverable either, so nobody can friend them.
  const search = await api('GET', `/v1/users/search?q=${encodeURIComponent('Guest')}`, { token: target.token });
  assert.equal(search.status, 200);
  assert.equal(search.body.results.length, 0, 'guests never appear in user search');
});

// ---------------------------------------------------------------------------
// User search
// ---------------------------------------------------------------------------

test('user search matches case-insensitively and never returns the caller', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');

  const res = await api('GET', `/v1/users/search?q=${bob.username.toUpperCase()}`, { token: alice.token });
  assert.equal(res.status, 200);
  assert.ok(res.body.results.some((r) => r.username === bob.username), 'case-insensitive match works');
  assert.equal(res.body.results.some((r) => r.id === alice.id), false, 'the caller never appears in their own search');
  assert.equal(res.body.results[0].relationship, 'none');
});

test('user search refuses a too-short query and cannot be used to scrape', async () => {
  const alice = await makeUser('alice');
  assert.equal((await api('GET', '/v1/users/search?q=a', { token: alice.token })).status, 400);
  // A wildcard must be escaped, not honoured.
  const wildcard = await api('GET', '/v1/users/search?q=%25%25%25', { token: alice.token });
  assert.equal(wildcard.status, 200);
  assert.equal(wildcard.body.results.length, 0, 'a LIKE wildcard must not match every user');
});

// ---------------------------------------------------------------------------
// Friend requests
// ---------------------------------------------------------------------------

test('a full request/accept cycle works and is visible from both sides', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');

  assert.equal((await api('POST', '/v1/friends/request',
    { body: { user_id: bob.id }, token: alice.token })).status, 201);

  // Bob sees it as incoming, Alice as outgoing.
  const bobList = await api('GET', '/v1/friends/list', { token: bob.token });
  assert.equal(bobList.body.incoming_requests.length, 1);
  assert.equal(bobList.body.incoming_requests[0].username, alice.username);

  const aliceList = await api('GET', '/v1/friends/list', { token: alice.token });
  assert.equal(aliceList.body.outgoing_requests.length, 1);

  // The REQUESTER must not be able to accept their own request.
  const selfAccept = await api('POST', '/v1/friends/respond',
    { body: { user_id: bob.id, accept: true }, token: alice.token });
  assert.equal(selfAccept.status, 404, 'only the addressee may accept');

  assert.equal((await api('POST', '/v1/friends/respond',
    { body: { user_id: alice.id, accept: true }, token: bob.token })).status, 200);

  for (const who of [alice, bob]) {
    const list = await api('GET', '/v1/friends/list', { token: who.token });
    assert.equal(list.body.friends.length, 1, 'the friendship is visible from both sides');
  }
});

test('duplicate and self requests are rejected', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');

  assert.equal((await api('POST', '/v1/friends/request',
    { body: { user_id: alice.id }, token: alice.token })).status, 400, 'cannot befriend yourself');

  await api('POST', '/v1/friends/request', { body: { user_id: bob.id }, token: alice.token });
  const dup = await api('POST', '/v1/friends/request', { body: { user_id: bob.id }, token: alice.token });
  assert.equal(dup.status, 409, 'a second identical request conflicts');

  // Bob requesting back is treated as accepting, which is what he means.
  const reverse = await api('POST', '/v1/friends/request', { body: { user_id: alice.id }, token: bob.token });
  assert.equal(reverse.status, 200);
  assert.equal(reverse.body.status, 'accepted');
});

test('a blocked pair cannot be re-opened, and does not reveal that it is blocked', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');
  await query(
    `INSERT INTO friendships (requester_id, addressee_id, status) VALUES ($1, $2, 'blocked')`,
    [bob.id, alice.id],
  );
  const res = await api('POST', '/v1/friends/request', { body: { user_id: bob.id }, token: alice.token });
  assert.equal(res.status, 404, 'a block reads as "no such user", never as "you are blocked"');
});

test('removing a friend works and is not silently a no-op', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');
  await api('POST', '/v1/friends/request', { body: { user_id: bob.id }, token: alice.token });
  await api('POST', '/v1/friends/respond', { body: { user_id: alice.id, accept: true }, token: bob.token });

  assert.equal((await api('DELETE', `/v1/friends/${bob.id}`, { token: alice.token })).status, 204);
  assert.equal((await api('GET', '/v1/friends/list', { token: alice.token })).body.friends.length, 0);
  assert.equal((await api('DELETE', `/v1/friends/${bob.id}`, { token: alice.token })).status, 404,
    'removing a non-friend reports honestly rather than pretending it worked');
});

// ---------------------------------------------------------------------------
// Presence + direct join
// ---------------------------------------------------------------------------

test('presence heartbeat drives the friends list, and a join ticket is minted only for in-game friends', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');
  await api('POST', '/v1/friends/request', { body: { user_id: bob.id }, token: alice.token });
  await api('POST', '/v1/friends/respond', { body: { user_id: alice.id, accept: true }, token: bob.token });

  // No heartbeat yet: offline, and no ticket.
  let list = await api('GET', '/v1/friends/list', { token: alice.token });
  assert.equal(list.body.friends[0].status, 'offline');
  assert.equal(list.body.friends[0].join_ticket, null, 'no ticket is minted for an offline friend');

  // Online in the launcher: still no ticket, because there is nothing to join.
  await api('POST', '/v1/presence/heartbeat', { body: { status: 'online_launcher' }, token: bob.token });
  list = await api('GET', '/v1/friends/list', { token: alice.token });
  assert.equal(list.body.friends[0].status, 'online_launcher');
  assert.equal(list.body.friends[0].join_ticket, null);

  // In game: now a real ticket bound to the server Bob is actually on.
  await api('POST', '/v1/presence/heartbeat',
    { body: { status: 'in_game', current_game_id: '1', current_server_id: 'srv-eu-1' }, token: bob.token });
  list = await api('GET', '/v1/friends/list', { token: alice.token });
  assert.equal(list.body.friends[0].status, 'in_game');
  assert.equal(list.body.friends[0].current_server_id, 'srv-eu-1');
  assert.ok(list.body.friends[0].join_ticket, 'an in-game friend yields a real direct-join ticket');
  assert.equal(list.body.presence_available, true);
});

test('presence expires on its own when heartbeats stop', async () => {
  const alice = await makeUser('alice');
  await api('POST', '/v1/presence/heartbeat', { body: { status: 'online_launcher' }, token: alice.token });
  const ttl = await redis.ttl(`presence:${alice.id}`);
  assert.ok(ttl > 0 && ttl <= 40, `presence carries a real TTL (got ${ttl})`);
});

// ---------------------------------------------------------------------------
// Follow (one-way, no consent needed)
// ---------------------------------------------------------------------------

test('following is one-way, needs no acceptance, and is idempotent', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');

  const follow = await api('POST', `/v1/follows/${bob.id}`, { token: alice.token });
  assert.equal(follow.status, 200);
  assert.equal(follow.body.status, 'following');

  // Following again is a real, honest no-op -- not a 409 -- so a client's
  // Follow button never needs its own local double-click guard.
  const followAgain = await api('POST', `/v1/follows/${bob.id}`, { token: alice.token });
  assert.equal(followAgain.status, 200);

  // Bob never had to accept anything, and does NOT automatically follow
  // alice back -- this is a one-way edge, not a second friendship.
  const bobFollowing = await api('GET', `/v1/follows/${bob.id}/following`, { token: bob.token });
  assert.equal(bobFollowing.status, 200);
  assert.equal(bobFollowing.body.following.find((u) => u.id === alice.id), undefined,
    'bob does not automatically follow alice back');

  const aliceFollowing = await api('GET', `/v1/follows/${alice.id}/following`, { token: alice.token });
  assert.ok(aliceFollowing.body.following.some((u) => u.id === bob.id), 'alice really follows bob');

  const bobFollowers = await api('GET', `/v1/follows/${bob.id}/followers`, { token: bob.token });
  const aliceRow = bobFollowers.body.followers.find((u) => u.id === alice.id);
  assert.ok(aliceRow, 'alice appears in bob\'s real followers list');
  // The viewer here is bob (his own token); bob does not follow alice
  // back, so this must read false, not just be present/truthy.
  assert.equal(aliceRow.viewer_is_following, false, 'the viewer flag reflects the VIEWER\'s own edge, not the row\'s');

  const counts = await api('GET', `/v1/follows/${bob.id}/counts`, { token: bob.token });
  assert.equal(counts.status, 200);
  assert.equal(counts.body.followers, 1);
  assert.equal(counts.body.following, 0);
});

test('self-follow and following a nonexistent/guest account are rejected', async () => {
  const alice = await makeUser('alice');
  const guest = await api('POST', '/v1/auth/guest');

  const selfFollow = await api('POST', `/v1/follows/${alice.id}`, { token: alice.token });
  assert.equal(selfFollow.status, 400);

  const nobody = await api('POST', '/v1/follows/999999999', { token: alice.token });
  assert.equal(nobody.status, 404);

  const followGuest = await api('POST', `/v1/follows/${guest.body.user.id}`, { token: alice.token });
  assert.equal(followGuest.status, 400);
});

test('guests cannot follow anyone, but their own follow endpoints still require auth', async () => {
  const guest = await api('POST', '/v1/auth/guest');
  const target = await makeUser('target');

  const attempt = await api('POST', `/v1/follows/${target.id}`, { token: guest.body.access_token });
  assert.equal(attempt.status, 403);
  assert.match(attempt.body.error.message, /guest/i);

  const noAuth = await api('POST', `/v1/follows/${target.id}`);
  assert.equal(noAuth.status, 401);
});

test('unfollowing is not silently a no-op', async () => {
  const alice = await makeUser('alice');
  const bob = await makeUser('bob');

  const notFollowing = await api('DELETE', `/v1/follows/${bob.id}`, { token: alice.token });
  assert.equal(notFollowing.status, 404, 'unfollowing someone you never followed is a real, reportable mistake');

  await api('POST', `/v1/follows/${bob.id}`, { token: alice.token });
  const unfollow = await api('DELETE', `/v1/follows/${bob.id}`, { token: alice.token });
  assert.equal(unfollow.status, 204);

  const following = await api('GET', `/v1/follows/${alice.id}/following`, { token: alice.token });
  assert.equal(following.body.following.find((u) => u.id === bob.id), undefined, 'bob is really gone from the list');
});

// ---------------------------------------------------------------------------
// Ban / disposable email / username lifecycle
// ---------------------------------------------------------------------------

test('disposable email domains are detected, including subdomains', () => {
  assert.equal(isDisposableEmailDomain('a@mailinator.com'), true);
  assert.equal(isDisposableEmailDomain('a@sub.mailinator.com'), true);
  assert.equal(isDisposableEmailDomain('a@GUERRILLAMAIL.COM'), true);
  assert.equal(isDisposableEmailDomain('a@gmail.com'), false);
  assert.equal(isDisposableEmailDomain('not-an-email'), false);
});

test('signup rejects a disposable email address', async () => {
  await clearRateLimits();
  const res = await api('POST', '/v1/auth/signup',
    { body: { email: `x${Date.now()}@mailinator.com`, password: 'a reasonable passphrase' } });
  assert.equal(res.status, 400);
  assert.match(res.body.error.message, /disposable/i);
});

test('a terminated account is banned by email AND by device, and cannot re-register either way', async () => {
  const victim = await makeUser('banned');
  await terminateAccount(victim.id, { reason: 'test', hwid: 'HW-TEST-1234', ip: '203.0.113.99' });

  await clearRateLimits();
  const sameEmail = await api('POST', '/v1/auth/signup',
    { body: { email: victim.email, password: 'a reasonable passphrase' } });
  assert.equal(sameEmail.status, 403, 'the banned email cannot register again');

  await clearRateLimits();
  const newEmailSameDevice = await api('POST', '/v1/auth/signup',
    { body: { email: uniqueEmail(), password: 'a reasonable passphrase', hwid: 'HW-TEST-1234' } });
  assert.equal(newEmailSameDevice.status, 403,
    'changing only the email does not evade the ban -- the device is flagged too');

  await clearRateLimits();
  const unrelated = await api('POST', '/v1/auth/signup',
    { body: { email: uniqueEmail(), password: 'a reasonable passphrase', hwid: 'HW-DIFFERENT' } });
  assert.equal(unrelated.status, 201, 'an unrelated device is unaffected');
});

test('a terminated username is locked for 30 days, then recycled', async () => {
  const victim = await makeUser('lockme');
  await terminateAccount(victim.id, { reason: 'test' });

  const locked = await query(`SELECT username, username_locked_until FROM users WHERE id = $1`, [victim.id]);
  assert.equal(locked.rows[0].username, victim.username, 'the handle is held, not immediately freed');
  const daysHeld = (new Date(locked.rows[0].username_locked_until) - Date.now()) / 86400000;
  assert.ok(daysHeld > 29 && daysHeld < 31, `held for ~30 days (got ${daysHeld.toFixed(1)})`);

  // Nobody else can take it while it is locked.
  const other = await makeUser('other');
  const steal = await claimUsername(other.id, victim.username);
  assert.equal(steal.ok, false, 'a locked handle cannot be claimed by somebody else');

  // Fast-forward the lock, then recycle.
  await query(`UPDATE users SET username_locked_until = NOW() - INTERVAL '1 day' WHERE id = $1`, [victim.id]);
  const recycled = await recycleExpiredUsernames();
  assert.ok(recycled.includes(String(victim.id)), 'the expired handle is released');

  const after = await query(`SELECT username, username_lower FROM users WHERE id = $1`, [victim.id]);
  assert.equal(after.rows[0].username, null, 'the handle really returns to the public pool');

  // And now it really is claimable.
  assert.equal((await claimUsername(other.id, victim.username)).ok, true);
});

test('an appeal granted INSIDE the lock window keeps the username', async () => {
  const victim = await makeUser('appeal');
  await terminateAccount(victim.id, { reason: 'test' });
  const { rows } = await query(
    `INSERT INTO ban_appeals (user_id, body) VALUES ($1, 'please') RETURNING id`, [victim.id]);

  const result = await grantAppeal(rows[0].id);
  assert.equal(result.state, 'active');
  assert.equal(result.keptUsername, true);

  const after = await query(`SELECT username, account_state FROM users WHERE id = $1`, [victim.id]);
  assert.equal(after.rows[0].username, victim.username, 'the handle is restored intact');
  assert.equal(after.rows[0].account_state, 'active');

  // The ban identifiers are lifted too, so they can log in again.
  await clearRateLimits();
  const login = await api('POST', '/v1/auth/login',
    { body: { email: victim.email, password: 'a reasonable passphrase' } });
  assert.equal(login.status, 200, 'a reinstated user can really log in again');
});

test('an appeal granted AFTER the handle was recycled reinstates the account as REQUIRES_RENAME', async () => {
  const victim = await makeUser('late');
  const originalUsername = victim.username;
  await terminateAccount(victim.id, { reason: 'test' });

  // Time passes; the handle is recycled and somebody else takes it.
  await query(`UPDATE users SET username_locked_until = NOW() - INTERVAL '1 day' WHERE id = $1`, [victim.id]);
  await recycleExpiredUsernames();
  const squatter = await makeUser('squatter');
  assert.equal((await claimUsername(squatter.id, originalUsername)).ok, true);

  const { rows } = await query(
    `INSERT INTO ban_appeals (user_id, body) VALUES ($1, 'late appeal') RETURNING id`, [victim.id]);
  const result = await grantAppeal(rows[0].id);

  assert.equal(result.keptUsername, false);
  assert.equal(result.state, 'requires_rename',
    'the account comes back with its data but must choose a new handle -- we cannot take one off somebody else');

  // The squatter keeps it.
  const held = await query(`SELECT id FROM users WHERE username_lower = LOWER($1)`, [originalUsername]);
  assert.equal(String(held.rows[0].id), String(squatter.id));

  // Claiming a new username clears the flag.
  const fresh = `renamed${crypto.randomBytes(4).toString('hex')}`;
  assert.equal((await claimUsername(victim.id, fresh)).ok, true);
  const after = await query(`SELECT account_state FROM users WHERE id = $1`, [victim.id]);
  assert.equal(after.rows[0].account_state, 'active', 'picking a new handle returns the account to normal');
});

test('username recycling is idempotent, which is what a cron job needs', async () => {
  const first = await recycleExpiredUsernames();
  const second = await recycleExpiredUsernames();
  assert.deepEqual(second, [], 'a second run releases nothing extra');
  assert.ok(Array.isArray(first));
});

test('username validation rejects malformed handles', async () => {
  const user = await makeUser('valid');
  assert.equal((await claimUsername(user.id, 'ab')).ok, false, 'too short');
  assert.equal((await claimUsername(user.id, 'a'.repeat(21))).ok, false, 'too long');
  assert.equal((await claimUsername(user.id, 'has space')).ok, false, 'no spaces');
  assert.equal((await claimUsername(user.id, 'bad!char')).ok, false, 'no punctuation');
  // Unique per run: this suite shares one database with the others, so a
  // fixed handle would collide with a previous run and fail for a reason
  // that has nothing to do with validation.
  assert.equal((await claimUsername(user.id, `Good_Name${crypto.randomBytes(3).toString('hex')}`)).ok, true);
});

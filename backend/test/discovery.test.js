// Pagination, the account directory, presence states, and publishing.
// Real Express, real PostgreSQL, real Redis.
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

async function makeUser(prefix = 'disc') {
  await clearRateLimits();
  const email = `${prefix}_${crypto.randomBytes(8).toString('hex')}@example.com`;
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, JSON.stringify(signup.body));
  const username = `${prefix}${crypto.randomBytes(4).toString('hex')}`;
  await api('POST', '/v1/auth/username', { body: { username }, token: signup.body.access_token });
  return { id: signup.body.user.id, username, token: signup.body.access_token };
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

test('the games catalogue accepts batches up to 200 and clamps beyond that', async () => {
  const res = await api('GET', '/v1/catalog/games?limit=200');
  assert.equal(res.status, 200);
  assert.ok(Array.isArray(res.body.games));

  // Over the cap must clamp, not error and not honour it -- an unbounded
  // limit is how one request pulls the whole table.
  const over = await api('GET', '/v1/catalog/games?limit=100000');
  assert.equal(over.status, 200);
  assert.ok(over.body.games.length <= 200, 'a limit beyond the cap is really clamped to 200');
});

test('keyset pagination walks the catalogue without repeating or skipping', async () => {
  const user = await makeUser('pager');
  // Enough games to need several pages at limit=2.
  for (let i = 0; i < 5; i += 1) {
    await clearRateLimits();
    const slug = `pg-${crypto.randomBytes(4).toString('hex')}`;
    const res = await api('POST', '/v1/catalog/games/publish',
      { body: { slug, title: `Paged ${i}` }, token: user.token });
    assert.equal(res.status, 201, JSON.stringify(res.body));
  }

  const seen = [];
  let cursor = null;
  for (let page = 0; page < 20; page += 1) {
    const path = `/v1/catalog/games?limit=2${cursor ? `&cursor=${cursor}` : ''}`;
    const res = await api('GET', path);
    assert.equal(res.status, 200);
    for (const g of res.body.games) seen.push(g.id);
    cursor = res.body.next_cursor;
    if (!cursor) break;
  }

  assert.equal(new Set(seen).size, seen.length, 'paging never returns the same game twice');
  assert.ok(seen.length >= 5, 'paging really walked the whole catalogue');
});

// ---------------------------------------------------------------------------
// Account directory + presence
// ---------------------------------------------------------------------------

test('the account directory paginates and reports live presence', async () => {
  const viewer = await makeUser('dir');
  const other = await makeUser('dir');
  await api('POST', '/v1/presence/heartbeat', { body: { status: 'in_studio' }, token: other.token });

  const res = await api('GET', '/v1/users?limit=200', { token: viewer.token });
  assert.equal(res.status, 200);
  assert.equal(res.body.presence_available, true);

  const found = res.body.users.find((u) => u.id === other.id);
  assert.ok(found, 'a real registered account really appears in the directory');
  assert.equal(found.status, 'in_studio', 'the directory really reflects live presence');

  const capped = await api('GET', '/v1/users?limit=99999', { token: viewer.token });
  assert.ok(capped.body.users.length <= 200, 'the directory limit is really capped at 200');
});

test('the directory excludes guests and requires authentication', async () => {
  const viewer = await makeUser('dir');
  await clearRateLimits();
  const guest = await api('POST', '/v1/auth/guest');

  const res = await api('GET', '/v1/users?limit=200', { token: viewer.token });
  assert.equal(res.body.users.some((u) => u.id === guest.body.user.id), false,
    'guests really never appear in the account directory');

  assert.equal((await api('GET', '/v1/users')).status, 401, 'the directory really requires auth');
});

test('in_studio is a real distinct presence state, and junk falls back safely', async () => {
  const user = await makeUser('pres');

  for (const status of ['online_launcher', 'in_studio', 'in_game']) {
    const beat = await api('POST', '/v1/presence/heartbeat', { body: { status }, token: user.token });
    assert.equal(beat.status, 200);
    const raw = JSON.parse(await redis.get(`presence:${user.id}`));
    assert.equal(raw.status, status, `${status} is really stored as itself`);
  }

  // An unknown state must not be stored verbatim -- clients would then
  // have to handle arbitrary strings from other clients.
  await api('POST', '/v1/presence/heartbeat', { body: { status: 'wizard-mode' }, token: user.token });
  const raw = JSON.parse(await redis.get(`presence:${user.id}`));
  assert.equal(raw.status, 'online_launcher', 'an unrecognised presence state really falls back safely');
});

test('the presence summary counts only live entries', async () => {
  const user = await makeUser('sum');
  await api('POST', '/v1/presence/heartbeat', { body: { status: 'in_studio' }, token: user.token });

  const res = await api('GET', '/v1/presence/summary', { token: user.token });
  assert.equal(res.status, 200);
  assert.equal(res.body.available, true);
  assert.ok(res.body.in_studio >= 1, 'the summary really counts the in-studio user');
  assert.equal(res.body.total_online, res.body.online_launcher + res.body.in_studio + res.body.in_game,
    'total_online is really the sum of the live states');
  assert.ok(res.body.registered_accounts >= 1);

  // Dropping the key must drop the count -- no stale "online" lingering.
  await redis.del(`presence:${user.id}`);
  const after = await api('GET', '/v1/presence/summary', { token: user.token });
  assert.ok(after.body.in_studio < res.body.in_studio || after.body.in_studio === 0,
    'an expired presence key really stops being counted');
});

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------

test('publishing registers a game in the public catalogue immediately', async () => {
  const creator = await makeUser('pub');
  const slug = `pub-${crypto.randomBytes(4).toString('hex')}`;

  const res = await api('POST', '/v1/catalog/games/publish', {
    body: { slug, title: 'My Published Place', description: 'Made in Studio.',
            thumbnail_url: 'https://cdn.example/t.png',
            scene_sha256: crypto.createHash('sha256').update('scene').digest('hex') },
    token: creator.token,
  });
  assert.equal(res.status, 201, JSON.stringify(res.body));
  assert.equal(res.body.status, 'published');

  // Immediately visible to an anonymous browser.
  const detail = await api('GET', `/v1/catalog/games/${slug}`);
  assert.equal(detail.status, 200);
  assert.equal(detail.body.game.title, 'My Published Place');
  assert.equal(detail.body.game.creator.display_name.length > 0, true);
});

test('re-publishing your own slug updates it; somebody else\'s is refused', async () => {
  const owner = await makeUser('own');
  const stranger = await makeUser('str');
  const slug = `own-${crypto.randomBytes(4).toString('hex')}`;

  assert.equal((await api('POST', '/v1/catalog/games/publish',
    { body: { slug, title: 'First' }, token: owner.token })).status, 201);

  await clearRateLimits();
  const updated = await api('POST', '/v1/catalog/games/publish',
    { body: { slug, title: 'Second' }, token: owner.token });
  assert.equal(updated.status, 200);
  assert.equal(updated.body.status, 'updated', 'the owner really updates in place');
  assert.equal((await api('GET', `/v1/catalog/games/${slug}`)).body.game.title, 'Second');

  await clearRateLimits();
  const hijack = await api('POST', '/v1/catalog/games/publish',
    { body: { slug, title: 'Mine now' }, token: stranger.token });
  assert.equal(hijack.status, 409, 'somebody else really cannot overwrite your published place');
  assert.equal((await api('GET', `/v1/catalog/games/${slug}`)).body.game.title, 'Second',
    'and the real place is genuinely untouched');
});

test('publishing validates its input and refuses guests', async () => {
  const creator = await makeUser('val');

  const cases = [
    [{ slug: 'ab', title: 'x' }, 'slug too short'],
    [{ slug: 'Has Capitals', title: 'x' }, 'slug with capitals and spaces'],
    [{ slug: 'ok-slug', title: '' }, 'empty title'],
    [{ slug: 'ok-slug', title: 'x', thumbnail_url: 'javascript:alert(1)' }, 'javascript: thumbnail'],
    [{ slug: 'ok-slug', title: 'x', scene_sha256: 'not-a-hash' }, 'malformed scene hash'],
  ];
  for (const [body, what] of cases) {
    await clearRateLimits();
    const res = await api('POST', '/v1/catalog/games/publish', { body, token: creator.token });
    assert.equal(res.status, 400, `${what} is really rejected`);
  }

  await clearRateLimits();
  const guest = await api('POST', '/v1/auth/guest');
  const guestPublish = await api('POST', '/v1/catalog/games/publish',
    { body: { slug: `g-${crypto.randomBytes(4).toString('hex')}`, title: 'Guest place' },
      token: guest.body.access_token });
  assert.equal(guestPublish.status, 403, 'guests really cannot publish');
});

test('publishing requires authentication', async () => {
  const res = await api('POST', '/v1/catalog/games/publish', { body: { slug: 'anon-place', title: 'x' } });
  assert.equal(res.status, 401);
});

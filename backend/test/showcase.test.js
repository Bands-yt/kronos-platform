// Real integration tests for the showcase portal: submissions stay
// pending until an admin features them, only the real owner can submit
// their own game, and the kronos:// deep link is server-constructed.
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

const uniqueEmail = () => `showcase_${crypto.randomBytes(8).toString('hex')}@example.com`;

async function makeUser() {
  await clearRateLimits();
  const signup = await api('POST', '/v1/auth/signup', { body: { email: uniqueEmail(), password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, JSON.stringify(signup.body));
  return { id: signup.body.user.id, token: signup.body.access_token };
}

async function publishGame(token, slug) {
  const res = await api('POST', '/v1/catalog/games/publish', { body: { slug, title: `Game ${slug}` }, token });
  assert.equal(res.status, 201, JSON.stringify(res.body));
}

test('a submission is pending and invisible until an admin features it', async () => {
  const creator = await makeUser();
  const admin = await makeUser();
  const slug = `showcase-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const submit = await api('POST', '/v1/showcase/submit', {
    body: { slug, category: 'games', title: 'My cool world', description: 'a real showcase entry' },
    token: creator.token,
  });
  assert.equal(submit.status, 201, JSON.stringify(submit.body));
  assert.equal(submit.body.featured, false);

  const beforeFeature = await api('GET', `/v1/showcase?category=games`);
  assert.equal(beforeFeature.status, 200);
  assert.ok(!beforeFeature.body.entries.some((e) => e.id === submit.body.id), 'a pending submission is not publicly visible yet');

  const strangerFeature = await api('POST', `/v1/showcase/${submit.body.id}/feature`, { token: creator.token });
  assert.equal(strangerFeature.status, 403, 'only an admin can feature a submission');

  await query(`UPDATE users SET role = 'admin' WHERE id = $1`, [admin.id]);
  const featured = await api('POST', `/v1/showcase/${submit.body.id}/feature`, { token: admin.token });
  assert.equal(featured.status, 200, JSON.stringify(featured.body));
  assert.ok(featured.body.featured_at);

  const afterFeature = await api('GET', `/v1/showcase?category=games`);
  assert.equal(afterFeature.status, 200);
  const entry = afterFeature.body.entries.find((e) => e.id === submit.body.id);
  assert.ok(entry, 'a featured submission is really publicly visible now');
  assert.equal(entry.kronos_uri, `kronos://launch?game=${slug}&ref=showcase`, 'the deep link is server-constructed, matching the real launch URI format');
  assert.equal(entry.game.slug, slug);
  assert.equal(entry.author.id, String(creator.id));
});

test('a stranger cannot submit someone else\'s game, and an unknown category is refused', async () => {
  const owner = await makeUser();
  const stranger = await makeUser();
  const slug = `showcase-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(owner.token, slug);

  const stolenSubmit = await api('POST', '/v1/showcase/submit', { body: { slug, category: 'games', title: 'Not mine' }, token: stranger.token });
  assert.equal(stolenSubmit.status, 403);

  const badCategory = await api('POST', '/v1/showcase/submit', { body: { slug, category: 'not-a-real-category', title: 'x' }, token: owner.token });
  assert.equal(badCategory.status, 400);

  const badCategoryFilter = await api('GET', '/v1/showcase?category=not-a-real-category');
  assert.equal(badCategoryFilter.status, 400);
});

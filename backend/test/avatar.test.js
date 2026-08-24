// Real integration tests for backend avatar-config persistence. Real
// Express, real PostgreSQL. No mocks.
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

const uniqueEmail = () => `av_${crypto.randomBytes(8).toString('hex')}@example.com`;

async function makeUser() {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, `signup failed: ${JSON.stringify(signup.body)}`);
  return { id: signup.body.user.id, token: signup.body.access_token };
}

test('a fresh account has no saved config, and reads back the real default starter look', async () => {
  const alice = await makeUser();
  const res = await api('GET', '/v1/avatar/me', { token: alice.token });
  assert.equal(res.status, 200);
  assert.equal(res.body.skin_tone_index, -1, 'never-chosen skin tone -- the client falls back to the real default');
  assert.equal(res.body.head_shape_index, 0);
  assert.equal(res.body.body_height, 1.0);
  assert.deepEqual(res.body.equipped_items, {}, 'a fresh avatar has nothing equipped, same as the real client default');
});

test('saving and re-fetching an avatar config round-trips for real', async () => {
  const alice = await makeUser();
  const config = {
    skin_tone_index: 4,
    head_shape_index: 1,
    body_height: 1.1,
    body_width: 0.9,
    body_limb_scale: 1.05,
    body_torso_length: 0.95,
    body_shoulder_width: 1.02,
    clothing_fit_index: 1,
    equipped_items: { Hair: 'itm-bacon-hair-01', Torso: 'itm-red-shirt', Accessory: 'itm-cool-shades' },
  };
  const put = await api('PUT', '/v1/avatar/me', { body: config, token: alice.token });
  assert.equal(put.status, 200);
  assert.deepEqual(put.body, config);

  const get = await api('GET', '/v1/avatar/me', { token: alice.token });
  assert.equal(get.status, 200);
  assert.deepEqual(get.body, config, 'the saved config really persisted, not just echoed back');

  // Saving again (an equip/unequip in practice) really overwrites, not appends.
  const secondPut = await api('PUT', '/v1/avatar/me', {
    body: { ...config, equipped_items: { Hair: 'itm-bacon-hair-01' } },
    token: alice.token,
  });
  assert.deepEqual(secondPut.body.equipped_items, { Hair: 'itm-bacon-hair-01' }, 'unequipping Torso/Accessory really removed them, not merged');
});

test('body sliders and indices are clamped server-side, not trusted from the client', async () => {
  const alice = await makeUser();
  const res = await api('PUT', '/v1/avatar/me', {
    body: { body_height: 99, body_width: -5, skin_tone_index: 9999, clothing_fit_index: 47 },
    token: alice.token,
  });
  assert.equal(res.status, 200);
  assert.equal(res.body.body_height, 1.15, 'an out-of-range body slider is clamped, not accepted verbatim');
  assert.equal(res.body.body_width, 0.85);
  assert.equal(res.body.skin_tone_index, 63);
  assert.equal(res.body.clothing_fit_index, 1);
});

test('equipped_items rejects an unknown category and a non-string item id', async () => {
  const alice = await makeUser();
  const badCategory = await api('PUT', '/v1/avatar/me', {
    body: { equipped_items: { NotARealCategory: 'itm-x' } },
    token: alice.token,
  });
  assert.equal(badCategory.status, 400);

  const badValue = await api('PUT', '/v1/avatar/me', {
    body: { equipped_items: { Hair: 12345 } },
    token: alice.token,
  });
  assert.equal(badValue.status, 400);
});

test('a signed-in user can view another real user\'s avatar config, defaulted or saved', async () => {
  const alice = await makeUser();
  const bob = await makeUser();
  await api('PUT', '/v1/avatar/me', { body: { skin_tone_index: 7 }, token: bob.token });

  const viewBob = await api('GET', `/v1/avatar/${bob.id}`, { token: alice.token });
  assert.equal(viewBob.status, 200);
  assert.equal(viewBob.body.skin_tone_index, 7, 'alice really sees bob\'s real saved config');

  const viewNobody = await api('GET', '/v1/avatar/999999999', { token: alice.token });
  assert.equal(viewNobody.status, 404);
});

test('avatar endpoints require authentication', async () => {
  assert.equal((await api('GET', '/v1/avatar/me')).status, 401);
  assert.equal((await api('PUT', '/v1/avatar/me', { body: {} })).status, 401);
});

test('the account_state <> terminated guard applies, matching every other real user lookup', async () => {
  const alice = await makeUser();
  const { rows } = await query(
    `INSERT INTO users (email, email_lower, password_hash, display_name, account_state)
     VALUES ($1, $1, 'x', 'Gone', 'terminated') RETURNING id`,
    [uniqueEmail()],
  );
  const res = await api('GET', `/v1/avatar/${rows[0].id}`, { token: alice.token });
  assert.equal(res.status, 404);
});

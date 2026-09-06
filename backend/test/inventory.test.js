// Real integration tests for real-time player inventories: creator-
// managed item catalog, server-authoritative grant/consume, and the
// player-facing read. Real Express, real PostgreSQL, real Redis.
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

const uniqueEmail = () => `inventory_${crypto.randomBytes(8).toString('hex')}@example.com`;

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

// No public "register a server" endpoint exists (see sessions/
// provisioner.js's own comment) -- a real deployment gets this row
// either from a direct operator INSERT or the JIT provisioner; tests
// stand one up the same direct way.
async function registerServer(gameId) {
  const serverKey = `srv-test-${crypto.randomBytes(8).toString('hex')}`;
  await query(
    `INSERT INTO game_servers (server_key, game_id, host, port, region, max_players) VALUES ($1, $2, 'test-host', 1234, 'test', 12)`,
    [serverKey, gameId],
  );
  return serverKey;
}

test('grant and consume are server-authoritative, and quantity never goes negative', async () => {
  const creator = await makeUser();
  const player = await makeUser();
  const slug = `inv-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const catalogRes = await api('POST', `/v1/inventory/games/${slug}/catalog`, { body: { sku: 'gold', name: 'Gold Coin', kind: 'currency' }, token: creator.token });
  assert.equal(catalogRes.status, 200, JSON.stringify(catalogRes.body));

  const { rows: gameRows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  const serverKey = await registerServer(gameRows[0].id);

  const grant = await api('POST', `/v1/inventory/games/${slug}/servers/${serverKey}/grant`, { body: { user_id: player.id, sku: 'gold', quantity: 100 } });
  assert.equal(grant.status, 200, JSON.stringify(grant.body));
  assert.equal(grant.body.quantity, 100);

  const mine = await api('GET', `/v1/inventory/me?game_id=${gameRows[0].id}`, { token: player.token });
  assert.equal(mine.status, 200);
  assert.equal(mine.body.items.length, 1);
  assert.equal(mine.body.items[0].sku, 'gold');
  assert.equal(mine.body.items[0].quantity, 100);

  const overConsume = await api('POST', `/v1/inventory/games/${slug}/servers/${serverKey}/consume`, { body: { user_id: player.id, sku: 'gold', quantity: 1000 } });
  assert.equal(overConsume.status, 409, 'consuming more than the real balance is a real conflict, not a negative balance');

  const consume = await api('POST', `/v1/inventory/games/${slug}/servers/${serverKey}/consume`, { body: { user_id: player.id, sku: 'gold', quantity: 40 } });
  assert.equal(consume.status, 200);
  assert.equal(consume.body.quantity, 60);
});

test('grant refuses an unregistered sku, and an unknown server key is refused entirely', async () => {
  const creator = await makeUser();
  const player = await makeUser();
  const slug = `inv-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);
  const { rows: gameRows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  const serverKey = await registerServer(gameRows[0].id);

  const unknownSku = await api('POST', `/v1/inventory/games/${slug}/servers/${serverKey}/grant`, { body: { user_id: player.id, sku: 'no-such-sku', quantity: 1 } });
  assert.equal(unknownSku.status, 400);

  const unknownServer = await api('POST', `/v1/inventory/games/${slug}/servers/not-a-real-key/grant`, { body: { user_id: player.id, sku: 'gold', quantity: 1 } });
  assert.equal(unknownServer.status, 404);
});

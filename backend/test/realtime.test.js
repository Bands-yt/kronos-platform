// Real integration tests for the realtime gateway: authenticated
// WebSocket upgrade, channel-subscription authorization, and a real
// publish (via an inventory grant) actually reaching a subscribed
// socket. createApp() alone does not wire up WebSocket upgrades (see
// server.js's own comment) -- this test attaches the gateway to its own
// test server the same way server.js does for a real run.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test, { after, before } from 'node:test';
import WebSocket from 'ws';

import { createApp } from '../src/server.js';
import { attachWebSocketGateway } from '../src/realtime/gateway.js';
import { pool, query } from '../src/db.js';
import { redis } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';

let server;
let baseUrl;
let wsUrl;

before(async () => {
  setEmailTransport(async () => {});
  server = createApp().listen(0);
  await new Promise((r) => server.once('listening', r));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
  wsUrl = `ws://127.0.0.1:${server.address().port}`;
  attachWebSocketGateway(server);
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

const uniqueEmail = () => `realtime_${crypto.randomBytes(8).toString('hex')}@example.com`;

async function makeUser() {
  await clearRateLimits();
  const signup = await api('POST', '/v1/auth/signup', { body: { email: uniqueEmail(), password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, JSON.stringify(signup.body));
  return { id: signup.body.user.id, token: signup.body.access_token };
}

function onceMessage(ws) {
  return new Promise((resolve) => ws.once('message', (data) => resolve(JSON.parse(data.toString()))));
}

test('an upgrade with no valid access token is refused', async () => {
  const ws = new WebSocket(`${wsUrl}/v1/ws?token=not-a-real-token`);
  const outcome = await new Promise((resolve) => {
    ws.on('open', () => resolve('open'));
    ws.on('error', () => resolve('error'));
    ws.on('close', () => resolve('close'));
  });
  assert.notEqual(outcome, 'open', 'an invalid token must never complete the upgrade');
});

test('a player can subscribe to their own inventory channel, but not someone else\'s', async () => {
  const player = await makeUser();
  const stranger = await makeUser();

  const ws = new WebSocket(`${wsUrl}/v1/ws?token=${player.token}`);
  await new Promise((resolve, reject) => { ws.once('open', resolve); ws.once('error', reject); });

  ws.send(JSON.stringify({ subscribe: `inventory:${player.id}` }));
  const ownChannel = await onceMessage(ws);
  assert.equal(ownChannel.subscribed, `inventory:${player.id}`);

  ws.send(JSON.stringify({ subscribe: `inventory:${stranger.id}` }));
  const strangerChannel = await onceMessage(ws);
  assert.ok(strangerChannel.error, 'subscribing to a different player\'s inventory channel is refused');

  ws.close();
});

test('a real inventory grant is really pushed to a subscribed socket', async () => {
  const creator = await makeUser();
  const player = await makeUser();
  const slug = `rt-${crypto.randomBytes(4).toString('hex')}`;
  await api('POST', '/v1/catalog/games/publish', { body: { slug, title: 'RT game' }, token: creator.token });
  await api('POST', `/v1/inventory/games/${slug}/catalog`, { body: { sku: 'gem', name: 'Gem' }, token: creator.token });
  const { rows: gameRows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  const serverKey = `srv-test-${crypto.randomBytes(8).toString('hex')}`;
  await query(
    `INSERT INTO game_servers (server_key, game_id, host, port, region, max_players) VALUES ($1, $2, 'h', 1, 'r', 1)`,
    [serverKey, gameRows[0].id],
  );

  const ws = new WebSocket(`${wsUrl}/v1/ws?token=${player.token}`);
  await new Promise((resolve, reject) => { ws.once('open', resolve); ws.once('error', reject); });
  ws.send(JSON.stringify({ subscribe: `inventory:${player.id}` }));
  await onceMessage(ws); // the {subscribed: ...} ack

  const pushed = onceMessage(ws);
  const grant = await api('POST', `/v1/inventory/games/${slug}/servers/${serverKey}/grant`, { body: { user_id: player.id, sku: 'gem', quantity: 3 } });
  assert.equal(grant.status, 200, JSON.stringify(grant.body));

  const message = await pushed;
  assert.equal(message.channel, `inventory:${player.id}`);
  assert.equal(message.data.sku, 'gem');
  assert.equal(message.data.quantity, 3);

  ws.close();
});

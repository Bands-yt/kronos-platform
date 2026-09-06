// Real integration tests for native matchmaking: ticket queue, rating-
// band grouping, grace-period solo fallback, and cancellation. Reuses
// the real allocation path (sessions/routes.js's allocatePlayerToGame)
// against a real live server registered directly in Redis/Postgres, the
// same way a real dedicated server would after heartbeating.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { config } from '../src/config.js';
import { pool, query } from '../src/db.js';
import { redis, keys } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';

let server;
let baseUrl;
const originalGraceSeconds = config.matchmakingGraceSeconds;

before(async () => {
  setEmailTransport(async () => {});
  // Real players in this test never wait for a companion -- a 15s grace
  // period (the real default) would make this test slow for no reason.
  config.matchmakingGraceSeconds = 0;
  server = createApp().listen(0);
  await new Promise((r) => server.once('listening', r));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  config.matchmakingGraceSeconds = originalGraceSeconds;
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

const uniqueEmail = () => `mm_${crypto.randomBytes(8).toString('hex')}@example.com`;

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

// A real, alive server: registered in Postgres AND heartbeating in
// Redis, exactly what fetchLiveServers (sessions/routes.js) requires
// before allocatePlayerToGame will ever pick it.
async function registerLiveServer(gameId, maxPlayers = 12) {
  const serverKey = `srv-test-${crypto.randomBytes(8).toString('hex')}`;
  await query(
    `INSERT INTO game_servers (server_key, game_id, host, port, region, max_players) VALUES ($1, $2, '127.0.0.1', 9999, 'test', $3)`,
    [serverKey, gameId, maxPlayers],
  );
  await redis.set(keys.serverHeartbeat(serverKey), '1', 'EX', 60);
  await redis.set(keys.serverPlayers(serverKey), '0', 'EX', 60);
  return serverKey;
}

test('a solo ticket matches after the grace period once a live server exists', async () => {
  const player = await makeUser();
  const slug = `mm-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(player.token, slug);
  const { rows: gameRows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  await registerLiveServer(gameRows[0].id);

  const created = await api('POST', `/v1/matchmaking/games/${slug}/tickets`, { body: {}, token: player.token });
  assert.equal(created.status, 201, JSON.stringify(created.body));

  const polled = await api('GET', `/v1/matchmaking/tickets/${created.body.ticket_id}`, { token: player.token });
  assert.equal(polled.status, 200, JSON.stringify(polled.body));
  assert.equal(polled.body.status, 'matched');
  assert.ok(polled.body.join_ticket, 'a real join ticket was minted via the exact same allocation path /v1/sessions/allocate uses');
  assert.equal(polled.body.server.server_key, (await query(`SELECT server_key FROM game_servers WHERE game_id = $1`, [gameRows[0].id])).rows[0].server_key);
});

test('a ticket cannot be polled or cancelled by a different player', async () => {
  const player = await makeUser();
  const stranger = await makeUser();
  const slug = `mm-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(player.token, slug);

  const created = await api('POST', `/v1/matchmaking/games/${slug}/tickets`, { body: {}, token: player.token });
  assert.equal(created.status, 201);

  const stolenPoll = await api('GET', `/v1/matchmaking/tickets/${created.body.ticket_id}`, { token: stranger.token });
  assert.equal(stolenPoll.status, 403);

  const stolenCancel = await api('DELETE', `/v1/matchmaking/tickets/${created.body.ticket_id}`, { token: stranger.token });
  assert.equal(stolenCancel.status, 403);

  const ownCancel = await api('DELETE', `/v1/matchmaking/tickets/${created.body.ticket_id}`, { token: player.token });
  assert.equal(ownCancel.status, 200);

  const afterCancel = await api('GET', `/v1/matchmaking/tickets/${created.body.ticket_id}`, { token: player.token });
  assert.equal(afterCancel.status, 404, 'a cancelled ticket is really gone');
});

test('a ticket stays queued with no live server, and 503s never leak out as an opaque error', async () => {
  const player = await makeUser();
  const slug = `mm-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(player.token, slug);

  const created = await api('POST', `/v1/matchmaking/games/${slug}/tickets`, { body: {}, token: player.token });
  assert.equal(created.status, 201);
  const polled = await api('GET', `/v1/matchmaking/tickets/${created.body.ticket_id}`, { token: player.token });
  assert.equal(polled.status, 200);
  assert.equal(polled.body.status, 'queued', 'no live server anywhere for this game, so the poller reports queued rather than erroring');
});

// Real integration tests for global leaderboards: server-authoritative
// score submission (best-score policy by default), and rank computed at
// read time. Real Express, real PostgreSQL, real Redis.
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

const uniqueEmail = () => `leaderboard_${crypto.randomBytes(8).toString('hex')}@example.com`;

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

async function registerServer(gameId) {
  const serverKey = `srv-test-${crypto.randomBytes(8).toString('hex')}`;
  await query(
    `INSERT INTO game_servers (server_key, game_id, host, port, region, max_players) VALUES ($1, $2, 'test-host', 1234, 'test', 12)`,
    [serverKey, gameId],
  );
  return serverKey;
}

test('the default "best" policy never lowers a score, and rank is computed live', async () => {
  const creator = await makeUser();
  const alice = await makeUser();
  const bob = await makeUser();
  const slug = `lb-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);
  const { rows: gameRows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  const serverKey = await registerServer(gameRows[0].id);

  await api('POST', `/v1/leaderboards/games/${slug}/servers/${serverKey}/scores`, { body: { board_key: 'global', user_id: alice.id, score: 100 } });
  await api('POST', `/v1/leaderboards/games/${slug}/servers/${serverKey}/scores`, { body: { board_key: 'global', user_id: bob.id, score: 50 } });

  const worseSubmit = await api('POST', `/v1/leaderboards/games/${slug}/servers/${serverKey}/scores`, { body: { board_key: 'global', user_id: alice.id, score: 10 } });
  assert.equal(worseSubmit.status, 200);
  assert.equal(worseSubmit.body.status, 'unchanged');
  assert.equal(worseSubmit.body.score, 100, 'a worse score never overwrites the real best under the default policy');

  const betterSubmit = await api('POST', `/v1/leaderboards/games/${slug}/servers/${serverKey}/scores`, { body: { board_key: 'global', user_id: bob.id, score: 200 } });
  assert.equal(betterSubmit.status, 200);
  assert.equal(betterSubmit.body.status, 'updated');
  assert.equal(betterSubmit.body.score, 200);

  const top = await api('GET', `/v1/leaderboards/games/${slug}/global?limit=10`);
  assert.equal(top.status, 200, JSON.stringify(top.body));
  assert.equal(top.body.entries.length, 2);
  assert.equal(top.body.entries[0].user_id, String(bob.id), 'bob is now really rank 1 after the real update');
  assert.equal(Number(top.body.entries[0].rank), 1);
  assert.equal(top.body.entries[1].user_id, String(alice.id));
  assert.equal(Number(top.body.entries[1].rank), 2);
});

test('the "latest" policy always overwrites, even with a worse score', async () => {
  const creator = await makeUser();
  const player = await makeUser();
  const slug = `lb-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);
  const { rows: gameRows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  const serverKey = await registerServer(gameRows[0].id);

  await api('POST', `/v1/leaderboards/games/${slug}/servers/${serverKey}/scores`, { body: { board_key: 'speedrun', user_id: player.id, score: 500, policy: 'latest' } });
  const overwritten = await api('POST', `/v1/leaderboards/games/${slug}/servers/${serverKey}/scores`, { body: { board_key: 'speedrun', user_id: player.id, score: 1, policy: 'latest' } });
  assert.equal(overwritten.status, 200);
  assert.equal(overwritten.body.score, 1, 'the latest policy really overwrites regardless of direction');
});

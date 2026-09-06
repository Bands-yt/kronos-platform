// Real integration tests for the Telemetry & Minidump Handler: raw
// binary crash-dump intake, content-addressed storage on the local-disk
// fallback, and the honest "received, no trace" state when no
// minidump-stackwalk binary is configured (the real default for every
// deployment of this repo today).
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { config } from '../src/config.js';
import { pool, query } from '../src/db.js';
import { redis } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';

let server;
let baseUrl;
let localStorageTestDir;

before(async () => {
  setEmailTransport(async () => {});
  localStorageTestDir = await fsp.mkdtemp(path.join(os.tmpdir(), 'kronos-test-telemetry-'));
  config.localStorageDir = localStorageTestDir;
  server = createApp().listen(0);
  await new Promise((r) => server.once('listening', r));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  server.close();
  await pool.end();
  redis.disconnect();
  await fsp.rm(localStorageTestDir, { recursive: true, force: true });
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

const uniqueEmail = () => `telemetry_${crypto.randomBytes(8).toString('hex')}@example.com`;

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

test('a real crash dump is stored content-addressed and stays at "received" with no stackwalk tool configured', async () => {
  assert.equal(config.minidumpStackwalkPath, '', 'sanity: no external tool is configured for this test run');

  const creator = await makeUser();
  const slug = `crash-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const dumpBytes = Buffer.from(`a real minidump body -- ${crypto.randomBytes(32).toString('hex')}`);
  const expectedSha256 = crypto.createHash('sha256').update(dumpBytes).digest('hex');

  const res = await fetch(`${baseUrl}/v1/telemetry/crash?game_slug=${slug}&engine_version=0.4.0&platform=win64`, {
    method: 'POST',
    headers: { 'content-type': 'application/octet-stream' },
    body: dumpBytes,
  });
  assert.equal(res.status, 202);
  const body = await res.json();
  assert.equal(body.sha256, expectedSha256, 'the server-computed hash matches the real uploaded bytes');

  // Give the fire-and-forget processMinidump() a tick to run (it's a
  // real no-op here, but still asynchronous).
  await new Promise((r) => setTimeout(r, 50));

  const { rows } = await query(`SELECT status, stack_trace, size_bytes, engine_version, platform FROM crash_reports WHERE id = $1`, [body.id]);
  assert.equal(rows[0].status, 'received', 'a real, honest state -- no fabricated trace with no tool configured');
  assert.equal(rows[0].stack_trace, null);
  assert.equal(Number(rows[0].size_bytes), dumpBytes.length);
  assert.equal(rows[0].engine_version, '0.4.0');
  assert.equal(rows[0].platform, 'win64');

  const ownerView = await api('GET', `/v1/telemetry/crash/${body.id}`, { token: creator.token });
  assert.equal(ownerView.status, 200, JSON.stringify(ownerView.body));
  assert.equal(ownerView.body.crash_report.sha256, expectedSha256);

  const stranger = await makeUser();
  const strangerView = await api('GET', `/v1/telemetry/crash/${body.id}`, { token: stranger.token });
  assert.equal(strangerView.status, 403, 'only the game\'s creator (or an admin) can see its crash reports');

  const listed = await api('GET', `/v1/telemetry/games/${slug}/crashes`, { token: creator.token });
  assert.equal(listed.status, 200);
  assert.equal(listed.body.crash_reports.length, 1);
  assert.equal(listed.body.crash_reports[0].sha256, expectedSha256);
});

test('an empty body is refused before anything is stored', async () => {
  const res = await fetch(`${baseUrl}/v1/telemetry/crash`, {
    method: 'POST',
    headers: { 'content-type': 'application/octet-stream' },
    body: Buffer.alloc(0),
  });
  assert.equal(res.status, 400);
});

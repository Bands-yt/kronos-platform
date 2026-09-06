// Real integration tests for Chunked Delta-Binary Sync: local-disk
// chunk upload/confirm, version manifests, sync-plan delta computation,
// instant rollback, and exclusive asset locks. Real Express, real
// PostgreSQL, real Redis -- no mocks, same convention as
// assetStreaming.test.js.
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
  localStorageTestDir = await fsp.mkdtemp(path.join(os.tmpdir(), 'kronos-test-assetsync-'));
  config.localStorageDir = localStorageTestDir;
  server = createApp().listen(0);
  await new Promise((r) => server.once('listening', r));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
  config.publicBaseUrl = baseUrl;
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

const uniqueEmail = () => `assetsync_${crypto.randomBytes(8).toString('hex')}@example.com`;

async function makeUser() {
  await clearRateLimits();
  const email = uniqueEmail();
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, `signup failed: ${JSON.stringify(signup.body)}`);
  return { id: signup.body.user.id, token: signup.body.access_token };
}

async function publishGame(token, slug) {
  const res = await api('POST', '/v1/catalog/games/publish', { body: { slug, title: `Game ${slug}` }, token });
  assert.equal(res.status, 201, JSON.stringify(res.body));
}

// Uploads real bytes through the full chunk upload-url -> PUT ->
// confirm pipeline (local-disk fallback) and returns the sha256.
async function uploadChunk(token, slug, bytes) {
  const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  const uploadUrlRes = await api('POST', `/v1/catalog/games/${slug}/chunks/upload-url`,
    { body: { sha256, size_bytes: bytes.length }, token });
  assert.equal(uploadUrlRes.status, 200, JSON.stringify(uploadUrlRes.body));
  if (uploadUrlRes.body.needed) {
    const putRes = await fetch(uploadUrlRes.body.upload_url, { method: 'PUT', body: bytes, headers: { authorization: `Bearer ${token}` } });
    assert.equal(putRes.status, 200);
    const confirmRes = await api('POST', `/v1/catalog/games/${slug}/chunks/${sha256}/confirm`, { token });
    assert.equal(confirmRes.status, 200, JSON.stringify(confirmRes.body));
  }
  return sha256;
}

test('a chunk already confirmed is reported as not needed, the real dedup win', async () => {
  const creator = await makeUser();
  const slug = `chunks-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const bytes = Buffer.from(`chunk bytes ${crypto.randomBytes(16).toString('hex')}`);
  const sha256 = await uploadChunk(creator.token, slug, bytes);

  const secondRequest = await api('POST', `/v1/catalog/games/${slug}/chunks/upload-url`,
    { body: { sha256, size_bytes: bytes.length }, token: creator.token });
  assert.equal(secondRequest.status, 200);
  assert.equal(secondRequest.body.needed, false, 'a confirmed chunk needs no re-upload, even for a different request');
});

test('confirm rejects a chunk whose real content does not match the declared hash', async () => {
  const creator = await makeUser();
  const slug = `chunks-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const claimedSha256 = crypto.createHash('sha256').update('claimed chunk').digest('hex');
  const uploadUrlRes = await api('POST', `/v1/catalog/games/${slug}/chunks/upload-url`,
    { body: { sha256: claimedSha256, size_bytes: 10 }, token: creator.token });
  assert.equal(uploadUrlRes.status, 200);
  await fetch(uploadUrlRes.body.upload_url, { method: 'PUT', body: Buffer.from('not what was claimed'), headers: { authorization: `Bearer ${creator.token}` } });

  const confirmRes = await api('POST', `/v1/catalog/games/${slug}/chunks/${claimedSha256}/confirm`, { token: creator.token });
  assert.equal(confirmRes.status, 400);

  const { rows } = await query(`SELECT 1 FROM asset_chunks WHERE sha256 = $1`, [claimedSha256]);
  assert.equal(rows.length, 0, 'a failed confirm never registers the chunk as real');
});

test('a version manifest is refused when it references an unconfirmed chunk', async () => {
  const creator = await makeUser();
  const slug = `versions-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const neverUploaded = crypto.createHash('sha256').update('never uploaded').digest('hex');
  const res = await api('POST', `/v1/catalog/games/${slug}/versions`,
    { body: { chunks: [neverUploaded], total_size_bytes: 10 }, token: creator.token });
  assert.equal(res.status, 400);
});

test('a real version publish, sync-plan delta, and instant rollback', async () => {
  const creator = await makeUser();
  const slug = `versions-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const chunkA = Buffer.from(`chunk A ${crypto.randomBytes(8).toString('hex')}`);
  const chunkB = Buffer.from(`chunk B ${crypto.randomBytes(8).toString('hex')}`);
  const shaA = await uploadChunk(creator.token, slug, chunkA);
  const shaB = await uploadChunk(creator.token, slug, chunkB);

  const v1 = await api('POST', `/v1/catalog/games/${slug}/versions`,
    { body: { chunks: [shaA, shaB], total_size_bytes: chunkA.length + chunkB.length, label: 'v1' }, token: creator.token });
  assert.equal(v1.status, 201, JSON.stringify(v1.body));
  assert.equal(v1.body.version.chunk_count, 2);

  // A client that already has chunk A only needs chunk B -- the real
  // delta computation.
  const syncPlan = await api('POST', `/v1/catalog/games/${slug}/versions/${v1.body.version.id}/sync-plan`, { body: { have: [shaA] } });
  assert.equal(syncPlan.status, 200, JSON.stringify(syncPlan.body));
  const byHash = Object.fromEntries(syncPlan.body.chunks.map((c) => [c.sha256, c]));
  assert.equal(byHash[shaA].needed, false, 'a chunk the caller already has needs no download URL');
  assert.equal(byHash[shaA].download_url, undefined);
  assert.equal(byHash[shaB].needed, true);
  assert.ok(byHash[shaB].download_url.startsWith('http'), 'a real download URL was minted only for the missing chunk');

  // A second chunk-only publish (a "delta" version needing only one new
  // chunk) -- then roll back and confirm it's instant (no data moved).
  const chunkC = Buffer.from(`chunk C ${crypto.randomBytes(8).toString('hex')}`);
  const shaC = await uploadChunk(creator.token, slug, chunkC);
  const v2 = await api('POST', `/v1/catalog/games/${slug}/versions`,
    { body: { chunks: [shaA, shaC], total_size_bytes: chunkA.length + chunkC.length, label: 'v2' }, token: creator.token });
  assert.equal(v2.status, 201);

  const { rows: afterV2 } = await query(`SELECT current_asset_version_id FROM games WHERE slug = $1`, [slug]);
  assert.equal(String(afterV2[0].current_asset_version_id), v2.body.version.id);

  const rollback = await api('POST', `/v1/catalog/games/${slug}/versions/${v1.body.version.id}/rollback`, { token: creator.token });
  assert.equal(rollback.status, 200, JSON.stringify(rollback.body));
  const { rows: afterRollback } = await query(`SELECT current_asset_version_id FROM games WHERE slug = $1`, [slug]);
  assert.equal(String(afterRollback[0].current_asset_version_id), v1.body.version.id, 'rollback really repointed the game at the older version');
});

test('asset locks are exclusive: a lock held by someone else refuses a new acquire until it expires', async () => {
  const owner = await makeUser();
  const stranger = await makeUser();
  const slug = `locks-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(owner.token, slug);

  // A stranger cannot even attempt to lock an asset in a game they do
  // not own (this repo has no separate collaborator ACL yet).
  const strangerAttempt = await api('POST', `/v1/catalog/games/${slug}/locks`, { body: { asset_path: 'scenes/main.kronos' }, token: stranger.token });
  assert.equal(strangerAttempt.status, 403);

  const acquired = await api('POST', `/v1/catalog/games/${slug}/locks`, { body: { asset_path: 'scenes/main.kronos' }, token: owner.token });
  assert.equal(acquired.status, 200, JSON.stringify(acquired.body));

  // Re-acquiring your own lock refreshes it rather than conflicting.
  const refreshed = await api('POST', `/v1/catalog/games/${slug}/locks`, { body: { asset_path: 'scenes/main.kronos' }, token: owner.token });
  assert.equal(refreshed.status, 200);

  // Simulate a real conflicting hold (a collaborator ACL would let a
  // second real user reach this path; the exclusivity check itself is
  // tested directly here against a seeded conflicting row).
  const { rows: gameRows } = await query(`SELECT id FROM games WHERE slug = $1`, [slug]);
  await query(`UPDATE asset_locks SET locked_by = $1 WHERE game_id = $2 AND asset_path = $3`, [stranger.id, gameRows[0].id, 'scenes/main.kronos']);

  const conflicted = await api('POST', `/v1/catalog/games/${slug}/locks`, { body: { asset_path: 'scenes/main.kronos' }, token: owner.token });
  assert.equal(conflicted.status, 409, 'a real, unexpired hold by someone else is a genuine conflict');

  const released = await api('DELETE', `/v1/catalog/games/${slug}/locks`, { body: { asset_path: 'scenes/main.kronos' }, token: owner.token });
  assert.equal(released.status, 200);
  const { rows: afterRelease } = await query(`SELECT 1 FROM asset_locks WHERE game_id = $1 AND asset_path = $2`, [gameRows[0].id, 'scenes/main.kronos']);
  assert.equal(afterRelease.length, 0);
});

test('dialogue lines require an already-confirmed chunk for their audio and expose phoneme CRUD', async () => {
  const creator = await makeUser();
  const slug = `dialogue-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const audioBytes = Buffer.from(`audio bytes ${crypto.randomBytes(8).toString('hex')}`);
  const audioSha = await uploadChunk(creator.token, slug, audioBytes);

  const line = await api('POST', `/v1/catalog/games/${slug}/dialogue`,
    { body: { line_key: 'intro_01', transcript: 'Welcome to Kronos.', audio_sha256: audioSha, duration_ms: 2000 }, token: creator.token });
  assert.equal(line.status, 200, JSON.stringify(line.body));

  const phonemes = await api('POST', `/v1/catalog/games/${slug}/dialogue/${line.body.id}/phonemes`,
    { body: { phonemes: [{ phoneme: 'W', start_ms: 0, end_ms: 100 }, { phoneme: 'EH', start_ms: 100, end_ms: 200 }] }, token: creator.token });
  assert.equal(phonemes.status, 200);
  assert.equal(phonemes.body.count, 2);

  const listed = await api('GET', `/v1/catalog/games/${slug}/dialogue/${line.body.id}/phonemes`, { token: creator.token });
  assert.equal(listed.status, 200);
  assert.equal(listed.body.phonemes.length, 2);
  assert.equal(listed.body.phonemes[0].phoneme, 'W');
});

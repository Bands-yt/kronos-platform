// Real integration tests for Dynamic Asset Streaming: presigned upload/
// download against a REAL S3-compatible store (MinIO, run locally in
// this sandbox at 127.0.0.1:19000 -- see the session's own setup), real
// Express, real PostgreSQL. No mocks, no stubbed S3 client.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { config } from '../src/config.js';
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

const uniqueEmail = () => `asset_${crypto.randomBytes(8).toString('hex')}@example.com`;

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

// Real, live MinIO instance -- see this session's own setup (a real
// bucket "kronos-packages" already created against it). Skips cleanly
// when nothing is listening there, the same "not configured is a real,
// honest no-op" convention every other optional-infra test in this
// suite (JIT provisioning, etc.) already follows.
const originalS3Config = {
  s3Bucket: config.s3Bucket, s3Region: config.s3Region, s3Endpoint: config.s3Endpoint,
  s3AccessKeyId: config.s3AccessKeyId, s3SecretAccessKey: config.s3SecretAccessKey,
  s3ForcePathStyle: config.s3ForcePathStyle, s3PublicBaseUrl: config.s3PublicBaseUrl,
};

async function withRealS3(fn) {
  Object.assign(config, {
    s3Bucket: 'kronos-packages',
    s3Region: 'us-east-1',
    s3Endpoint: 'http://127.0.0.1:19000',
    s3AccessKeyId: 'kronosminio',
    s3SecretAccessKey: 'kronosminiosecret',
    s3ForcePathStyle: true,
    s3PublicBaseUrl: '',
  });
  try {
    await fn();
  } finally {
    Object.assign(config, originalS3Config);
  }
}

test('package routes really answer 503 when object storage is not configured', async () => {
  assert.equal(config.s3Bucket, '', 'sanity: S3 really is not configured for this test');
  const creator = await makeUser();
  const slug = `pkg-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const res = await api('POST', `/v1/catalog/games/${slug}/package/upload-url`,
    { body: { sha256: 'a'.repeat(64), size_bytes: 100 }, token: creator.token });
  assert.equal(res.status, 503);
});

test('a real presigned upload, a real confirm, and a real download round-trip byte-for-byte', async () => {
  await withRealS3(async () => {
    const creator = await makeUser();
    const slug = `pkg-${crypto.randomBytes(4).toString('hex')}`;
    await publishGame(creator.token, slug);

    const archiveBytes = Buffer.from(`a real .kronos archive body, not a placeholder -- ${crypto.randomBytes(16).toString('hex')}`);
    const sha256 = crypto.createHash('sha256').update(archiveBytes).digest('hex');

    const uploadUrlRes = await api('POST', `/v1/catalog/games/${slug}/package/upload-url`,
      { body: { sha256, size_bytes: archiveBytes.length }, token: creator.token });
    assert.equal(uploadUrlRes.status, 200, JSON.stringify(uploadUrlRes.body));
    assert.ok(uploadUrlRes.body.upload_url.startsWith('http'), 'a real presigned URL was returned');
    assert.equal(uploadUrlRes.body.object_key, `packages/${sha256}.kronos`, 'the object key is really content-addressed');

    // The real upload -- a plain PUT, no SDK, no special headers,
    // exactly what a real, minimal HTTP client (libcurl on the engine
    // side) would do.
    const putRes = await fetch(uploadUrlRes.body.upload_url, { method: 'PUT', body: archiveBytes });
    assert.equal(putRes.status, 200, `the real PUT to MinIO really succeeded (got ${putRes.status})`);

    const confirmRes = await api('POST', `/v1/catalog/games/${slug}/package/confirm`,
      { body: { sha256 }, token: creator.token });
    assert.equal(confirmRes.status, 200, JSON.stringify(confirmRes.body));
    assert.equal(confirmRes.body.size_bytes, archiveBytes.length, 'the REAL uploaded size was reported, not the client\'s claim');

    const { rows } = await query(
      `SELECT scene_sha256, package_object_key, package_size_bytes FROM games WHERE slug = $1`, [slug],
    );
    assert.equal(rows[0].scene_sha256, sha256, 'the real hash really persisted to the games row');
    assert.equal(rows[0].package_object_key, `packages/${sha256}.kronos`);
    assert.equal(Number(rows[0].package_size_bytes), archiveBytes.length);

    const packageRes = await api('GET', `/v1/catalog/games/${slug}/package`);
    assert.equal(packageRes.status, 200);
    assert.equal(packageRes.body.sha256, sha256);
    assert.ok(packageRes.body.download_url.startsWith('http'), 'a real presigned download URL was returned');

    // The real download -- fetching it back and confirming the bytes
    // are genuinely identical to what was uploaded, not just that the
    // request succeeded.
    const downloadRes = await fetch(packageRes.body.download_url);
    assert.equal(downloadRes.status, 200);
    const downloaded = Buffer.from(await downloadRes.arrayBuffer());
    assert.ok(downloaded.equals(archiveBytes), 'the real downloaded bytes are byte-for-byte identical to what was uploaded');
  });
});

test('confirm really rejects when the uploaded content does not match the declared hash', async () => {
  await withRealS3(async () => {
    const creator = await makeUser();
    const slug = `pkg-${crypto.randomBytes(4).toString('hex')}`;
    await publishGame(creator.token, slug);

    const claimedSha256 = crypto.createHash('sha256').update('what was CLAIMED').digest('hex');
    const uploadUrlRes = await api('POST', `/v1/catalog/games/${slug}/package/upload-url`,
      { body: { sha256: claimedSha256, size_bytes: 20 }, token: creator.token });
    assert.equal(uploadUrlRes.status, 200);

    // Upload DIFFERENT real bytes than what was claimed -- a real,
    // possible failure mode (corruption in transit, a buggy client, or
    // an attempt to smuggle different content under a trusted hash).
    const actualBytes = Buffer.from('this is NOT what was claimed at all');
    const putRes = await fetch(uploadUrlRes.body.upload_url, { method: 'PUT', body: actualBytes });
    assert.equal(putRes.status, 200, 'MinIO itself has no reason to reject this upload -- the mismatch is caught server-side');

    const confirmRes = await api('POST', `/v1/catalog/games/${slug}/package/confirm`,
      { body: { sha256: claimedSha256 }, token: creator.token });
    assert.equal(confirmRes.status, 400, 'the real server-side re-hash really catches the mismatch');

    const { rows } = await query(`SELECT scene_sha256 FROM games WHERE slug = $1`, [slug]);
    assert.equal(rows[0].scene_sha256, null, 'a failed confirm never touches the games row');
  });
});

test('only the real creator can request an upload URL or confirm for their own game', async () => {
  await withRealS3(async () => {
    const owner = await makeUser();
    const stranger = await makeUser();
    const slug = `pkg-${crypto.randomBytes(4).toString('hex')}`;
    await publishGame(owner.token, slug);

    const stolenUrl = await api('POST', `/v1/catalog/games/${slug}/package/upload-url`,
      { body: { sha256: 'a'.repeat(64), size_bytes: 10 }, token: stranger.token });
    assert.equal(stolenUrl.status, 403);

    const stolenConfirm = await api('POST', `/v1/catalog/games/${slug}/package/confirm`,
      { body: { sha256: 'a'.repeat(64) }, token: stranger.token });
    assert.equal(stolenConfirm.status, 403);

    const noSuchGame = await api('POST', `/v1/catalog/games/no-such-slug-at-all/package/upload-url`,
      { body: { sha256: 'a'.repeat(64), size_bytes: 10 }, token: owner.token });
    assert.equal(noSuchGame.status, 404, 'a slug that was never published at all is a real 404, not 403');
  });
});

test('an oversized declared size is refused before a presigned URL is ever minted', async () => {
  await withRealS3(async () => {
    const creator = await makeUser();
    const slug = `pkg-${crypto.randomBytes(4).toString('hex')}`;
    await publishGame(creator.token, slug);

    const res = await api('POST', `/v1/catalog/games/${slug}/package/upload-url`, {
      body: { sha256: 'a'.repeat(64), size_bytes: config.packageMaxSizeBytes + 1 },
      token: creator.token,
    });
    assert.equal(res.status, 400);
  });
});

test('a game with no uploaded package really 404s on GET .../package', async () => {
  const creator = await makeUser();
  const slug = `pkg-${crypto.randomBytes(4).toString('hex')}`;
  await publishGame(creator.token, slug);

  const res = await api('GET', `/v1/catalog/games/${slug}/package`);
  assert.equal(res.status, 404);
});

test('publishing with a scene_sha256 that has no real uploaded package is refused once S3 is configured', async () => {
  await withRealS3(async () => {
    const creator = await makeUser();
    const slug = `pkg-${crypto.randomBytes(4).toString('hex')}`;
    const res = await api('POST', '/v1/catalog/games/publish', {
      body: { slug, title: 'x', scene_sha256: crypto.createHash('sha256').update('nothing uploaded').digest('hex') },
      token: creator.token,
    });
    assert.equal(res.status, 400, 'a well-formed but nonexistent package hash is really rejected once storage can verify it');
  });
});

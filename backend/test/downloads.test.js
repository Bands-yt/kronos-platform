// Real integration tests for the Web Download portal: a manifest that
// only ever reports real, actually-published builds (never a fabricated
// entry), a real server-side sha256, and a real redirect/stream to the
// real bytes. Real Express, no mocks.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { config } from '../src/config.js';
import { pool } from '../src/db.js';
import { redis } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';
import { installerObjectKey } from '../src/storage/objectKey.js';

let server;
let baseUrl;
let localStorageTestDir;

before(async () => {
  setEmailTransport(async () => {});
  localStorageTestDir = await fsp.mkdtemp(path.join(os.tmpdir(), 'kronos-test-downloads-'));
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

// Places real bytes directly on local disk at the exact path
// storage/local.js's own objectPath() would resolve the given key to --
// simulating the out-of-band release process that really populates
// this in a real deployment (see catalog/downloads.js's own header
// comment: this backend never receives installer uploads itself).
async function placeLocalBuild(key, bytes) {
  const dest = path.join(localStorageTestDir, key);
  await fsp.mkdir(path.dirname(dest), { recursive: true });
  await fsp.writeFile(dest, bytes);
}

test('an app/platform with no real build published is omitted from the manifest, never fabricated', async () => {
  const res = await fetch(`${baseUrl}/v1/downloads/latest`);
  assert.equal(res.status, 200);
  const body = await res.json();
  assert.equal(body.version, config.desktopAppsVersion);
  assert.deepEqual(body.apps, []);
});

test('a real published build appears with a real server-computed sha256 and round-trips byte-for-byte', async () => {
  const bytes = Buffer.from(`a real Kronos Studio win64 build -- ${crypto.randomBytes(24).toString('hex')}`);
  const expectedSha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  await placeLocalBuild(installerObjectKey('studio', 'win64', config.desktopAppsVersion), bytes);

  const manifest = await fetch(`${baseUrl}/v1/downloads/latest`).then((r) => r.json());
  const entry = manifest.apps.find((a) => a.app === 'studio' && a.platform === 'win64');
  assert.ok(entry, 'the real published build really appears in the manifest');
  assert.equal(entry.sha256, expectedSha256, 'the sha256 is really computed from the real bytes, not fabricated');
  assert.equal(entry.size_bytes, bytes.length);
  assert.equal(entry.version, config.desktopAppsVersion);
  assert.equal(entry.name, 'Kronos Studio');

  const downloaded = await fetch(entry.download_url).then((r) => r.arrayBuffer());
  assert.ok(Buffer.from(downloaded).equals(bytes), 'the real downloaded bytes are byte-for-byte identical to the real build');
});

test('the direct installer route 404s for an unknown app or platform, and streams real bytes for a known one', async () => {
  const unknownApp = await fetch(`${baseUrl}/v1/downloads/installers/not-a-real-app/win64`);
  assert.equal(unknownApp.status, 404);
  const unknownPlatform = await fetch(`${baseUrl}/v1/downloads/installers/studio/not-a-real-platform`);
  assert.equal(unknownPlatform.status, 404);

  const bytes = Buffer.from(`a real Kronos Audio linux build -- ${crypto.randomBytes(24).toString('hex')}`);
  await placeLocalBuild(installerObjectKey('audio', 'linux_x86_64', config.desktopAppsVersion), bytes);
  const res = await fetch(`${baseUrl}/v1/downloads/installers/audio/linux_x86_64`);
  assert.equal(res.status, 200);
  const downloaded = Buffer.from(await res.arrayBuffer());
  assert.ok(downloaded.equals(bytes));
});

// Kronos ("GitHub Releases Binary Download Integration"): a real,
// known app/platform with no build staged on THIS server host must
// never just 404 -- release.yml (see .github/workflows/release.yml)
// publishes every real combo to GitHub Releases on every tag push, so
// this server always has a real fallback to point at. `redirect:
// 'manual'` is deliberate: this test asserts on the Location header
// this backend itself computed, and must never actually dial out to
// github.com (no network access in CI, and nothing here needs GitHub to
// really have that asset for this backend's own contract to be
// verified).
test('a real app/platform with no local build 302-redirects to the real GitHub Release artifact URL', async () => {
  const res = await fetch(`${baseUrl}/v1/downloads/installers/movie-maker/win64`, { redirect: 'manual' });
  assert.equal(res.status, 302);
  assert.equal(
    res.headers.get('location'),
    `https://github.com/${config.githubReleasesRepo}/releases/latest/download/kronos_movie_maker_win64.zip`,
  );
});

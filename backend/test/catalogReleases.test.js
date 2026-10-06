// Package versions, review, rollback, storage quota and catalog images,
// against real Express, PostgreSQL, Redis and local-disk storage.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fsp from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { Readable } from 'node:stream';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { config } from '../src/config.js';
import { pool, query } from '../src/db.js';
import { closeRedis, redis } from '../src/redis.js';
import { setEmailTransport } from '../src/email/mailer.js';
import {
  buildArchive, inspectPackageStream, isSafeRelativePath, minimalGameArchive, validateGamePackage,
} from '../src/catalog/packageArchive.js';
import { imageInfo } from '../src/catalog/imageInfo.js';

let server;
let baseUrl;
let storageDir;

before(async () => {
  setEmailTransport(async () => {});
  storageDir = await fsp.mkdtemp(path.join(os.tmpdir(), 'kronos-releases-'));
  config.localStorageDir = storageDir;
  config.gameReviewRequired = true;
  server = createApp().listen(0);
  await new Promise((r) => server.once('listening', r));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
  config.publicBaseUrl = baseUrl;
});

after(async () => {
  server.close();
  await pool.end();
  closeRedis();
  await fsp.rm(storageDir, { recursive: true, force: true });
});

async function api(method, urlPath, { body, token } = {}) {
  const res = await fetch(`${baseUrl}${urlPath}`, {
    method,
    headers: { 'content-type': 'application/json', ...(token ? { authorization: `Bearer ${token}` } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await res.text();
  let parsed = null;
  try { parsed = text ? JSON.parse(text) : null; } catch { parsed = text; }
  return { status: res.status, body: parsed, headers: res.headers };
}

async function clearRateLimits() {
  const found = await redis.keys('rl:*');
  if (found.length > 0) await redis.del(...found);
}

async function makeUser({ admin = false } = {}) {
  await clearRateLimits();
  const email = `rel_${crypto.randomBytes(8).toString('hex')}@example.com`;
  const signup = await api('POST', '/v1/auth/signup', { body: { email, password: 'a reasonable passphrase' } });
  assert.equal(signup.status, 201, JSON.stringify(signup.body));
  if (admin) await query(`UPDATE users SET role = 'admin' WHERE id = $1`, [signup.body.user.id]);
  return { id: signup.body.user.id, token: signup.body.access_token };
}

async function newGame(creator) {
  const slug = `rel-${crypto.randomBytes(5).toString('hex')}`;
  const res = await api('POST', '/v1/catalog/games/publish', { body: { slug, title: `Release ${slug}` }, token: creator.token });
  assert.equal(res.status, 201, JSON.stringify(res.body));
  return { slug, res };
}

async function uploadPackage(creator, slug, bytes) {
  const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  const ticket = await api('POST', `/v1/catalog/games/${slug}/package/upload-url`,
    { body: { sha256, size_bytes: bytes.length }, token: creator.token });
  if (ticket.status !== 200) return { ticket, sha256 };
  const put = await fetch(ticket.body.upload_url, { method: 'PUT', body: bytes, headers: { authorization: `Bearer ${creator.token}` } });
  assert.equal(put.status, 200);
  const confirm = await api('POST', `/v1/catalog/games/${slug}/package/confirm`, { body: { sha256 }, token: creator.token });
  return { ticket, confirm, sha256 };
}

async function uploadImage(creator, slug, kind, bytes, contentType) {
  const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  const ticket = await api('POST', `/v1/catalog/games/${slug}/media/upload-url`,
    { body: { kind, sha256, size_bytes: bytes.length, content_type: contentType }, token: creator.token });
  if (ticket.status !== 200) return { ticket };
  const put = await fetch(ticket.body.upload_url, { method: 'PUT', body: bytes, headers: { authorization: `Bearer ${creator.token}` } });
  assert.equal(put.status, 200);
  const confirm = await api('POST', `/v1/catalog/games/${slug}/media/confirm`,
    { body: { kind, sha256, content_type: contentType }, token: creator.token });
  return { ticket, confirm };
}

function png(width, height, salt = '') {
  const ihdr = Buffer.alloc(25);
  ihdr.writeUInt32BE(13, 0);
  ihdr.write('IHDR', 4, 'latin1');
  ihdr.writeUInt32BE(width, 8);
  ihdr.writeUInt32BE(height, 12);
  ihdr[16] = 8;
  ihdr[17] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), ihdr, Buffer.from(`IDAT${salt}`)]);
}

async function inspect(bytes) {
  return inspectPackageStream(Readable.from([bytes]));
}

test('archive inspection mirrors the engine reader and validator', async () => {
  assert.ok(isSafeRelativePath('Scripts/Main.lua'));
  for (const bad of ['../x', 'a/../../x', '/etc/passwd', 'C:/x', 'a\\b', '', 'a//b', './a']) {
    assert.equal(isSafeRelativePath(bad), false, bad);
  }

  const ok = await inspect(minimalGameArchive({ name: 'Hill' }));
  const manifest = validateGamePackage(ok);
  assert.equal(manifest.name, 'Hill');
  assert.deepEqual(manifest.scenes, ['default.scene']);
  assert.equal(manifest.file_count, 3);

  await assert.rejects(inspect(Buffer.from('not an archive at all')), /not a Kronos package/);
  await assert.rejects(inspect(buildArchive([{ path: '../escape.txt', data: 'x' }])), /unsafe path/);
  await assert.rejects(inspect(minimalGameArchive().subarray(0, 40)), /truncated/);
  await assert.rejects(inspect(Buffer.concat([minimalGameArchive(), Buffer.from('junk')])), /trailing data/);
  await assert.rejects(
    inspect(buildArchive([{ path: 'a.txt', data: 'x' }, { path: 'a.txt', data: 'y' }])), /twice/,
  );

  const cliFlag = await inspect(buildArchive([
    { path: 'game.gamemanifest', data: 'GAMEMANIFEST 1\nNAME X\nLAUNCHKIND CliFlag\nCLIFLAG --server\nEND\n' },
  ]));
  assert.throws(() => validateGamePackage(cliFlag), /scene-based/);

  const noProject = await inspect(buildArchive([
    { path: 'game.gamemanifest', data: 'GAMEMANIFEST 1\nNAME X\nPROJECTPATH project.project\nEND\n' },
  ]));
  assert.throws(() => validateGamePackage(noProject), /missing from the package/);

  const escapingScene = await inspect(buildArchive([
    { path: 'game.gamemanifest', data: 'GAMEMANIFEST 1\nNAME X\nPROJECTPATH project.project\nEND\n' },
    { path: 'project.project', data: 'PROJECT 1\nACTIVESCENE 0\nSCENE ../../outside.scene\nEND\n' },
  ]));
  assert.throws(() => validateGamePackage(escapingScene), /outside the package/);

  const missingScene = await inspect(buildArchive([
    { path: 'game.gamemanifest', data: 'GAMEMANIFEST 1\nNAME X\nPROJECTPATH project.project\nEND\n' },
    { path: 'project.project', data: 'PROJECT 1\nACTIVESCENE 0\nSCENE gone.scene\nEND\n' },
  ]));
  assert.throws(() => validateGamePackage(missingScene), /starting scene/);

  assert.deepEqual(imageInfo(png(640, 360)), { contentType: 'image/png', width: 640, height: 360 });
  const jpeg = Buffer.from([0xff, 0xd8, 0xff, 0xe0, 0x00, 0x04, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x01, 0x68, 0x02, 0x80, 0x03]);
  assert.deepEqual(imageInfo(jpeg), { contentType: 'image/jpeg', width: 640, height: 360 });
  assert.equal(imageInfo(Buffer.from('GIF89a')), null);
});

test('confirm rejects packages a server or player would refuse to run', async () => {
  const creator = await makeUser();
  const { slug } = await newGame(creator);

  const garbage = await uploadPackage(creator, slug, Buffer.from(`garbage ${crypto.randomBytes(8).toString('hex')}`));
  assert.equal(garbage.confirm.status, 400);
  assert.match(garbage.confirm.body.error.message, /rejected/);

  const traversal = await uploadPackage(creator, slug, buildArchive([
    { path: 'game.gamemanifest', data: 'GAMEMANIFEST 1\nNAME X\nPROJECTPATH ../../etc/project.project\nEND\n' },
  ]));
  assert.equal(traversal.confirm.status, 400);

  const versions = await api('GET', `/v1/catalog/games/${slug}/package/versions`, { token: creator.token });
  assert.equal(versions.body.versions.length, 0, 'nothing invalid became a version');
});

test('new games and versions wait for review, then go live; rollback and delete work', async () => {
  const creator = await makeUser();
  const admin = await makeUser({ admin: true });
  const stranger = await makeUser();
  const { slug, res: published } = await newGame(creator);
  assert.equal(published.body.game.review_status, 'pending');

  const listed = async () => {
    const res = await api('GET', `/v1/catalog/games?q=${encodeURIComponent(`Release ${slug}`)}`);
    return res.body.games.some((g) => g.slug === slug);
  };
  assert.equal(await listed(), false, 'a pending game is not listed');
  assert.equal((await api('GET', `/v1/catalog/games/${slug}`)).status, 404);

  const v1Bytes = minimalGameArchive({ name: `V1 ${slug}` });
  const v1 = await uploadPackage(creator, slug, v1Bytes);
  assert.equal(v1.confirm.status, 200, JSON.stringify(v1.confirm.body));
  assert.equal(v1.confirm.body.version.version_number, 1);
  assert.equal(v1.confirm.body.version.review_status, 'pending');
  assert.equal(v1.confirm.body.version.manifest.name, `V1 ${slug}`);

  const again = await api('POST', `/v1/catalog/games/${slug}/package/confirm`, { body: { sha256: v1.sha256 }, token: creator.token });
  assert.equal(again.body.version.version_number, 1, 're-confirming the same bytes does not create a second version');

  assert.equal((await api('GET', `/v1/catalog/games/${slug}/package`)).status, 404, 'players cannot download a pending game');
  const ownerPreview = await api('GET', `/v1/catalog/games/${slug}/package?version=1`, { token: creator.token });
  assert.equal(ownerPreview.status, 200, 'the creator can fetch their own pending version');
  assert.equal((await api('GET', `/v1/catalog/games/${slug}/package?version=1`, { token: stranger.token })).status, 404);
  assert.equal((await api('POST', '/v1/sessions/allocate', { body: { game_slug: slug }, token: creator.token })).status, 404,
    'an unreviewed game cannot be hosted');

  assert.equal((await api('GET', '/v1/moderation/catalog/pending', { token: stranger.token })).status, 403);
  const pending = await api('GET', '/v1/moderation/catalog/pending?limit=200', { token: admin.token });
  assert.equal(pending.status, 200);
  const item = pending.body.versions.find((v) => v.game.slug === slug);
  assert.ok(item, 'the version is in the review queue');
  const reviewerCopy = await fetch(item.download_url, { headers: { authorization: `Bearer ${admin.token}` } });
  assert.ok(Buffer.from(await reviewerCopy.arrayBuffer()).equals(v1Bytes), 'the reviewer downloads the exact upload');

  const noReason = await api('POST', `/v1/moderation/catalog/versions/${item.id}/review`, { body: { approve: false }, token: admin.token });
  assert.equal(noReason.status, 400, 'a rejection must say why');
  const approve = await api('POST', `/v1/moderation/catalog/versions/${item.id}/review`, { body: { approve: true }, token: admin.token });
  assert.equal(approve.status, 200, JSON.stringify(approve.body));
  assert.equal(approve.body.live, true);
  assert.equal((await api('POST', `/v1/moderation/catalog/versions/${item.id}/review`, { body: { approve: true }, token: admin.token })).status, 409);

  assert.equal(await listed(), true, 'the approved game is listed');
  let live = await api('GET', `/v1/catalog/games/${slug}/package`);
  assert.equal(live.status, 200);
  assert.equal(live.body.version_number, 1);

  const v2Bytes = minimalGameArchive({ name: `V2 ${slug}` });
  const v2 = await uploadPackage(creator, slug, v2Bytes);
  assert.equal(v2.confirm.body.version.review_status, 'pending');
  live = await api('GET', `/v1/catalog/games/${slug}/package`);
  assert.equal(live.body.version_number, 1, 'players keep the approved version while the update is reviewed');

  const queued = (await api('GET', '/v1/moderation/catalog/pending?limit=200', { token: admin.token })).body.versions
    .find((v) => v.game.slug === slug);
  await api('POST', `/v1/moderation/catalog/versions/${queued.id}/review`, { body: { approve: true }, token: admin.token });
  live = await api('GET', `/v1/catalog/games/${slug}/package`);
  assert.equal(live.body.version_number, 2);

  const rollback = await api('POST', `/v1/catalog/games/${slug}/package/versions/1/activate`, { token: creator.token });
  assert.equal(rollback.status, 200, JSON.stringify(rollback.body));
  live = await api('GET', `/v1/catalog/games/${slug}/package`);
  assert.equal(live.body.version_number, 1, 'rollback is instant');
  const downloaded = Buffer.from(await (await fetch(live.body.download_url)).arrayBuffer());
  assert.ok(downloaded.equals(v1Bytes), 'players download the rolled-back bytes');
  assert.equal((await api('POST', `/v1/catalog/games/${slug}/package/versions/2/activate`, { token: stranger.token })).status, 403);

  const v3 = await uploadPackage(creator, slug, minimalGameArchive({ name: `V3 ${slug}` }));
  const v3Item = (await api('GET', '/v1/moderation/catalog/pending?limit=200', { token: admin.token })).body.versions
    .find((v) => v.game.slug === slug);
  await api('POST', `/v1/moderation/catalog/versions/${v3Item.id}/review`,
    { body: { approve: false, note: 'Crashes on load' }, token: admin.token });
  const activateRejected = await api('POST', `/v1/catalog/games/${slug}/package/versions/3/activate`, { token: creator.token });
  assert.equal(activateRejected.status, 409, 'a rejected version can never go live');
  const history = await api('GET', `/v1/catalog/games/${slug}/package/versions`, { token: creator.token });
  assert.deepEqual(history.body.versions.map((v) => [v.version_number, v.review_status, v.current]),
    [[3, 'rejected', false], [2, 'approved', false], [1, 'approved', true]]);
  assert.equal(history.body.versions[0].review_note, 'Crashes on load', 'the creator sees why');
  assert.ok(v3.confirm.body.version);

  assert.equal((await api('DELETE', `/v1/catalog/games/${slug}/package/versions/1`, { token: creator.token })).status, 409,
    'the live version cannot be deleted');
  const objectPath = path.join(storageDir, 'packages', `${v2.sha256}.kronos`);
  await fsp.access(objectPath);
  const deleted = await api('DELETE', `/v1/catalog/games/${slug}/package/versions/2`, { token: creator.token });
  assert.equal(deleted.status, 200);
  await assert.rejects(fsp.access(objectPath), 'the unreferenced object is removed from storage');

  const { rows: actions } = await query(
    `SELECT action_type FROM moderation_actions WHERE target_user_id = $1 ORDER BY id`, [creator.id],
  );
  assert.deepEqual(actions.map((a) => a.action_type), ['approve_game_version', 'approve_game_version', 'reject_game_version']);
});

test('a rejected first version keeps the game unlisted until a fix is approved', async () => {
  const creator = await makeUser();
  const admin = await makeUser({ admin: true });
  const { slug } = await newGame(creator);
  await uploadPackage(creator, slug, minimalGameArchive({ name: `Bad ${slug}` }));
  const item = (await api('GET', '/v1/moderation/catalog/pending?limit=200', { token: admin.token })).body.versions
    .find((v) => v.game.slug === slug);
  await api('POST', `/v1/moderation/catalog/versions/${item.id}/review`, { body: { approve: false, note: 'Offensive name' }, token: admin.token });
  let { rows } = await query(`SELECT review_status FROM games WHERE slug = $1`, [slug]);
  assert.equal(rows[0].review_status, 'rejected');

  await uploadPackage(creator, slug, minimalGameArchive({ name: `Fixed ${slug}` }));
  ({ rows } = await query(`SELECT review_status FROM games WHERE slug = $1`, [slug]));
  assert.equal(rows[0].review_status, 'pending', 'a resubmission goes back into the queue');
});

test('uploads count against a per-account storage quota', async () => {
  const creator = await makeUser();
  const { slug } = await newGame(creator);
  const first = minimalGameArchive({ name: `Quota ${slug}` });
  await query(`UPDATE users SET storage_quota_bytes = $2 WHERE id = $1`, [creator.id, first.length + 100]);

  const ok = await uploadPackage(creator, slug, first);
  assert.equal(ok.confirm.status, 200);
  const storage = await api('GET', '/v1/catalog/storage', { token: creator.token });
  assert.equal(storage.body.storage.used_bytes, first.length);
  assert.equal(storage.body.storage.quota_bytes, first.length + 100);

  const bigger = minimalGameArchive({ name: `Quota ${slug}`, extra: [{ path: 'big.bin', data: crypto.randomBytes(4096) }] });
  const over = await uploadPackage(creator, slug, bigger);
  assert.equal(over.ticket.status, 413);
  assert.equal(over.ticket.body.error.code, 'quota_exceeded');
  assert.match(over.ticket.body.error.message, /free/);

  const { slug: second } = await newGame(creator);
  const reused = await uploadPackage(creator, second, first);
  assert.equal(reused.confirm.status, 200, 'bytes the account already stores cost nothing again');
});

test('thumbnails and screenshots are validated, reviewed and served', async () => {
  const creator = await makeUser();
  const admin = await makeUser({ admin: true });
  const { slug } = await newGame(creator);

  const wrongType = await uploadImage(creator, slug, 'thumbnail', png(640, 360, 'a'), 'image/jpeg');
  assert.equal(wrongType.confirm.status, 400, 'PNG bytes declared as JPEG are refused');
  const huge = await uploadImage(creator, slug, 'thumbnail', png(9000, 9000, 'b'), 'image/png');
  assert.equal(huge.confirm.status, 400);
  const gif = await uploadImage(creator, slug, 'screenshot', Buffer.from(`GIF89a${crypto.randomBytes(8).toString('hex')}`), 'image/png');
  assert.equal(gif.confirm.status, 400);

  const thumb = await uploadImage(creator, slug, 'thumbnail', png(640, 360, slug), 'image/png');
  assert.equal(thumb.confirm.status, 201, JSON.stringify(thumb.confirm.body));
  const media = thumb.confirm.body.media;
  assert.equal(media.review_status, 'pending');
  assert.equal((await api('GET', `/v1/catalog/media/${media.id}`)).status, 404, 'a pending image is private');
  const ownerView = await fetch(`${baseUrl}/v1/catalog/media/${media.id}`, { headers: { authorization: `Bearer ${creator.token}` } });
  assert.equal(ownerView.status, 200);

  const queue = await api('GET', '/v1/moderation/catalog/pending?limit=200', { token: admin.token });
  const queued = queue.body.media.find((m) => m.id === media.id);
  assert.ok(queued);
  await api('POST', `/v1/moderation/catalog/media/${media.id}/review`, { body: { approve: true }, token: admin.token });

  const served = await fetch(`${baseUrl}/v1/catalog/media/${media.id}`);
  assert.equal(served.status, 200);
  assert.equal(served.headers.get('content-type'), 'image/png');
  const { rows } = await query(`SELECT thumbnail_url FROM games WHERE slug = $1`, [slug]);
  assert.equal(rows[0].thumbnail_url, `${baseUrl}/v1/catalog/media/${media.id}`);

  const republish = await api('POST', '/v1/catalog/games/publish',
    { body: { slug, title: 'Renamed', thumbnail_url: 'https://cdn.example/x.png' }, token: creator.token });
  assert.equal(republish.status, 200);
  const { rows: kept } = await query(`SELECT thumbnail_url FROM games WHERE slug = $1`, [slug]);
  assert.equal(kept[0].thumbnail_url, rows[0].thumbnail_url, 'republishing keeps the uploaded thumbnail');

  const original = config.maxScreenshotsPerGame;
  config.maxScreenshotsPerGame = 2;
  try {
    const shots = [];
    for (let i = 0; i < 2; i += 1) {
      const shot = await uploadImage(creator, slug, 'screenshot', png(1280, 720, `${slug}-${i}`), 'image/png');
      assert.equal(shot.confirm.status, 201);
      shots.push(shot.confirm.body.media);
    }
    const third = await uploadImage(creator, slug, 'screenshot', png(1280, 720, `${slug}-3`), 'image/png');
    assert.equal(third.confirm.status, 409);
    for (const shot of shots) {
      await api('POST', `/v1/moderation/catalog/media/${shot.id}/review`, { body: { approve: true }, token: admin.token });
    }
    const removed = await api('DELETE', `/v1/catalog/games/${slug}/media/${shots[1].id}`, { token: creator.token });
    assert.equal(removed.status, 200);
  } finally {
    config.maxScreenshotsPerGame = original;
  }

  const listing = await api('GET', `/v1/catalog/games/${slug}/media`, { token: creator.token });
  assert.deepEqual(listing.body.media.map((m) => m.kind).sort(), ['screenshot', 'thumbnail']);
});

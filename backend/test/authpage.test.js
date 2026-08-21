// The browser sign-in page. Most of these tests are about ONE thing: the
// page hands a real refresh token to whatever `redirect_uri` says, so an
// unvalidated value there is an open redirect that mails working
// credentials to an attacker. That is the classic way these flows break,
// so it gets adversarial coverage rather than a happy-path smoke test.
import assert from 'node:assert/strict';
import test, { after, before } from 'node:test';

import { createApp } from '../src/server.js';
import { pool } from '../src/db.js';
import { redis } from '../src/redis.js';
import { isSafeLoopbackRedirect } from '../src/web/authPage.js';

let server;
let baseUrl;

before(async () => {
  server = createApp().listen(0);
  await new Promise((r) => server.once('listening', r));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  server.close();
  await pool.end();
  redis.disconnect();
});

test('loopback redirect validation accepts only real loopback targets', () => {
  assert.equal(isSafeLoopbackRedirect('http://127.0.0.1:8765/auth/callback'), true);
  assert.equal(isSafeLoopbackRedirect('http://localhost:8765/auth/callback'), true);
  assert.equal(isSafeLoopbackRedirect('http://[::1]:8765/auth/callback'), true);
});

test('loopback redirect validation rejects every escape route', () => {
  const hostile = [
    // Plain remote hosts.
    'http://evil.example/steal',
    'https://evil.example/steal',
    // Embedded credentials -- parses as hostname evil.example, and a naive
    // "does it contain 127.0.0.1" check would wave this through.
    'http://127.0.0.1@evil.example/steal',
    'http://localhost@evil.example/steal',
    // Lookalike hostnames.
    'http://127.0.0.1.evil.example/steal',
    'http://notlocalhost:8765/x',
    // Non-http schemes.
    'javascript:alert(1)',
    'data:text/html,<script>alert(1)</script>',
    'file:///etc/passwd',
    // Loopback but no port: cannot identify a real listener.
    'http://127.0.0.1/auth/callback',
    // Privileged / nonsense ports.
    'http://127.0.0.1:80/auth/callback',
    'http://127.0.0.1:0/auth/callback',
    'http://127.0.0.1:99999/auth/callback',
    // Garbage.
    '',
    'not a url',
    '//evil.example',
  ];
  for (const value of hostile) {
    assert.equal(isSafeLoopbackRedirect(value), false, `must reject: ${value}`);
  }
});

test('/auth/start serves the page for a valid loopback redirect', async () => {
  const url = `${baseUrl}/auth/start?redirect_uri=${encodeURIComponent('http://127.0.0.1:8765/auth/callback')}&state=abc123`;
  const res = await fetch(url);
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-type') || '', /html/);
  // A page embedding a one-time state must never be cached.
  assert.match(res.headers.get('cache-control') || '', /no-store/);

  const html = await res.text();
  assert.match(html, /KRONOS/);
  assert.match(html, /Play as Guest/, 'the guest path is offered on the page, per spec');
  assert.match(html, /Create Account/);
  assert.match(html, /127\.0\.0\.1:8765/, 'the validated redirect really reaches the page');
});

test('/auth/start refuses a hostile redirect and sends the user nowhere', async () => {
  const res = await fetch(
    `${baseUrl}/auth/start?redirect_uri=${encodeURIComponent('http://evil.example/steal')}&state=abc123`,
    { redirect: 'manual' },
  );
  assert.equal(res.status, 400);
  // Critically: it must not redirect. Bouncing the user to an untrusted
  // target is the exact failure this guards against.
  assert.equal(res.headers.get('location'), null, 'a bad redirect target must never be redirected to');
  const html = await res.text();
  assert.ok(!html.includes('evil.example'), 'the hostile value is not reflected back into the page');
});

test('/auth/start rejects a malformed or oversized state', async () => {
  const good = encodeURIComponent('http://127.0.0.1:8765/auth/callback');
  assert.equal((await fetch(`${baseUrl}/auth/start?redirect_uri=${good}`)).status, 400, 'missing state');
  assert.equal((await fetch(`${baseUrl}/auth/start?redirect_uri=${good}&state=${'a'.repeat(200)}`)).status, 400,
    'oversized state');
  assert.equal((await fetch(`${baseUrl}/auth/start?redirect_uri=${good}&state=${encodeURIComponent('<script>')}`)).status,
    400, 'state with markup');
});

test('the page never reflects unescaped input into markup', async () => {
  // state is charset-restricted, so injection has to be attempted through
  // the redirect -- which is also validated. Belt and braces: confirm a
  // crafted-but-valid loopback URL containing a quote is escaped.
  const tricky = 'http://127.0.0.1:8765/cb?x=%22%3E%3Cscript%3Ealert(1)%3C/script%3E';
  const res = await fetch(`${baseUrl}/auth/start?redirect_uri=${encodeURIComponent(tricky)}&state=abc`);
  assert.equal(res.status, 200);
  const html = await res.text();
  assert.ok(!html.includes('<script>alert(1)</script>'), 'injected markup must not survive into the page');
});

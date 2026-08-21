import express from 'express';

import { config } from '../config.js';

export const authPageRouter = express.Router();

// ---------------------------------------------------------------------------
// Redirect-URI validation.
//
// This is the single most security-critical function in this file. The page
// hands a real refresh token to whatever `redirect_uri` says, so an
// unvalidated value here is an open redirect that mails working credentials
// to an attacker's server -- the classic way OAuth-style flows get broken.
//
// Only loopback is ever allowed, because the only legitimate consumer is a
// LoopbackHttpServer running on the user's own machine. Anything else --
// any remote host, any non-http scheme, any userinfo trick -- is refused
// outright rather than sanitised, because "clever normalisation" is exactly
// where these bugs live.
// ---------------------------------------------------------------------------
export function isSafeLoopbackRedirect(value) {
  let url;
  try {
    url = new URL(String(value));
  } catch {
    return false;
  }
  // http only: the loopback listener is plain HTTP by necessity, and
  // allowing other schemes opens javascript:/data: injection.
  if (url.protocol !== 'http:') return false;
  // Reject embedded credentials -- "http://127.0.0.1@evil.example" parses
  // with hostname evil.example in some naive checks.
  if (url.username || url.password) return false;
  if (url.hostname !== '127.0.0.1' && url.hostname !== 'localhost' && url.hostname !== '[::1]') return false;
  // A port is required: it is what identifies the specific listener this
  // launcher opened.
  if (!url.port) return false;
  const port = Number(url.port);
  if (!Number.isInteger(port) || port < 1024 || port > 65535) return false;
  return true;
}

const escapeHtml = (value) =>
  String(value).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

function renderPage({ redirectUri, state, googleClientId }) {
  // The page talks to the SAME JSON endpoints the C++ client would have --
  // /v1/auth/login, /signup, /guest -- so there is one authentication
  // implementation, already tested, rather than a second one living here.
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sign in to Kronos</title>
<style>
  :root {
    --charcoal:#191B1D; --slate:#232527; --sky:#4EA8DE; --green:#00B259;
    --green-hover:#13c96b; --text:#fff; --muted:#ccc; --border:#34373a; --danger:#e0574d;
  }
  * { box-sizing:border-box; }
  body {
    margin:0; min-height:100vh; display:flex; align-items:center; justify-content:center;
    background:var(--charcoal); color:var(--text); padding:24px;
    font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  }
  .card {
    width:100%; max-width:400px; background:var(--slate); border:1px solid var(--border);
    border-radius:14px; padding:32px;
  }
  .brand { font-size:30px; letter-spacing:.16em; font-weight:600; text-align:center; margin-bottom:4px; }
  .sub { text-align:center; color:var(--muted); font-size:14px; margin-bottom:26px; }
  label { display:block; font-size:13px; color:var(--muted); margin:14px 0 6px; }
  input {
    width:100%; padding:11px 12px; border-radius:8px; border:1px solid var(--border);
    background:#1b1d1f; color:var(--text); font-size:15px;
  }
  input:focus { outline:none; border-color:var(--sky); }
  button {
    width:100%; padding:12px; border:0; border-radius:8px; font-size:15px; font-weight:600;
    cursor:pointer; margin-top:18px; color:#fff; background:var(--green);
  }
  button:hover:not(:disabled) { background:var(--green-hover); }
  button:disabled { opacity:.55; cursor:default; }
  button.secondary { background:transparent; border:1px solid var(--border); color:var(--muted); margin-top:10px; }
  button.secondary:hover:not(:disabled) { border-color:var(--sky); color:var(--text); background:transparent; }
  .row { display:flex; gap:10px; margin-top:22px; }
  .row button { margin-top:0; }
  .tab {
    flex:1; background:transparent; border:0; border-bottom:2px solid var(--border);
    color:var(--muted); padding:10px; border-radius:0; font-weight:600; margin-top:0;
  }
  .tab.active { color:var(--text); border-bottom-color:var(--sky); }
  .msg { margin-top:16px; font-size:13.5px; min-height:19px; }
  .msg.error { color:var(--danger); }
  .msg.ok { color:var(--green); }
  .divider { display:flex; align-items:center; gap:12px; color:var(--muted); font-size:12px; margin:22px 0 4px; }
  .divider::before, .divider::after { content:""; flex:1; height:1px; background:var(--border); }
  .foot { margin-top:22px; font-size:12px; color:var(--muted); text-align:center; line-height:1.5; }
</style>
</head>
<body>
<main class="card">
  <div class="brand">KRONOS</div>
  <div class="sub">Sign in to continue in the Kronos client</div>

  <div class="row" role="tablist" style="margin-top:0">
    <button class="tab active" id="tab-signin" role="tab">Sign In</button>
    <button class="tab" id="tab-signup" role="tab">Create Account</button>
  </div>

  <form id="form" autocomplete="on">
    <label for="email">Email</label>
    <input id="email" name="email" type="email" required autocomplete="username">
    <label for="password">Password</label>
    <input id="password" name="password" type="password" required autocomplete="current-password" minlength="10">
    <button id="submit" type="submit">Sign In</button>
  </form>

  <div class="divider">or</div>
  <button class="secondary" id="guest" type="button">Play as Guest</button>

  <div class="msg" id="msg" role="status" aria-live="polite"></div>
  <div class="foot">
    Playing as a guest keeps nothing between sessions and cannot add friends.
    You can create an account later without losing your device's local games.
  </div>
</main>

<script>
(function () {
  var REDIRECT = ${JSON.stringify(redirectUri)};
  var STATE = ${JSON.stringify(state)};

  var msg = document.getElementById('msg');
  var form = document.getElementById('form');
  var submit = document.getElementById('submit');
  var guestBtn = document.getElementById('guest');
  var tabSignIn = document.getElementById('tab-signin');
  var tabSignUp = document.getElementById('tab-signup');
  var mode = 'signin';

  function say(text, kind) { msg.textContent = text; msg.className = 'msg ' + (kind || ''); }
  function busy(on) { submit.disabled = on; guestBtn.disabled = on; }

  function setMode(next) {
    mode = next;
    tabSignIn.classList.toggle('active', next === 'signin');
    tabSignUp.classList.toggle('active', next === 'signup');
    submit.textContent = next === 'signin' ? 'Sign In' : 'Create Account';
    document.getElementById('password').setAttribute(
      'autocomplete', next === 'signin' ? 'current-password' : 'new-password');
    say('');
  }
  tabSignIn.addEventListener('click', function () { setMode('signin'); });
  tabSignUp.addEventListener('click', function () { setMode('signup'); });

  // Hands the refresh token back to the launcher's loopback listener. The
  // server already validated REDIRECT points at loopback, so this cannot
  // be pointed at a remote host.
  function handOff(refreshToken) {
    var url = REDIRECT
      + (REDIRECT.indexOf('?') >= 0 ? '&' : '?')
      + 'code=' + encodeURIComponent(refreshToken)
      + '&state=' + encodeURIComponent(STATE);
    say('Signed in. Returning you to Kronos...', 'ok');
    window.location.replace(url);
  }

  async function post(path, body) {
    var res = await fetch(path, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(body || {}),
    });
    var data = null;
    try { data = await res.json(); } catch (e) { /* empty body */ }
    return { ok: res.ok, data: data };
  }

  form.addEventListener('submit', async function (event) {
    event.preventDefault();
    busy(true);
    say(mode === 'signin' ? 'Signing in...' : 'Creating your account...');
    var result = await post(mode === 'signin' ? '/v1/auth/login' : '/v1/auth/signup', {
      email: document.getElementById('email').value,
      password: document.getElementById('password').value,
    });
    busy(false);
    if (!result.ok || !result.data || !result.data.refresh_token) {
      say((result.data && result.data.error && result.data.error.message) || 'Something went wrong.', 'error');
      return;
    }
    handOff(result.data.refresh_token);
  });

  guestBtn.addEventListener('click', async function () {
    busy(true);
    say('Setting up a guest session...');
    var result = await post('/v1/auth/guest', {});
    busy(false);
    if (!result.ok || !result.data || !result.data.refresh_token) {
      say((result.data && result.data.error && result.data.error.message) || 'Could not start a guest session.', 'error');
      return;
    }
    handOff(result.data.refresh_token);
  });

  setMode('signin');
})();
</script>
</body>
</html>`;
}

authPageRouter.get('/auth/start', (req, res) => {
  const redirectUri = String(req.query.redirect_uri || '');
  const state = String(req.query.state || '');

  if (!isSafeLoopbackRedirect(redirectUri)) {
    // Deliberately rendered as a plain page rather than redirected
    // anywhere: if the redirect target is untrustworthy, the one thing we
    // must not do is send the user (or a token) to it.
    return res.status(400).type('html').send(
      `<!doctype html><meta charset="utf-8"><body style="background:#191B1D;color:#fff;font-family:system-ui;padding:40px">
       <h1 style="letter-spacing:.16em">KRONOS</h1>
       <p>This sign-in link is not valid. Please start sign-in from the Kronos client.</p></body>`,
    );
  }
  if (state.length === 0 || state.length > 128 || !/^[A-Za-z0-9._~-]+$/.test(state)) {
    return res.status(400).type('html').send('<!doctype html><meta charset="utf-8"><p>Invalid sign-in request.</p>');
  }

  // This page must never be cached: it embeds a one-time state value.
  res.setHeader('Cache-Control', 'no-store');
  res.setHeader('X-Frame-Options', 'DENY');
  res.setHeader(
    'Content-Security-Policy',
    "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'none'",
  );
  res.type('html').send(
    renderPage({ redirectUri: escapeHtml(redirectUri), state: escapeHtml(state), googleClientId: config.googleClientId }),
  );
});

import crypto from 'node:crypto';
import { SignJWT, jwtVerify } from 'jose';

import { config } from '../config.js';
import { query, withTransaction } from '../db.js';

const jwtKey = new TextEncoder().encode(config.jwtSecret);

// ---------------------------------------------------------------------------
// Access tokens (short-lived, stateless)
// ---------------------------------------------------------------------------

export async function issueAccessToken(user) {
  return new SignJWT({
    email: user.email,
    display_name: user.display_name,
    email_verified: user.email_verified,
  })
    .setProtectedHeader({ alg: 'HS256', typ: 'JWT' })
    .setSubject(String(user.id))
    .setIssuer(config.jwtIssuer)
    .setAudience(config.jwtAudience)
    .setIssuedAt()
    .setExpirationTime(`${config.accessTokenTtlSeconds}s`)
    .sign(jwtKey);
}

export async function verifyAccessToken(token) {
  // Issuer and audience are verified, not just the signature. A validly
  // signed token minted for a different audience must not be accepted.
  const { payload } = await jwtVerify(token, jwtKey, {
    issuer: config.jwtIssuer,
    audience: config.jwtAudience,
    algorithms: ['HS256'],
  });
  return payload;
}

// ---------------------------------------------------------------------------
// Refresh tokens (long-lived, stateful, rotating, revocable)
// ---------------------------------------------------------------------------

const sha256 = (value) => crypto.createHash('sha256').update(value).digest('hex');

// The refresh token itself is high-entropy random, NOT a JWT. It carries
// no claims and means nothing on its own -- all authority lives in the
// database row it hashes to. That makes revocation genuinely immediate
// rather than "immediate once the token expires".
function generateRefreshToken() {
  return crypto.randomBytes(32).toString('base64url');
}

export async function issueRefreshToken(userId, { familyId = crypto.randomUUID(), userAgent = null } = {}) {
  const token = generateRefreshToken();
  const expiresAt = new Date(Date.now() + config.refreshTokenTtlSeconds * 1000);
  await query(
    `INSERT INTO refresh_tokens (user_id, token_hash, family_id, expires_at, user_agent)
     VALUES ($1, $2, $3, $4, $5)`,
    [userId, sha256(token), familyId, expiresAt, userAgent],
  );
  return { token, familyId, expiresAt };
}

export class RefreshTokenReuseError extends Error {}

// Rotates a refresh token: the presented token is consumed and a fresh
// one issued, atomically.
//
// Reuse detection: if a token that has already been rotated away is
// presented again, either the client is badly broken or the token was
// stolen and replayed. We cannot tell which, so we assume theft and
// revoke the entire family -- forcing a real re-login is a far better
// outcome than leaving a thief with a working session.
export async function rotateRefreshToken(presentedToken, { userAgent = null } = {}) {
  const presentedHash = sha256(presentedToken);

  const outcome = await withTransaction(async (client) => {
    const { rows } = await client.query(
      `SELECT id, user_id, family_id, expires_at, revoked_at, replaced_by
         FROM refresh_tokens
        WHERE token_hash = $1
        FOR UPDATE`,
      [presentedHash],
    );
    if (rows.length === 0) return null;
    const row = rows[0];

    // Reuse is reported back to the caller rather than thrown from here.
    // Throwing inside the transaction would ROLL BACK the very revocation
    // we need to persist -- found by the reuse test, which saw the
    // newer token still working after a detected replay.
    if (row.replaced_by !== null || row.revoked_at !== null) {
      return { reuseDetectedForFamily: row.family_id };
    }

    if (new Date(row.expires_at).getTime() <= Date.now()) return null;

    const newToken = generateRefreshToken();
    const newHash = sha256(newToken);
    const expiresAt = new Date(Date.now() + config.refreshTokenTtlSeconds * 1000);

    await client.query(
      `INSERT INTO refresh_tokens (user_id, token_hash, family_id, expires_at, user_agent)
       VALUES ($1, $2, $3, $4, $5)`,
      [row.user_id, newHash, row.family_id, expiresAt, userAgent],
    );
    await client.query(
      `UPDATE refresh_tokens SET replaced_by = $1, revoked_at = NOW() WHERE id = $2`,
      [newHash, row.id],
    );

    return { token: newToken, userId: row.user_id, familyId: row.family_id, expiresAt };
  });

  if (outcome && outcome.reuseDetectedForFamily) {
    // Committed on its own, after the read transaction has ended, so the
    // revocation actually survives.
    await query(
      `UPDATE refresh_tokens SET revoked_at = NOW() WHERE family_id = $1 AND revoked_at IS NULL`,
      [outcome.reuseDetectedForFamily],
    );
    throw new RefreshTokenReuseError('Refresh token was reused; the whole token family has been revoked.');
  }

  return outcome;
}

export async function revokeRefreshToken(presentedToken) {
  const { rowCount } = await query(
    `UPDATE refresh_tokens SET revoked_at = NOW() WHERE token_hash = $1 AND revoked_at IS NULL`,
    [sha256(presentedToken)],
  );
  return rowCount > 0;
}

export async function revokeAllForUser(userId) {
  const { rowCount } = await query(
    `UPDATE refresh_tokens SET revoked_at = NOW() WHERE user_id = $1 AND revoked_at IS NULL`,
    [userId],
  );
  return rowCount;
}

// ---------------------------------------------------------------------------
// One-shot tokens (password reset, email verification)
// ---------------------------------------------------------------------------

// Only the hash is stored. A leaked database therefore does not hand an
// attacker a set of working reset links.
export async function issueOneShotToken(table, userId, ttlSeconds) {
  const token = crypto.randomBytes(32).toString('base64url');
  const expiresAt = new Date(Date.now() + ttlSeconds * 1000);
  await query(
    `INSERT INTO ${table} (user_id, token_hash, expires_at) VALUES ($1, $2, $3)`,
    [userId, sha256(token), expiresAt],
  );
  return { token, expiresAt };
}

// Consumes a one-shot token, returning the user id it belonged to, or
// null if it is unknown, expired, or already used. The UPDATE ... WHERE
// used_at IS NULL is what makes "single use" atomic: two concurrent
// requests cannot both win.
export async function consumeOneShotToken(table, presentedToken) {
  const { rows } = await query(
    `UPDATE ${table}
        SET used_at = NOW()
      WHERE token_hash = $1
        AND used_at IS NULL
        AND expires_at > NOW()
      RETURNING user_id`,
    [sha256(presentedToken)],
  );
  return rows.length > 0 ? rows[0].user_id : null;
}

// ---------------------------------------------------------------------------
// Join tickets -- short-lived proof, for a game server, that the bearer
// was really allocated to it by us.
// ---------------------------------------------------------------------------

// Signed with a secret distinct from the login-token secret, so a game
// server can verify tickets without holding the key that mints sessions.
export function issueJoinTicket({ userId, gameId, serverKey }) {
  const payload = {
    uid: String(userId),
    gid: String(gameId),
    srv: serverKey,
    exp: Math.floor(Date.now() / 1000) + config.joinTicketTtlSeconds,
    jti: crypto.randomUUID(),
  };
  const body = Buffer.from(JSON.stringify(payload)).toString('base64url');
  const signature = crypto.createHmac('sha256', config.joinTicketSecret).update(body).digest('base64url');
  return `${body}.${signature}`;
}

export function verifyJoinTicket(ticket) {
  if (typeof ticket !== 'string' || !ticket.includes('.')) return null;
  const [body, signature] = ticket.split('.', 2);

  const expected = crypto.createHmac('sha256', config.joinTicketSecret).update(body).digest('base64url');
  const a = Buffer.from(signature);
  const b = Buffer.from(expected);
  if (a.length !== b.length || !crypto.timingSafeEqual(a, b)) return null;

  let payload;
  try {
    payload = JSON.parse(Buffer.from(body, 'base64url').toString('utf8'));
  } catch {
    return null;
  }
  if (typeof payload.exp !== 'number' || payload.exp * 1000 <= Date.now()) return null;
  return payload;
}

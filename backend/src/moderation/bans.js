import crypto from 'node:crypto';

import { query } from '../db.js';

// Values are hashed before storage and before lookup. An email address and
// an IP address are both personal data, and this subsystem only ever needs
// to answer "have I seen this before?" -- which a hash answers exactly as
// well as the plaintext, while leaking far less if the table is ever dumped.
export const hashIdentifier = (value) =>
  crypto.createHash('sha256').update(String(value).trim().toLowerCase()).digest('hex');

// A deliberately small, embedded list of the most common disposable-email
// providers rather than a package dependency.
//
// Honest limitations, stated because they matter for how much weight this
// should carry: the real lists run to tens of thousands of domains and go
// stale constantly, new throwaway domains appear daily, and any list will
// eventually block somebody's legitimate niche provider. This is friction
// against casual abuse, not a wall -- so it is deliberately NOT the only
// thing standing between a banned user and a new account (see the
// multi-layer identifier checks below).
const DISPOSABLE_DOMAINS = new Set([
  'mailinator.com', 'guerrillamail.com', 'guerrillamail.net', '10minutemail.com', 'tempmail.com',
  'temp-mail.org', 'throwawaymail.com', 'yopmail.com', 'trashmail.com', 'sharklasers.com',
  'getnada.com', 'dispostable.com', 'maildrop.cc', 'fakeinbox.com', 'mailnesia.com',
  'spamgourmet.com', 'mytemp.email', 'moakt.com', 'tempr.email', 'emailondeck.com',
]);

export function isDisposableEmailDomain(email) {
  const at = String(email || '').lastIndexOf('@');
  if (at < 0) return false;
  const domain = email.slice(at + 1).trim().toLowerCase();
  if (DISPOSABLE_DOMAINS.has(domain)) return true;
  // Also catch obvious subdomains of a listed provider.
  for (const known of DISPOSABLE_DOMAINS) {
    if (domain.endsWith('.' + known)) return true;
  }
  return false;
}

// Returns the matching ban record, or null. Checks every supplied
// identifier, so an evader who changes only their email is still caught by
// hwid or ip.
export async function findActiveBan({ email, hwid, ip }) {
  const candidates = [];
  if (email) candidates.push(['email', hashIdentifier(email)]);
  if (hwid) candidates.push(['hwid', hashIdentifier(hwid)]);
  if (ip) candidates.push(['ip', hashIdentifier(ip)]);
  if (candidates.length === 0) return null;

  const { rows } = await query(
    `SELECT kind, reason, expires_at
       FROM banned_identifiers
      WHERE lifted_at IS NULL
        AND (expires_at IS NULL OR expires_at > NOW())
        AND (kind, value_hash) IN (${candidates.map((_, i) => `($${i * 2 + 1}, $${i * 2 + 2})`).join(', ')})
      LIMIT 1`,
    candidates.flat(),
  );
  return rows.length > 0 ? rows[0] : null;
}

export const USERNAME_LOCK_DAYS = 30;

// Terminates an account: records ban identifiers, and holds the username
// for USERNAME_LOCK_DAYS so a successful appeal inside that window can give
// it straight back.
export async function terminateAccount(userId, { reason = '', hwid = null, ip = null, expiresAt = null } = {}) {
  const { rows } = await query(`SELECT email, username FROM users WHERE id = $1`, [userId]);
  if (rows.length === 0) return null;
  const user = rows[0];

  await query(
    `UPDATE users
        SET account_state = 'terminated',
            terminated_at = NOW(),
            username_locked_until = NOW() + INTERVAL '${USERNAME_LOCK_DAYS} days',
            updated_at = NOW()
      WHERE id = $1`,
    [userId],
  );

  const identifiers = [];
  if (user.email) identifiers.push(['email', hashIdentifier(user.email)]);
  if (hwid) identifiers.push(['hwid', hashIdentifier(hwid)]);
  if (ip) identifiers.push(['ip', hashIdentifier(ip)]);
  for (const [kind, valueHash] of identifiers) {
    // ON CONFLICT DO NOTHING: re-banning an already-banned identifier is
    // an ordinary no-op, not an error.
    await query(
      `INSERT INTO banned_identifiers (kind, value_hash, user_id, reason, expires_at)
       VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (kind, value_hash) WHERE lifted_at IS NULL DO NOTHING`,
      [kind, valueHash, userId, reason, expiresAt],
    );
  }

  return { userId, lockedUsername: user.username };
}

// Releases every username whose lock has expired, returning it to the
// public pool. Idempotent, so running it twice (or a missed run followed by
// a catch-up run) is harmless -- which is what a cron job needs.
export async function recycleExpiredUsernames() {
  const { rows } = await query(
    `UPDATE users
        SET username = NULL,
            username_lower = NULL,
            username_locked_until = NULL,
            updated_at = NOW()
      WHERE account_state = 'terminated'
        AND username_lower IS NOT NULL
        AND username_locked_until IS NOT NULL
        AND username_locked_until <= NOW()
      RETURNING id`,
  );
  return rows.map((r) => String(r.id));
}

// Grants an appeal.
//
// The interesting case is the one the spec calls out: if the handle was
// already recycled and somebody else took it, we cannot take it back off
// them -- so the account is reinstated with its data intact and flagged
// REQUIRES_RENAME, which forces a free username choice at next login.
// Silently reinstating with a NULL username would leave an account that
// cannot be searched for or friended, which looks like data loss.
export async function grantAppeal(appealId) {
  const { rows: appeals } = await query(
    `UPDATE ban_appeals SET status = 'granted', resolved_at = NOW()
      WHERE id = $1 AND status = 'open'
      RETURNING user_id`,
    [appealId],
  );
  if (appeals.length === 0) return null;
  const userId = appeals[0].user_id;

  await query(
    `UPDATE banned_identifiers SET lifted_at = NOW() WHERE user_id = $1 AND lifted_at IS NULL`,
    [userId],
  );

  const { rows: users } = await query(`SELECT username_lower FROM users WHERE id = $1`, [userId]);
  const stillHoldsUsername = users.length > 0 && users[0].username_lower !== null;

  const newState = stillHoldsUsername ? 'active' : 'requires_rename';
  await query(
    `UPDATE users
        SET account_state = $2,
            terminated_at = NULL,
            username_locked_until = NULL,
            updated_at = NOW()
      WHERE id = $1`,
    [userId, newState],
  );

  return { userId: String(userId), state: newState, keptUsername: stillHoldsUsername };
}

// Denies an appeal -- the real, symmetric counterpart to grantAppeal()
// above. Deliberately does NOT touch banned_identifiers/account_state:
// a denial changes nothing about the account's own real ban state, only
// the appeal's own record of having been reviewed and rejected.
export async function denyAppeal(appealId, resolution = '') {
  const { rows } = await query(
    `UPDATE ban_appeals SET status = 'denied', resolved_at = NOW(), resolution = $2
      WHERE id = $1 AND status = 'open'
      RETURNING user_id`,
    [appealId, resolution],
  );
  if (rows.length === 0) return null;
  return { userId: String(rows[0].user_id) };
}

// Claims a username for a user. Fails if it is taken, or still held under
// somebody else's termination lock.
export async function claimUsername(userId, username) {
  const trimmed = String(username || '').trim();
  if (!/^[A-Za-z0-9_]{3,20}$/.test(trimmed)) {
    return { ok: false, error: 'Usernames must be 3-20 characters, letters, numbers or underscore.' };
  }
  try {
    await query(
      `UPDATE users
          SET username = $2, username_lower = LOWER($2),
              account_state = CASE WHEN account_state = 'requires_rename' THEN 'active' ELSE account_state END,
              updated_at = NOW()
        WHERE id = $1`,
      [userId, trimmed],
    );
  } catch (err) {
    if (err.code === '23505') return { ok: false, error: 'That username is already taken.' };
    throw err;
  }
  return { ok: true, username: trimmed };
}

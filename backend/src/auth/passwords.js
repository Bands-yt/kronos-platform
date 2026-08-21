import crypto from 'node:crypto';
import { promisify } from 'node:util';

const scrypt = promisify(crypto.scrypt);

// scrypt, from Node's own standard library, rather than argon2id.
//
// argon2id is the current first choice on paper, but every Node binding
// for it is a native module -- a compilation step and a supply-chain
// dependency on a security-critical path. scrypt is memory-hard, is in
// the stdlib (nothing to compile, nothing extra to trust), and is an
// accepted password KDF in its own right. The parameters below are the
// part that actually determines strength, and they are tuned well above
// Node's defaults.
//
// N=2^15 with r=8 is ~32 MB of memory per hash. That is deliberately
// expensive: it is the whole point of a memory-hard KDF, and it bounds
// how fast an attacker with the leaked hashes can guess.
const PARAMS = { N: 32768, r: 8, p: 1, keylen: 64, maxmem: 96 * 1024 * 1024 };
const SALT_BYTES = 16;

export async function hashPassword(plaintext) {
  const salt = crypto.randomBytes(SALT_BYTES);
  const derived = await scrypt(plaintext, salt, PARAMS.keylen, PARAMS);
  // Self-describing format, so parameters can be raised later without
  // invalidating existing hashes -- verifyPassword reads them back out
  // of the stored string rather than assuming today's values.
  return `scrypt$${PARAMS.N}$${PARAMS.r}$${PARAMS.p}$${salt.toString('base64')}$${derived.toString('base64')}`;
}

export async function verifyPassword(plaintext, stored) {
  if (typeof stored !== 'string') return false;
  const parts = stored.split('$');
  if (parts.length !== 6 || parts[0] !== 'scrypt') return false;

  const N = Number(parts[1]);
  const r = Number(parts[2]);
  const p = Number(parts[3]);
  if (!Number.isInteger(N) || !Number.isInteger(r) || !Number.isInteger(p)) return false;

  let salt;
  let expected;
  try {
    salt = Buffer.from(parts[4], 'base64');
    expected = Buffer.from(parts[5], 'base64');
  } catch {
    return false;
  }

  let derived;
  try {
    derived = await scrypt(plaintext, salt, expected.length, { N, r, p, maxmem: PARAMS.maxmem });
  } catch {
    return false;
  }

  // Constant-time compare: a length-varying or short-circuiting compare
  // leaks how much of the hash matched, which is enough to attack.
  if (derived.length !== expected.length) return false;
  return crypto.timingSafeEqual(derived, expected);
}

// Deliberate work done on a login attempt for an email that does not
// exist. Without this, "no such user" returns in ~1 ms while a real user
// takes ~100 ms, and that timing difference alone lets anyone enumerate
// which email addresses have accounts.
const DUMMY_HASH_PROMISE = hashPassword(crypto.randomBytes(32).toString('hex'));
export async function burnTimingForUnknownUser(candidatePlaintext) {
  const dummy = await DUMMY_HASH_PROMISE;
  await verifyPassword(candidatePlaintext, dummy);
}

// Minimum viable password policy. Deliberately length-first: length is
// the property that actually resists guessing, whereas character-class
// rules mostly push people toward predictable substitutions.
export function validatePasswordStrength(password) {
  if (typeof password !== 'string') return 'Password is required.';
  if (password.length < 10) return 'Password must be at least 10 characters.';
  if (password.length > 512) return 'Password must be at most 512 characters.';
  const trivial = /^(.)\1+$/;
  if (trivial.test(password)) return 'Password must not be a single repeated character.';
  return null;
}

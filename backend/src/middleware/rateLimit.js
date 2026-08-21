import { redis, keys } from '../redis.js';
import { tooManyRequests } from '../errors.js';

// Fixed-window limiter backed by Redis, so the limit holds across every
// instance of this service rather than per-process.
//
// Applied hardest to the auth endpoints, which are the ones worth
// brute-forcing. Deliberately fails OPEN if Redis is unreachable: an
// outage in a defence-in-depth layer should degrade protection, not take
// login down for everyone.
export function rateLimit({ bucket, limit, windowSeconds, keyFn }) {
  return async (req, _res, next) => {
    const id = (keyFn ? keyFn(req) : req.ip) || 'unknown';
    const key = keys.rateLimit(bucket, id);
    try {
      const count = await redis.incr(key);
      if (count === 1) await redis.expire(key, windowSeconds);
      if (count > limit) return next(tooManyRequests(`Too many ${bucket} attempts. Try again shortly.`));
    } catch (err) {
      console.error('[ratelimit] Redis unavailable, allowing request: %s', err.message);
    }
    next();
  };
}

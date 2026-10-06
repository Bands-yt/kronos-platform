// Cron entry point that deletes expired session and one-shot tokens.
// Idempotent; suggested schedule: daily.
//   30 3 * * *  node /path/to/backend/scripts/purge-expired-tokens.js
import { pool } from '../src/db.js';
import { purgeExpiredTokens } from '../src/auth/tokens.js';

const removed = await purgeExpiredTokens();
console.log('[purge-expired-tokens] %s', Object.entries(removed).map(([t, n]) => `${t}=${n}`).join(' '));
await pool.end();
process.exit(0);

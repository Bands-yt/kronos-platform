// Cron entry point for the 30-day username recycle.
//
// Idempotent by construction (see recycleExpiredUsernames), so a missed
// run followed by a catch-up run is harmless and running it twice in the
// same minute does nothing the second time. Suggested schedule: hourly.
//   0 * * * *  node /path/to/backend/scripts/recycle-usernames.js
import { pool } from '../src/db.js';
import { recycleExpiredUsernames } from '../src/moderation/bans.js';

const released = await recycleExpiredUsernames();
console.log('[recycle-usernames] released %d handle(s)%s', released.length,
            released.length > 0 ? ` (user ids: ${released.join(', ')})` : '');
await pool.end();
process.exit(0);

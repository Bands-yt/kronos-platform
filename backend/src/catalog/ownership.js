import { query } from '../db.js';
import { forbidden, notFound } from '../errors.js';

// Shared by every route that acts on a specific game's assets (packages,
// chunks, versions, locks, dialogue) -- a slug must exist and be owned by
// the caller before anything under it can be touched. Moved out of
// catalog/routes.js so src/catalog/assets.js can use the exact same check
// rather than a second, drifting copy of it.
export async function requireOwnedGame(slug, userId) {
  const { rows } = await query(`SELECT id, creator_id FROM games WHERE slug = $1`, [slug]);
  if (rows.length === 0) throw notFound('No such game -- publish it first.');
  if (String(rows[0].creator_id) !== String(userId)) throw forbidden('You do not own this game.');
  return rows[0];
}

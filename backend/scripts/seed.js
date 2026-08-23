import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { pool, query } from '../src/db.js';

// Populates the catalogue from the REAL Kronos Studio projects checked
// into the engine repo's games/ directory -- not fabricated sample data.
//
// Each project directory (games/<Name>/) carries a real
// game.gamemanifest written by Studio's own publish flow (see
// engine/src/publishing/). This script reads NAME and DESCRIPTION
// straight out of that file rather than inventing marketing copy: if a
// manifest has no description, the catalogue row gets none either,
// matching games.description's own NOT NULL DEFAULT ''.
//
// Idempotent and safe to re-run: each project upserts on its slug (a
// second run after editing a manifest updates the existing row instead
// of erroring or duplicating it), exactly like schema.sql's own
// CREATE TABLE IF NOT EXISTS convention.
//
// Where the games/ directory lives is itself overridable
// (KRONOS_GAMES_DIR) rather than hardcoded, because this script only
// ever sees the filesystem it runs on -- there is no way for it to
// reach across to a different host's checkout.

const here = path.dirname(fileURLToPath(import.meta.url));
const defaultGamesDir = path.join(here, '..', '..', 'games');
const gamesDir = process.env.KRONOS_GAMES_DIR
  ? path.resolve(process.env.KRONOS_GAMES_DIR)
  : defaultGamesDir;

// Ad-hoc rows inserted by hand while building the storefront UI, before
// this script existed. Removed by slug (never a blanket DELETE/TRUNCATE)
// so this can never touch a real creator's published game that happens
// to already be in the table.
const PLACEHOLDER_SLUGS = ['sky-forge', 'deep-tunnels', 'quiet-town'];

// The system account real first-party sample projects publish under.
// NOT a real login -- password_hash stays NULL, exactly like a
// Google-only account, so it can never be password-brute-forced because
// there is no password to guess.
const SAMPLES_CREATOR_EMAIL = 'kronos-samples@kronos.local';
const SAMPLES_CREATOR_NAME = 'Kronos';

function slugify(name) {
  return name
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

// GAMEMANIFEST's format is flat "KEY value" lines terminated by END --
// see any games/*/game.gamemanifest for the real shape. GENRETAG may
// repeat; every other key is single-valued and last-write-wins if it
// somehow repeats too, which is a forgiving default for a hand-authored
// text file.
function parseManifest(text) {
  const fields = { genreTags: [] };
  for (const rawLine of text.split('\n')) {
    const line = rawLine.trim();
    if (!line || line === 'END' || line.startsWith('GAMEMANIFEST')) continue;
    const spaceIdx = line.indexOf(' ');
    const key = spaceIdx === -1 ? line : line.slice(0, spaceIdx);
    const value = spaceIdx === -1 ? '' : line.slice(spaceIdx + 1).trim();
    if (key === 'GENRETAG') {
      if (value) fields.genreTags.push(value);
    } else if (key === 'NAME') {
      fields.name = value;
    } else if (key === 'DESCRIPTION') {
      fields.description = value;
    }
  }
  return fields;
}

function discoverProjects() {
  if (!fs.existsSync(gamesDir)) {
    console.warn('[seed] games directory not found at "%s" -- nothing to scan. Set KRONOS_GAMES_DIR to override.', gamesDir);
    return [];
  }

  const projects = [];
  for (const entry of fs.readdirSync(gamesDir, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const manifestPath = path.join(gamesDir, entry.name, 'game.gamemanifest');
    if (!fs.existsSync(manifestPath)) continue; // not every directory here is necessarily a real project

    const parsed = parseManifest(fs.readFileSync(manifestPath, 'utf8'));
    if (!parsed.name) {
      console.warn('[seed] "%s" has a manifest with no NAME -- skipping.', entry.name);
      continue;
    }
    projects.push({
      directory: entry.name,
      slug: slugify(parsed.name),
      title: parsed.name,
      description: parsed.description || '',
      genreTags: parsed.genreTags,
    });
  }
  return projects;
}

async function main() {
  const projects = discoverProjects();
  console.log('[seed] scanning "%s" -- found %d real project(s)%s', gamesDir, projects.length,
    projects.length ? ': ' + projects.map((p) => p.directory).join(', ') : '');

  if (projects.length === 0) {
    console.log('[seed] nothing to seed. Placeholder rows (if any) are left untouched so the catalogue is not emptied.');
    await pool.end();
    return;
  }

  const creator = await query(
    `INSERT INTO users (email, email_lower, display_name, password_hash, email_verified)
     VALUES ($1, $1, $2, NULL, TRUE)
     ON CONFLICT (email_lower) DO UPDATE SET display_name = EXCLUDED.display_name
     RETURNING id`,
    [SAMPLES_CREATOR_EMAIL, SAMPLES_CREATOR_NAME],
  );
  const creatorId = creator.rows[0].id;

  for (const project of projects) {
    await query(
      `INSERT INTO games (slug, title, description, creator_id, thumbnail_url, published)
       VALUES ($1, $2, $3, $4, '', TRUE)
       ON CONFLICT (slug) DO UPDATE
         SET title = EXCLUDED.title,
             description = EXCLUDED.description,
             updated_at = NOW()`,
      [project.slug, project.title, project.description, creatorId],
    );
    console.log('[seed] upserted "%s" -> slug "%s"%s', project.title, project.slug,
      project.genreTags.length ? ` (${project.genreTags.join(', ')} -- not stored, no genre column yet)` : '');
  }

  const removed = await query(`DELETE FROM games WHERE slug = ANY($1::text[]) RETURNING slug`, [PLACEHOLDER_SLUGS]);
  if (removed.rows.length > 0) {
    console.log('[seed] removed %d placeholder row(s): %s', removed.rows.length, removed.rows.map((r) => r.slug).join(', '));
  }

  await pool.end();
}

main().catch(async (err) => {
  console.error('[seed] failed:', err);
  await pool.end();
  process.exit(1);
});

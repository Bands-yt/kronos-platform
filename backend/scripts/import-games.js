import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { pool } from '../src/db.js';

const backendDir = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const gamesDir = path.resolve(process.env.GAME_ROOT || path.join(backendDir, '..', 'games'));
const publicGamesPath = '/games';
const creatorEmail = process.env.GAMES_CREATOR_EMAIL || 'local-projects@kronos.local';
const creatorName = process.env.GAMES_CREATOR_NAME || 'Kronos Local Projects';

function parseManifest(text) {
  const values = new Map();
  for (const line of text.split(/\r?\n/)) {
    const match = line.match(/^([A-Z]+)\s*(.*)$/);
    if (!match) continue;
    const [, key, value] = match;
    if (key === 'GENRETAG') {
      values.set(key, `${values.get(key) || ''}${values.has(key) ? ',' : ''}${value.trim()}`);
    } else {
      values.set(key, value.trim());
    }
  }
  return values;
}

function slugify(value) {
  return value.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 64);
}

async function discoverProjects() {
  const entries = await fs.readdir(gamesDir, { withFileTypes: true });
  const projects = [];
  for (const entry of entries) {
    if (!entry.isDirectory()) continue;
    const root = path.join(gamesDir, entry.name);
    try {
      const manifest = parseManifest(await fs.readFile(path.join(root, 'game.gamemanifest'), 'utf8'));
      const name = manifest.get('NAME') || entry.name;
      const slug = slugify(name);
      if (!slug) continue;
      projects.push({
        slug,
        title: name,
        description: manifest.get('DESCRIPTION') || '',
        genre: manifest.get('GENRETAG') || '',
        thumbnailColor: manifest.get('THUMBNAILCOLOR') || '',
        projectUrl: `${publicGamesPath}/${encodeURIComponent(entry.name)}/${manifest.get('PROJECTPATH') || 'project.project'}`,
        manifestUrl: `${publicGamesPath}/${encodeURIComponent(entry.name)}/game.gamemanifest`,
      });
    } catch (err) {
      if (err.code !== 'ENOENT') throw err;
    }
  }
  return projects;
}

const projects = await discoverProjects();
if (process.argv.includes('--dry-run')) {
  console.log(JSON.stringify(projects, null, 2));
  await pool.end();
  process.exit(0);
}

const client = await pool.connect();
try {
  if (projects.length === 0) throw new Error(`No game.gamemanifest files found in ${gamesDir}`);

  await client.query('BEGIN');
  const { rows: creators } = await client.query(
    `INSERT INTO users (email, email_lower, email_verified, display_name)
     VALUES ($1, $2, TRUE, $3)
     ON CONFLICT (email_lower) DO UPDATE SET display_name = EXCLUDED.display_name, updated_at = NOW()
     RETURNING id`,
    [creatorEmail, creatorEmail.toLowerCase(), creatorName],
  );
  for (const project of projects) {
    await client.query(
      `INSERT INTO games
         (slug, title, description, creator_id, project_url, manifest_url, genre, thumbnail_color, published)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8, TRUE)
       ON CONFLICT (slug) DO UPDATE SET
         title = EXCLUDED.title, description = EXCLUDED.description, creator_id = EXCLUDED.creator_id,
         project_url = EXCLUDED.project_url, manifest_url = EXCLUDED.manifest_url,
         genre = EXCLUDED.genre, thumbnail_color = EXCLUDED.thumbnail_color,
         published = TRUE, updated_at = NOW()`,
      [project.slug, project.title, project.description, creators[0].id, project.projectUrl,
        project.manifestUrl, project.genre, project.thumbnailColor],
    );
  }
  await client.query('COMMIT');
  console.log('[import-games] published %d projects from %s', projects.length, gamesDir);
} catch (err) {
  await client.query('ROLLBACK');
  throw err;
} finally {
  client.release();
  await pool.end();
}

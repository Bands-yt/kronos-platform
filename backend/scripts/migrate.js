import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { pool } from '../src/db.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const dbDir = path.join(here, '..', 'db');

// Base schema first, then every numbered migration in order. Each file is
// written to be re-runnable (IF NOT EXISTS / ADD COLUMN IF NOT EXISTS), so
// this is safe to run against an already-migrated database.
const files = [path.join(dbDir, 'schema.sql')];
const migrationsDir = path.join(dbDir, 'migrations');
if (fs.existsSync(migrationsDir)) {
  for (const name of fs.readdirSync(migrationsDir).sort()) {
    if (name.endsWith('.sql')) files.push(path.join(migrationsDir, name));
  }
}

for (const file of files) {
  await pool.query(fs.readFileSync(file, 'utf8'));
  console.log('[migrate] applied %s', path.basename(file));
}
await pool.end();
process.exit(0);

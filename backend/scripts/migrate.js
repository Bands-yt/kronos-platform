import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { pool } from '../src/db.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const schema = fs.readFileSync(path.join(here, '..', 'db', 'schema.sql'), 'utf8');

await pool.query(schema);
console.log('[migrate] schema applied');
await pool.end();
process.exit(0);

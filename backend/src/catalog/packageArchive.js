// Server-side reader for the engine's .kronos archive (engine/src/publishing/
// PackageArchive.cpp): "KRAR", u32 version, u32 count, then per entry u32
// name length, name, u64 uncompressed size, u64 compressed size, zlib data.
// All integers little-endian.
import crypto from 'node:crypto';
import zlib from 'node:zlib';

const MAGIC = Buffer.from('KRAR');
const VERSION = 1;
export const MAX_ENTRIES = 100000;
const MAX_NAME_LENGTH = 4096;
const MAX_ENTRY_BYTES = 2 ** 30;
const MAX_TOTAL_BYTES = 4 * 2 ** 30;
// Text files the validator needs are inflated in memory; anything bigger is
// not a plausible manifest or project file.
const MAX_INSPECTED_TEXT_BYTES = 1024 * 1024;
export const MANIFEST_NAME = 'game.gamemanifest';

export class PackageError extends Error {}

export function isSafeRelativePath(p) {
  if (typeof p !== 'string' || p.length === 0 || p.startsWith('/') || /[\\:\0]/.test(p)) return false;
  return p.split('/').every((part) => part !== '' && part !== '.' && part !== '..');
}

function normalizePath(p) {
  const parts = [];
  for (const part of String(p).split('/')) {
    if (part === '' || part === '.') continue;
    if (part === '..') {
      if (parts.length === 0 || parts[parts.length - 1] === '..') parts.push('..');
      else parts.pop();
      continue;
    }
    parts.push(part);
  }
  return parts.join('/');
}

class StreamReader {
  constructor(readable, onChunk) {
    this.iterator = readable[Symbol.asyncIterator]();
    this.onChunk = onChunk;
    this.buffer = Buffer.alloc(0);
    this.done = false;
  }

  async fill(n) {
    while (this.buffer.length < n && !this.done) {
      const { value, done } = await this.iterator.next();
      if (done) {
        this.done = true;
        break;
      }
      const chunk = Buffer.isBuffer(value) ? value : Buffer.from(value);
      this.onChunk(chunk);
      this.buffer = this.buffer.length === 0 ? chunk : Buffer.concat([this.buffer, chunk]);
    }
    return this.buffer.length >= n;
  }

  async read(n) {
    if (!(await this.fill(n))) throw new PackageError('The package archive is truncated.');
    const out = this.buffer.subarray(0, n);
    this.buffer = this.buffer.subarray(n);
    return out;
  }

  async skip(n) {
    let remaining = n;
    while (remaining > 0) {
      if (this.buffer.length === 0 && !(await this.fill(1))) throw new PackageError('The package archive is truncated.');
      const take = Math.min(remaining, this.buffer.length);
      this.buffer = this.buffer.subarray(take);
      remaining -= take;
    }
  }

  async atEnd() {
    return !(await this.fill(1));
  }
}

function readU64(buf) {
  const value = buf.readBigUInt64LE(0);
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new PackageError('The package archive has an impossible entry size.');
  return Number(value);
}

// Streams the whole archive once: hashes every byte, checks the structure and
// every path, and keeps small text files that might be the manifest or project.
export async function inspectPackageStream(readable, { maxBytes = Infinity } = {}) {
  const hash = crypto.createHash('sha256');
  let sizeBytes = 0;
  const reader = new StreamReader(readable, (chunk) => {
    sizeBytes += chunk.length;
    if (sizeBytes > maxBytes) throw new PackageError(`The package exceeds the ${maxBytes}-byte size limit.`);
    hash.update(chunk);
  });

  const magic = await reader.read(4);
  if (!magic.equals(MAGIC)) throw new PackageError('This is not a Kronos package archive.');
  const version = (await reader.read(4)).readUInt32LE(0);
  if (version !== VERSION) throw new PackageError(`Unsupported package archive version ${version}.`);
  const count = (await reader.read(4)).readUInt32LE(0);
  if (count > MAX_ENTRIES) throw new PackageError(`A package may contain at most ${MAX_ENTRIES} files.`);

  const files = [];
  const seen = new Set();
  const texts = new Map();
  let uncompressedTotal = 0;
  for (let i = 0; i < count; i += 1) {
    const nameLength = (await reader.read(4)).readUInt32LE(0);
    if (nameLength > MAX_NAME_LENGTH) throw new PackageError('A file name in the package is too long.');
    const name = (await reader.read(nameLength)).toString('utf8');
    if (!isSafeRelativePath(name)) throw new PackageError(`The package contains an unsafe path: "${name.slice(0, 200)}".`);
    if (seen.has(name)) throw new PackageError(`The package contains "${name}" twice.`);
    seen.add(name);

    const uncompressedSize = readU64(await reader.read(8));
    const compressedSize = readU64(await reader.read(8));
    if (uncompressedSize > MAX_ENTRY_BYTES || compressedSize > MAX_ENTRY_BYTES) {
      throw new PackageError(`"${name}" is larger than the 1 GiB per-file limit.`);
    }
    uncompressedTotal += uncompressedSize;
    if (uncompressedTotal > MAX_TOTAL_BYTES) throw new PackageError('The package unpacks to more than 4 GiB.');

    const wanted = name === MANIFEST_NAME || name.endsWith('.project');
    if (wanted && uncompressedSize <= MAX_INSPECTED_TEXT_BYTES && compressedSize <= MAX_INSPECTED_TEXT_BYTES) {
      const compressed = await reader.read(compressedSize);
      let inflated;
      try {
        inflated = zlib.inflateSync(compressed, { maxOutputLength: MAX_INSPECTED_TEXT_BYTES });
      } catch {
        throw new PackageError(`"${name}" in the package is corrupt.`);
      }
      if (inflated.length !== uncompressedSize) throw new PackageError(`"${name}" in the package has the wrong size.`);
      texts.set(name, inflated.toString('utf8'));
    } else {
      await reader.skip(compressedSize);
    }
    files.push({ path: name, uncompressedSize, compressedSize });
  }
  if (!(await reader.atEnd())) throw new PackageError('The package archive has unexpected trailing data.');

  return { sha256: hash.digest('hex'), sizeBytes, files, texts, uncompressedBytes: uncompressedTotal };
}

function parseKeyValueFile(text, header) {
  const lines = text.split(/\r?\n/);
  if (!lines[0] || !lines[0].startsWith(header)) return null;
  const entries = [];
  for (const line of lines.slice(1)) {
    if (line === 'END') return entries;
    const space = line.indexOf(' ');
    entries.push(space < 0 ? [line, ''] : [line.slice(0, space), line.slice(space + 1)]);
  }
  return null;
}

// Mirrors engine::publishing::loadPackagedGame(): what a server or player will
// refuse to run is refused here first, before the version can go live.
export function validateGamePackage(inspection) {
  const manifestText = inspection.texts.get(MANIFEST_NAME);
  if (manifestText === undefined) throw new PackageError(`The package has no ${MANIFEST_NAME}.`);
  const manifestEntries = parseKeyValueFile(manifestText, 'GAMEMANIFEST');
  if (!manifestEntries) throw new PackageError(`The package's ${MANIFEST_NAME} could not be parsed.`);

  const manifest = { name: '', description: '', genre_tags: [], launch_kind: 'ProjectPath', project_path: '' };
  for (const [key, value] of manifestEntries) {
    if (key === 'NAME') manifest.name = value;
    else if (key === 'DESCRIPTION') manifest.description = value;
    else if (key === 'GENRETAG') manifest.genre_tags.push(value);
    else if (key === 'LAUNCHKIND') manifest.launch_kind = value === 'CliFlag' ? 'CliFlag' : 'ProjectPath';
    else if (key === 'PROJECTPATH') manifest.project_path = value;
  }
  if (manifest.launch_kind !== 'ProjectPath') throw new PackageError('Only scene-based games can be published to the catalog.');
  if (!manifest.name.trim()) throw new PackageError('The game manifest has no name.');
  if (manifest.name.length > 100) throw new PackageError('The game name in the manifest is longer than 100 characters.');

  const projectPath = normalizePath(manifest.project_path);
  if (!isSafeRelativePath(projectPath)) throw new PackageError('The manifest does not point at a project inside the package.');
  const fileSet = new Set(inspection.files.map((f) => f.path));
  if (!fileSet.has(projectPath)) throw new PackageError(`The project file "${projectPath}" is missing from the package.`);
  const projectText = inspection.texts.get(projectPath);
  if (projectText === undefined) throw new PackageError(`The project file "${projectPath}" is too large to be valid.`);
  const projectEntries = parseKeyValueFile(projectText, 'PROJECT');
  if (!projectEntries) throw new PackageError(`The project file "${projectPath}" could not be parsed.`);

  const scenes = [];
  let activeScene = 0;
  for (const [key, value] of projectEntries) {
    if (key === 'SCENE') scenes.push(value);
    else if (key === 'ACTIVESCENE') activeScene = Number.parseInt(value, 10);
  }
  if (scenes.length === 0) throw new PackageError('The project has no scenes.');
  if (!Number.isInteger(activeScene) || activeScene < 0 || activeScene >= scenes.length) {
    throw new PackageError('The project\'s active scene index is out of range.');
  }
  // Scene paths are relative to the package root (runtime::loadGame()).
  for (const scene of scenes) {
    if (!isSafeRelativePath(normalizePath(scene))) {
      throw new PackageError(`The project references a scene outside the package: "${scene}".`);
    }
  }
  if (!fileSet.has(normalizePath(scenes[activeScene]))) {
    throw new PackageError(`The starting scene "${scenes[activeScene]}" is missing from the package.`);
  }

  return {
    name: manifest.name,
    description: manifest.description,
    genre_tags: manifest.genre_tags.slice(0, 20),
    project_path: projectPath,
    scenes,
    active_scene: activeScene,
    file_count: inspection.files.length,
    uncompressed_bytes: inspection.uncompressedBytes,
  };
}

// Test and tooling helper: builds an archive the engine's readArchive() accepts.
export function buildArchive(entries) {
  const parts = [MAGIC];
  const header = Buffer.alloc(8);
  header.writeUInt32LE(VERSION, 0);
  header.writeUInt32LE(entries.length, 4);
  parts.push(header);
  for (const { path: name, data } of entries) {
    const nameBytes = Buffer.from(name, 'utf8');
    const body = Buffer.isBuffer(data) ? data : Buffer.from(data);
    const compressed = zlib.deflateSync(body);
    const meta = Buffer.alloc(4);
    meta.writeUInt32LE(nameBytes.length, 0);
    const sizes = Buffer.alloc(16);
    sizes.writeBigUInt64LE(BigInt(body.length), 0);
    sizes.writeBigUInt64LE(BigInt(compressed.length), 8);
    parts.push(meta, nameBytes, sizes, compressed);
  }
  return Buffer.concat(parts);
}

export function minimalGameArchive({ name = 'Test Game', extra = [] } = {}) {
  return buildArchive([
    { path: MANIFEST_NAME, data: `GAMEMANIFEST 1\nNAME ${name}\nLAUNCHKIND ProjectPath\nPROJECTPATH project.project\nEND\n` },
    { path: 'project.project', data: 'PROJECT 1\nNAME Test\nVERSION 1.0.0\nCREATED 0\nMODIFIED 0\nACTIVESCENE 0\nSCENE default.scene\nEND\n' },
    { path: 'default.scene', data: 'SCENE 1\nEND\n' },
    ...extra,
  ]);
}

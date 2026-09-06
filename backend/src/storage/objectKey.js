// Content-addressed key format shared by every storage backend (S3 and
// local disk alike) -- two creators publishing byte-identical archives
// land on the exact same key regardless of which backend is active, and a
// deployment can flip S3_BUCKET on/off without the key format itself
// changing underneath already-published games.
export function packageObjectKey(sha256Hex) {
  return `packages/${sha256Hex}.kronos`;
}

// Chunked Delta-Binary Sync: one global, content-addressed pool shared by
// every game and every version -- a chunk uploaded once for game A is
// already "have"-able by game B's sync-plan if the bytes happen to match,
// same real dedup benefit content-addressing already gives packages.
export function chunkObjectKey(sha256Hex) {
  return `chunks/${sha256Hex}.bin`;
}

// Crash telemetry: a minidump is content-addressed the same way, so two
// engine instances that hit the exact same crash never store it twice.
export function minidumpObjectKey(sha256Hex) {
  return `minidumps/${sha256Hex}.dmp`;
}

// Web Download portal: unlike packages/chunks/minidumps, a desktop app
// build is NOT content-addressed by this backend -- it is built and
// placed here by an out-of-band release process (same "does not live in
// this repo or on this server by default" convention download.js's own
// GitHub-Releases-backed installer already follows), so the key is
// deterministic (app/platform/version) rather than hash-derived. The
// real sha256 served in the manifest is computed server-side from
// whatever is actually sitting at this key (see catalog/downloads.js) --
// never trusted from the key or filename alone.
export function installerObjectKey(app, platform, version) {
  return `installers/${app}/${platform}/${version}.bin`;
}

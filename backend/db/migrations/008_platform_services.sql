-- v0.4.0: Chunked Delta-Binary Sync, collaborative scene/dialogue
-- metadata, native matchmaking/inventory/leaderboard services, and
-- crash telemetry. Same rules as every migration before this one: every
-- statement is IF NOT EXISTS / idempotent, because scripts/migrate.js
-- re-runs every file in db/migrations on every invocation rather than
-- tracking which ones already applied.

-- --- Chunked Delta-Binary Sync ----------------------------------------------
--
-- One global, content-addressed chunk pool (see storage/objectKey.js's
-- chunkObjectKey) shared across every game -- the same dedup reasoning
-- packages already got from content-addressing, just at chunk
-- granularity instead of whole-file.
CREATE TABLE IF NOT EXISTS asset_chunks (
    sha256      TEXT        PRIMARY KEY,
    object_key  TEXT        NOT NULL,
    size_bytes  BIGINT      NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- An immutable manifest: which chunks, in order, make up one real
-- publish of a game's assets. `chunk_sha256s` deliberately has no FK
-- constraint on its elements (Postgres arrays can't carry one) -- the
-- assets router verifies every referenced chunk really exists in
-- asset_chunks at version-creation time instead (see catalog/assets.js).
-- Rollback is just repointing games.current_asset_version_id at an
-- older row here: no bytes move, so it's real and instant.
CREATE TABLE IF NOT EXISTS asset_versions (
    id                BIGSERIAL   PRIMARY KEY,
    game_id           BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    version_number    INTEGER     NOT NULL,
    label             TEXT        NOT NULL DEFAULT '',
    chunk_sha256s     TEXT[]      NOT NULL,
    total_size_bytes  BIGINT      NOT NULL,
    created_by        BIGINT      NOT NULL REFERENCES users(id),
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (game_id, version_number)
);
CREATE INDEX IF NOT EXISTS asset_versions_game_idx ON asset_versions(game_id, version_number DESC);

ALTER TABLE games ADD COLUMN IF NOT EXISTS current_asset_version_id BIGINT REFERENCES asset_versions(id);

-- Perforce-style exclusive checkout on a named asset path within a
-- project. `expires_at` is what makes a lock real rather than a
-- permanent liability -- see config.assetLockTtlSeconds; the acquire
-- route's own UPSERT (ON CONFLICT ... WHERE expires_at <= NOW() OR
-- locked_by = the same caller) is what makes "expired, or mine already"
-- an atomic acquire rather than a race between a read and a write.
CREATE TABLE IF NOT EXISTS asset_locks (
    id          BIGSERIAL   PRIMARY KEY,
    game_id     BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    asset_path  TEXT        NOT NULL,
    locked_by   BIGINT      NOT NULL REFERENCES users(id),
    locked_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at  TIMESTAMPTZ NOT NULL,
    UNIQUE (game_id, asset_path)
);

-- --- Collaborative scene files ----------------------------------------------
--
-- A named-file namespace within a project's own version history --
-- `games.scene_sha256` (see 007_asset_streaming.sql) already covers the
-- single-monolithic-package case; a real Studio project has more than
-- one scene file, each independently editable (and lockable, via
-- asset_locks.asset_path using this same `path` string) by a different
-- collaborator.
CREATE TABLE IF NOT EXISTS scene_documents (
    id                  BIGSERIAL   PRIMARY KEY,
    game_id             BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    path                TEXT        NOT NULL,
    current_version_id  BIGINT      REFERENCES asset_versions(id),
    updated_by          BIGINT      REFERENCES users(id),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (game_id, path)
);

-- --- Audio/voiceover phoneme metadata ---------------------------------------
--
-- Data-only viseme/lip-sync timing storage -- no phoneme-generation
-- pipeline lives here (that's a real ML/audio-processing project of its
-- own); this is the storage + CRUD surface a real generator would write
-- into.
CREATE TABLE IF NOT EXISTS dialogue_lines (
    id                BIGSERIAL   PRIMARY KEY,
    game_id           BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    line_key          TEXT        NOT NULL,
    locale            TEXT        NOT NULL DEFAULT 'en',
    transcript        TEXT        NOT NULL DEFAULT '',
    audio_object_key  TEXT,
    audio_sha256      TEXT,
    duration_ms       INTEGER,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (game_id, line_key, locale)
);

CREATE TABLE IF NOT EXISTS dialogue_phonemes (
    id                BIGSERIAL PRIMARY KEY,
    dialogue_line_id  BIGINT    NOT NULL REFERENCES dialogue_lines(id) ON DELETE CASCADE,
    sequence_index    INTEGER   NOT NULL,
    phoneme           TEXT      NOT NULL,
    start_ms          INTEGER   NOT NULL,
    end_ms            INTEGER   NOT NULL,
    UNIQUE (dialogue_line_id, sequence_index)
);

-- --- Native matchmaking ------------------------------------------------------
--
-- Queueing itself lives in Redis (see redis.js's keys.matchmakingQueue) --
-- same "volatile, reconstructible state stays out of Postgres" rule
-- game_servers' own schema comment already applies to liveness. Only the
-- durable per-player rating survives here.
CREATE TABLE IF NOT EXISTS player_ratings (
    user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id     BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    rating      INTEGER     NOT NULL DEFAULT 1000,
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, game_id)
);

-- --- Real-time player inventories --------------------------------------------
--
-- item_catalog is a creator-defined economy (cosmetics, currencies,
-- etc.); inventory_items is what a specific player actually holds.
-- Granting/consuming is server-authoritative only (see inventory/
-- routes.js's own header comment) -- quantity can never go negative,
-- enforced here rather than trusted to application logic alone.
CREATE TABLE IF NOT EXISTS item_catalog (
    id          BIGSERIAL   PRIMARY KEY,
    game_id     BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    sku         TEXT        NOT NULL,
    name        TEXT        NOT NULL,
    kind        TEXT        NOT NULL DEFAULT 'item',
    metadata    JSONB       NOT NULL DEFAULT '{}',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (game_id, sku)
);

CREATE TABLE IF NOT EXISTS inventory_items (
    id          BIGSERIAL   PRIMARY KEY,
    user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id     BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    sku         TEXT        NOT NULL,
    quantity    BIGINT      NOT NULL DEFAULT 0 CHECK (quantity >= 0),
    metadata    JSONB       NOT NULL DEFAULT '{}',
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (user_id, game_id, sku)
);
CREATE INDEX IF NOT EXISTS inventory_items_user_game_idx ON inventory_items(user_id, game_id);

-- --- Global leaderboards ------------------------------------------------------
--
-- No separately-maintained rank column -- rank is computed at read time
-- with a window function (see leaderboards/routes.js), same "compute the
-- real truth, don't cache a stale one" convention catalog/routes.js's
-- own livePlayerCounts already established.
CREATE TABLE IF NOT EXISTS leaderboard_entries (
    game_id     BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    board_key   TEXT        NOT NULL,
    user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    score       BIGINT      NOT NULL,
    metadata    JSONB       NOT NULL DEFAULT '{}',
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (game_id, board_key, user_id)
);
CREATE INDEX IF NOT EXISTS leaderboard_entries_rank_idx ON leaderboard_entries(game_id, board_key, score DESC);

-- --- Telemetry & Minidump Handler --------------------------------------------
--
-- The dump's real bytes live content-addressed in storage (see
-- storage/objectKey.js's minidumpObjectKey), same "opaque blob does not
-- belong in Postgres" rule games.package_object_key already follows.
-- `status`/`stack_trace` reflect config.minidumpStackwalkPath's own
-- honest-no-op-when-unset convention (see telemetry/routes.js).
CREATE TABLE IF NOT EXISTS crash_reports (
    id              BIGSERIAL   PRIMARY KEY,
    sha256          TEXT        NOT NULL,
    object_key      TEXT        NOT NULL,
    size_bytes      BIGINT      NOT NULL,
    game_id         BIGINT      REFERENCES games(id) ON DELETE SET NULL,
    user_id         BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    engine_version  TEXT        NOT NULL DEFAULT '',
    platform        TEXT        NOT NULL DEFAULT '',
    status          TEXT        NOT NULL DEFAULT 'received'
                                 CHECK (status IN ('received', 'processing', 'processed', 'failed')),
    stack_trace     TEXT,
    received_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    processed_at    TIMESTAMPTZ
);
CREATE INDEX IF NOT EXISTS crash_reports_game_idx ON crash_reports(game_id, received_at DESC);

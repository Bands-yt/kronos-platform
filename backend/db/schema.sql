-- Kronos backend schema.
--
-- Design notes that matter for security, kept next to the thing they
-- constrain rather than in a separate document nobody reads:
--   * No column here ever stores a plaintext secret. Passwords are
--     scrypt-hashed; refresh tokens and password-reset tokens are stored
--     as SHA-256 digests, so a database leak does not hand an attacker
--     usable credentials.
--   * Every token row is independently revocable and independently
--     expirable, which is what makes "log out everywhere" and "this
--     reset link was already used" real rather than best-effort.

CREATE TABLE IF NOT EXISTS users (
    id              BIGSERIAL PRIMARY KEY,
    -- Citext would be nicer but needs an extension; lowercasing on write
    -- plus a unique index on the lowercased value is equivalent and has
    -- no extension dependency.
    email           TEXT        NOT NULL,
    email_lower     TEXT        NOT NULL UNIQUE,
    email_verified  BOOLEAN     NOT NULL DEFAULT FALSE,
    display_name    TEXT        NOT NULL,
    -- NULL for accounts that only ever sign in with Google. A user with
    -- no password cannot be password-brute-forced, which is a real
    -- security benefit of the OAuth path, not just a convenience.
    password_hash   TEXT,
    -- Google's stable subject claim. Unique so two accounts can never
    -- claim the same Google identity.
    google_sub      TEXT UNIQUE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    disabled_at     TIMESTAMPTZ
);

-- Refresh tokens. Stored hashed, rotated on every use, and revocable.
-- `replaced_by` lets us detect token reuse: if a token that has already
-- been rotated is presented again, that is a strong signal the token was
-- stolen, and the whole family gets revoked.
CREATE TABLE IF NOT EXISTS refresh_tokens (
    id           BIGSERIAL PRIMARY KEY,
    user_id      BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash   TEXT        NOT NULL UNIQUE,
    family_id    UUID        NOT NULL,
    issued_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at   TIMESTAMPTZ NOT NULL,
    revoked_at   TIMESTAMPTZ,
    replaced_by  TEXT,
    user_agent   TEXT
);
CREATE INDEX IF NOT EXISTS refresh_tokens_user_idx ON refresh_tokens(user_id);
CREATE INDEX IF NOT EXISTS refresh_tokens_family_idx ON refresh_tokens(family_id);

-- Single-use, time-limited, hashed password-reset tokens.
CREATE TABLE IF NOT EXISTS password_reset_tokens (
    id          BIGSERIAL PRIMARY KEY,
    user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash  TEXT        NOT NULL UNIQUE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at  TIMESTAMPTZ NOT NULL,
    used_at     TIMESTAMPTZ
);
CREATE INDEX IF NOT EXISTS password_reset_user_idx ON password_reset_tokens(user_id);

-- Email confirmation, same shape and same rules as password reset.
CREATE TABLE IF NOT EXISTS email_verification_tokens (
    id          BIGSERIAL PRIMARY KEY,
    user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash  TEXT        NOT NULL UNIQUE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at  TIMESTAMPTZ NOT NULL,
    used_at     TIMESTAMPTZ
);

-- The published game catalogue.
CREATE TABLE IF NOT EXISTS games (
    id             BIGSERIAL PRIMARY KEY,
    slug           TEXT        NOT NULL UNIQUE,
    title          TEXT        NOT NULL,
    description    TEXT        NOT NULL DEFAULT '',
    creator_id     BIGINT      NOT NULL REFERENCES users(id),
    thumbnail_url  TEXT        NOT NULL DEFAULT '',
    published      BOOLEAN     NOT NULL DEFAULT FALSE,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS games_published_idx ON games(published, created_at DESC);

-- Registered dedicated game servers.
--
-- Liveness deliberately does NOT live here -- it lives in Redis with a
-- TTL (see src/sessions/routes.js). A row in this table means "this
-- server is allowed to host"; a Redis heartbeat key means "this server
-- is alive right now". Conflating the two is how you end up allocating
-- players to a machine that died ten minutes ago.
CREATE TABLE IF NOT EXISTS game_servers (
    id            BIGSERIAL PRIMARY KEY,
    server_key    TEXT        NOT NULL UNIQUE,
    game_id       BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    host          TEXT        NOT NULL,
    port          INTEGER     NOT NULL,
    region        TEXT        NOT NULL DEFAULT 'unknown',
    max_players   INTEGER     NOT NULL DEFAULT 12,
    enabled       BOOLEAN     NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS game_servers_game_idx ON game_servers(game_id, enabled);

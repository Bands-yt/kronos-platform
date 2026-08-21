-- Kronos: social graph, guest accounts, and account lifecycle.

-- ---------------------------------------------------------------------------
-- Accounts: username, guest flag, lifecycle state
-- ---------------------------------------------------------------------------

-- Username is separate from display_name: display_name is cosmetic and
-- freely changed, username is the unique handle friends search by, and is
-- the thing subject to the 30-day lock/recycle rules below.
ALTER TABLE users ADD COLUMN IF NOT EXISTS username TEXT;
ALTER TABLE users ADD COLUMN IF NOT EXISTS username_lower TEXT;
ALTER TABLE users ADD COLUMN IF NOT EXISTS is_guest BOOLEAN NOT NULL DEFAULT FALSE;

-- Lifecycle state drives what a session is allowed to do.
--   active           -- normal
--   terminated       -- banned; username held under lock until username_locked_until
--   requires_rename  -- reinstated after the handle was recycled; must pick a
--                       new username before doing anything else
ALTER TABLE users ADD COLUMN IF NOT EXISTS account_state TEXT NOT NULL DEFAULT 'active';
ALTER TABLE users ADD COLUMN IF NOT EXISTS username_locked_until TIMESTAMPTZ;
ALTER TABLE users ADD COLUMN IF NOT EXISTS terminated_at TIMESTAMPTZ;

-- Email is nullable now: a guest account has no email at all, which is
-- the entire point of "no email required".
ALTER TABLE users ALTER COLUMN email DROP NOT NULL;
ALTER TABLE users ALTER COLUMN email_lower DROP NOT NULL;

-- Partial unique index rather than a plain UNIQUE: a recycled username is
-- set to NULL, and several NULLs must be allowed to coexist.
CREATE UNIQUE INDEX IF NOT EXISTS users_username_lower_key
    ON users(username_lower) WHERE username_lower IS NOT NULL;
CREATE INDEX IF NOT EXISTS users_username_search_idx ON users(username_lower text_pattern_ops);
CREATE INDEX IF NOT EXISTS users_username_lock_idx
    ON users(username_locked_until) WHERE username_locked_until IS NOT NULL;

-- ---------------------------------------------------------------------------
-- Friendships
-- ---------------------------------------------------------------------------

-- One row per relationship, not two. `requester_id`/`addressee_id` record
-- who initiated (which 'pending' needs in order to know who may accept),
-- while the CHECK below keeps the pair canonically ordered so the same
-- two people can never end up with two contradictory rows.
CREATE TABLE IF NOT EXISTS friendships (
    id            BIGSERIAL PRIMARY KEY,
    requester_id  BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    addressee_id  BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status        TEXT        NOT NULL CHECK (status IN ('pending', 'accepted', 'blocked')),
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    -- Nobody can befriend themselves.
    CONSTRAINT friendships_not_self CHECK (requester_id <> addressee_id)
);

-- The uniqueness that actually matters: at most ONE relationship per
-- unordered pair, enforced on the sorted pair so (A,B) and (B,A) collide.
CREATE UNIQUE INDEX IF NOT EXISTS friendships_unique_pair
    ON friendships (LEAST(requester_id, addressee_id), GREATEST(requester_id, addressee_id));

-- Bidirectional lookup: "everything involving me" hits an index from
-- either side.
CREATE INDEX IF NOT EXISTS friendships_requester_idx ON friendships(requester_id, status);
CREATE INDEX IF NOT EXISTS friendships_addressee_idx ON friendships(addressee_id, status);

-- ---------------------------------------------------------------------------
-- Multi-layer ban records
-- ---------------------------------------------------------------------------

-- Deliberately keyed by (kind, value_hash) rather than by user: a ban has
-- to outlive the account it came from, or deleting the account would undo
-- it. Values are stored HASHED -- an email address and an IP address are
-- both personal data, and this table only ever needs to answer "have I
-- seen this before?", which a hash answers perfectly well.
--
-- Note on hwid: hardware fingerprints are personal data under GDPR and are
-- defeated by a reinstall or a VM, so this is a speed bump for casual ban
-- evasion, not an identity system. It is stored hashed and given the same
-- expiry treatment as everything else here.
CREATE TABLE IF NOT EXISTS banned_identifiers (
    id          BIGSERIAL PRIMARY KEY,
    kind        TEXT        NOT NULL CHECK (kind IN ('email', 'hwid', 'ip')),
    value_hash  TEXT        NOT NULL,
    user_id     BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    reason      TEXT        NOT NULL DEFAULT '',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    -- NULL means permanent. An IP ban in particular should usually expire:
    -- addresses are reassigned, and a permanent one eventually punishes a
    -- stranger.
    expires_at  TIMESTAMPTZ,
    lifted_at   TIMESTAMPTZ
);
CREATE UNIQUE INDEX IF NOT EXISTS banned_identifiers_unique
    ON banned_identifiers(kind, value_hash) WHERE lifted_at IS NULL;
CREATE INDEX IF NOT EXISTS banned_identifiers_user_idx ON banned_identifiers(user_id);

-- ---------------------------------------------------------------------------
-- Ban appeals
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS ban_appeals (
    id           BIGSERIAL PRIMARY KEY,
    user_id      BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    body         TEXT        NOT NULL,
    status       TEXT        NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'granted', 'denied')),
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    resolved_at  TIMESTAMPTZ,
    resolution   TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS ban_appeals_user_idx ON ban_appeals(user_id, status);

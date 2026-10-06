-- Catalog releases: every confirmed package upload is a numbered version a
-- creator can roll back to, new games and versions can wait in a review
-- queue before players see them, uploads count against a per-account quota,
-- and games carry a reviewed thumbnail and screenshots.

CREATE TABLE IF NOT EXISTS game_package_versions (
    id                 BIGSERIAL PRIMARY KEY,
    game_id            BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    version_number     INTEGER     NOT NULL,
    sha256             TEXT        NOT NULL,
    object_key         TEXT        NOT NULL,
    size_bytes         BIGINT      NOT NULL,
    -- What validateGamePackage() read out of the archive at confirm time.
    manifest           JSONB       NOT NULL DEFAULT '{}'::jsonb,
    review_status      TEXT        NOT NULL DEFAULT 'approved'
                       CHECK (review_status IN ('pending', 'approved', 'rejected')),
    review_note        TEXT        NOT NULL DEFAULT '',
    reviewed_by        BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    reviewed_at        TIMESTAMPTZ,
    uploaded_by        BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    created_at         TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (game_id, version_number)
);
CREATE INDEX IF NOT EXISTS game_package_versions_review_idx ON game_package_versions(review_status, created_at);
CREATE INDEX IF NOT EXISTS game_package_versions_sha_idx ON game_package_versions(sha256);

ALTER TABLE games ADD COLUMN IF NOT EXISTS current_package_version_id BIGINT
    REFERENCES game_package_versions(id) ON DELETE SET NULL;
-- A game is listed once it has been approved at least once. Existing rows
-- predate review and stay listed.
ALTER TABLE games ADD COLUMN IF NOT EXISTS review_status TEXT NOT NULL DEFAULT 'approved';
ALTER TABLE games DROP CONSTRAINT IF EXISTS games_review_status_check;
ALTER TABLE games ADD CONSTRAINT games_review_status_check
    CHECK (review_status IN ('pending', 'approved', 'rejected'));

-- NULL means the service-wide default (config.packageQuotaBytes).
ALTER TABLE users ADD COLUMN IF NOT EXISTS storage_quota_bytes BIGINT;

CREATE TABLE IF NOT EXISTS game_media (
    id             BIGSERIAL PRIMARY KEY,
    game_id        BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    kind           TEXT        NOT NULL CHECK (kind IN ('thumbnail', 'screenshot')),
    sha256         TEXT        NOT NULL,
    object_key     TEXT        NOT NULL,
    content_type   TEXT        NOT NULL CHECK (content_type IN ('image/png', 'image/jpeg')),
    size_bytes     BIGINT      NOT NULL,
    width          INTEGER     NOT NULL,
    height         INTEGER     NOT NULL,
    position       INTEGER     NOT NULL DEFAULT 0,
    review_status  TEXT        NOT NULL DEFAULT 'approved'
                   CHECK (review_status IN ('pending', 'approved', 'rejected')),
    review_note    TEXT        NOT NULL DEFAULT '',
    reviewed_by    BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    reviewed_at    TIMESTAMPTZ,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS game_media_game_idx ON game_media(game_id, kind, position);
CREATE INDEX IF NOT EXISTS game_media_review_idx ON game_media(review_status, created_at);

-- Packages uploaded before versioning become version 1 of their game.
INSERT INTO game_package_versions (game_id, version_number, sha256, object_key, size_bytes, uploaded_by, created_at)
SELECT g.id, 1, g.scene_sha256, g.package_object_key, COALESCE(g.package_size_bytes, 0), g.creator_id,
       COALESCE(g.package_uploaded_at, g.updated_at)
  FROM games g
 WHERE g.package_object_key IS NOT NULL AND g.scene_sha256 IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM game_package_versions v WHERE v.game_id = g.id);
UPDATE games g
   SET current_package_version_id = v.id
  FROM game_package_versions v
 WHERE v.game_id = g.id AND v.version_number = 1 AND g.current_package_version_id IS NULL
   AND v.sha256 = g.scene_sha256;

ALTER TABLE moderation_actions ADD COLUMN IF NOT EXISTS package_version_id BIGINT
    REFERENCES game_package_versions(id) ON DELETE SET NULL;
ALTER TABLE moderation_actions ADD COLUMN IF NOT EXISTS media_id BIGINT REFERENCES game_media(id) ON DELETE SET NULL;
ALTER TABLE moderation_actions DROP CONSTRAINT IF EXISTS moderation_actions_action_type_check;
ALTER TABLE moderation_actions ADD CONSTRAINT moderation_actions_action_type_check CHECK (action_type IN (
    'terminate', 'grant_appeal', 'deny_appeal', 'resolve_report', 'dismiss_report',
    'resolve_queue_item', 'dismiss_queue_item', 'age_verify', 'age_unverify',
    'approve_game_version', 'reject_game_version', 'approve_game_media', 'reject_game_media'
));

-- Termination now revokes sessions; revoke the ones left over from before.
UPDATE refresh_tokens SET revoked_at = NOW()
 WHERE revoked_at IS NULL
   AND user_id IN (SELECT id FROM users WHERE account_state = 'terminated');

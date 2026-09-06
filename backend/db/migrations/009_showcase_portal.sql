-- Showcase portal: community-submitted creations (games, 3D materials,
-- audio drops, movie clips) that a creator can submit for feature
-- consideration (see catalog/showcase.js's own header comment).
-- `featured_at` is the real gate on public visibility: a submission
-- lands here with it NULL (pending) and GET /v1/showcase only ever
-- returns rows where it's set -- same "never show what hasn't really
-- been approved" reasoning content_reports/moderation_actions already
-- apply to reported content.
--
-- game_id is always populated (even when the submission was really
-- about a specific asset_version) -- an asset version always resolves
-- to a real game, and keeping that resolution server-side at submit
-- time (see the submit route) is what keeps the public read query a
-- single join instead of a conditional one.
CREATE TABLE IF NOT EXISTS showcase_entries (
    id                BIGSERIAL   PRIMARY KEY,
    title             TEXT        NOT NULL,
    description       TEXT        NOT NULL DEFAULT '',
    category          TEXT        NOT NULL
                                   CHECK (category IN ('games', '3d_materials', 'audio_dps', 'movie_clips')),
    game_id           BIGINT      NOT NULL REFERENCES games(id) ON DELETE CASCADE,
    asset_version_id  BIGINT      REFERENCES asset_versions(id) ON DELETE SET NULL,
    thumbnail_url     TEXT        NOT NULL DEFAULT '',
    -- Server-constructed at submit time (kronos://launch?game=<slug>...),
    -- never taken from the client -- same "authoritative, not
    -- client-claimed" reasoning display_name in verify-ticket already
    -- follows.
    kronos_uri        TEXT        NOT NULL DEFAULT '',
    likes_count       BIGINT      NOT NULL DEFAULT 0,
    submitted_by      BIGINT      NOT NULL REFERENCES users(id),
    featured_at       TIMESTAMPTZ,
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS showcase_entries_public_idx ON showcase_entries(category, featured_at DESC, id DESC);
CREATE INDEX IF NOT EXISTS showcase_entries_game_idx ON showcase_entries(game_id);

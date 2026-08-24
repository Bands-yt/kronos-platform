-- Kronos ops surface. Real per-server content scanning already exists
-- client-side (engine/src/safety/*, engine/src/moderation/*) -- what was
-- missing was a backend an ops team could actually use: a role to gate
-- admin routes with, somewhere centralized for a report to land (every
-- report/review-queue/escalation log today lives only on the single
-- game-server process that generated it), and an HTTP route surface for
-- the account termination/appeal logic that already existed in bans.js
-- but, before this, was only ever reachable from tests.

-- A plain role column, not a boolean is_admin: 'admin' is the only real
-- value today, but a text column with a real CHECK costs nothing extra
-- and doesn't need a second migration the day a 'moderator' tier (can
-- resolve reports, cannot terminate accounts) actually gets asked for.
-- Inline CHECK on the ADD COLUMN itself -- see scripts/migrate.js's own
-- comment on why every migration here must be safe to re-run: on a
-- second run ADD COLUMN IF NOT EXISTS is skipped entirely (the column
-- already exists), so this inline constraint never needs a separate
-- idempotent ADD CONSTRAINT statement.
ALTER TABLE users ADD COLUMN IF NOT EXISTS role TEXT NOT NULL DEFAULT 'user' CHECK (role IN ('user', 'admin'));

-- A real, backend-centralized report -- distinct from (and complementary
-- to, not a replacement for) each game server's own local ReportLog. Any
-- authenticated caller, including a guest, may file one: the person
-- experiencing harassment is exactly who guest-mode's own social-graph
-- restriction was never meant to silence.
CREATE TABLE IF NOT EXISTS content_reports (
    id                BIGSERIAL PRIMARY KEY,
    reporter_id       BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    reported_user_id  BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    -- Real, optional context -- which game/server this happened on, if
    -- the client had one to report (a profile-level report may have
    -- neither). game_id intentionally has no NOT NULL: a report against
    -- a player's profile/avatar isn't tied to any one game.
    game_id           BIGINT      REFERENCES games(id) ON DELETE SET NULL,
    server_key        TEXT,
    -- Matches the real category set engine::safety::TextClassifierStub
    -- already classifies chat against, plus a catch-all -- one shared
    -- vocabulary rather than the backend inventing a second taxonomy.
    category          TEXT        NOT NULL CHECK (category IN (
        'harassment', 'sexual_content', 'pii_solicitation', 'off_platform_redirect',
        'grooming', 'hate', 'self_harm', 'threats', 'spam', 'other'
    )),
    detail            TEXT        NOT NULL DEFAULT '',
    status            TEXT        NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'resolved', 'dismissed')),
    resolved_by       BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    resolution_note   TEXT        NOT NULL DEFAULT '',
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    resolved_at       TIMESTAMPTZ,
    CONSTRAINT content_reports_not_self CHECK (reporter_id <> reported_user_id)
);
-- The real ops queue query ("open reports, newest first") and the real
-- "does this player have a history" lookup both get their own index --
-- same reasoning follows_follower_idx/follows_followee_idx already
-- established for exactly this kind of two-real-query table.
CREATE INDEX IF NOT EXISTS content_reports_status_idx ON content_reports(status, created_at DESC);
CREATE INDEX IF NOT EXISTS content_reports_reported_user_idx ON content_reports(reported_user_id, created_at DESC);

-- A real, permanent audit trail of every admin action -- who did what to
-- whom and why, kept even after the report (if any) that prompted it is
-- long resolved. report_id is nullable: an admin can terminate an
-- account or grant an appeal with no report behind it at all (e.g. a
-- direct legal request), which is a real, honest case, not an error.
CREATE TABLE IF NOT EXISTS moderation_actions (
    id             BIGSERIAL PRIMARY KEY,
    admin_id       BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    target_user_id BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    action_type    TEXT        NOT NULL CHECK (action_type IN (
        'terminate', 'grant_appeal', 'deny_appeal', 'resolve_report', 'dismiss_report'
    )),
    reason         TEXT        NOT NULL DEFAULT '',
    report_id      BIGINT      REFERENCES content_reports(id) ON DELETE SET NULL,
    created_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE INDEX IF NOT EXISTS moderation_actions_target_idx ON moderation_actions(target_user_id, created_at DESC);

-- Automated/ML moderation queue, account-level behavioral signal inputs,
-- and content age-gating -- the three pieces of the roadmap's moderation
-- phase that content_reports/moderation_actions (006_moderation.sql)
-- don't already cover. content_reports is a HUMAN filing a complaint;
-- this table is a CLASSIFIER (Gemini text/vision today, a future
-- behavioral-signal job later) flagging something nobody has reported
-- yet. Deliberately a second table rather than reusing content_reports:
-- a queue row has no reporter_id (there is no reporter), and it needs a
-- classifier/content-type shape content_reports has no reason to carry.

-- Same real, live-lookup-on-every-check reasoning requireAdmin() already
-- uses for role: age-gating a piece of content has to see today's
-- verification state, not a token claim minted before a parent completed
-- verification (or before a TOS violation revoked it).
ALTER TABLE users ADD COLUMN IF NOT EXISTS birthdate DATE;
ALTER TABLE users ADD COLUMN IF NOT EXISTS age_verified BOOLEAN NOT NULL DEFAULT FALSE;

-- A game a creator has marked (or moderation has determined) as mature.
-- Self-reported at publish time today, same trust level as every other
-- games column a creator controls -- moderation_actions is where an
-- ops override of a creator's own claim would get audited, same as any
-- other moderation decision.
ALTER TABLE games ADD COLUMN IF NOT EXISTS mature BOOLEAN NOT NULL DEFAULT FALSE;

-- One queue, discriminated by content_type, rather than four
-- content-type-specific tables: every row goes through the exact same
-- review/appeal lifecycle regardless of what produced it, so the ops
-- queue (GET /v1/moderation/queue) can list and page all four together.
-- content_ref is a free-form pointer into whichever subsystem actually
-- owns the content (a chat message id, a voice transcript id, an asset
-- chunk sha256, an export job id) -- there is no single content table
-- these four could share a foreign key into.
CREATE TABLE IF NOT EXISTS moderation_queue (
    id                BIGSERIAL PRIMARY KEY,
    content_type      TEXT        NOT NULL CHECK (content_type IN (
        'chat', 'voice_transcript', 'ugc_asset', 'movie_export'
    )),
    content_ref       TEXT        NOT NULL DEFAULT '',
    flagged_user_id   BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    game_id           BIGINT      REFERENCES games(id) ON DELETE SET NULL,
    server_key        TEXT,
    -- Same shared vocabulary content_reports.category already uses (see
    -- that table's own comment), plus 'nsfw' and 'ip_violation' -- the
    -- two Gemini Vision-specific findings the text taxonomy has no
    -- category for.
    category          TEXT        NOT NULL CHECK (category IN (
        'harassment', 'sexual_content', 'pii_solicitation', 'off_platform_redirect',
        'grooming', 'hate', 'self_harm', 'threats', 'spam', 'nsfw', 'ip_violation', 'other'
    )),
    -- Mirrors engine::safety::ModerationVerdict's own real shape
    -- (isSafe/reasonCode/usedFallback/detail) -- this row IS a verdict,
    -- so it carries the same fields rather than inventing a parallel
    -- shape the two ends of the pipeline would have to be kept in sync.
    is_safe           BOOLEAN     NOT NULL,
    used_fallback     BOOLEAN     NOT NULL DEFAULT FALSE,
    detail            TEXT        NOT NULL DEFAULT '',
    -- Which pipeline produced this row -- 'gemini-flash-lite-latest',
    -- 'behavioral-signals', a future model name. Not constrained to a
    -- fixed set: a new classifier shouldn't need a migration to be able
    -- to write here.
    classifier        TEXT        NOT NULL DEFAULT '',
    status            TEXT        NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'resolved', 'dismissed')),
    reviewed_by       BIGINT      REFERENCES users(id) ON DELETE SET NULL,
    resolution_note   TEXT        NOT NULL DEFAULT '',
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    reviewed_at       TIMESTAMPTZ
);
-- Same two real query shapes content_reports_status_idx/
-- content_reports_reported_user_idx already exist for: the ops queue
-- ("open items, newest first") and "does this account have a history".
CREATE INDEX IF NOT EXISTS moderation_queue_status_idx ON moderation_queue(status, created_at DESC);
CREATE INDEX IF NOT EXISTS moderation_queue_flagged_user_idx ON moderation_queue(flagged_user_id, created_at DESC);

-- Lets a queue-item decision be audited exactly like a content_reports
-- decision -- report_id and queue_item_id are both nullable and mutually
-- exclusive in practice, never both set by the routes that write here.
ALTER TABLE moderation_actions ADD COLUMN IF NOT EXISTS queue_item_id BIGINT REFERENCES moderation_queue(id) ON DELETE SET NULL;

-- Widen the existing action_type CHECK to cover queue decisions. DROP
-- CONSTRAINT IF EXISTS + ADD CONSTRAINT (same name) is idempotent: a
-- second run drops exactly the constraint the first run added and
-- re-adds the identical one, so this is safe to re-run like every other
-- statement in this file even though it isn't an IF-NOT-EXISTS form.
ALTER TABLE moderation_actions DROP CONSTRAINT IF EXISTS moderation_actions_action_type_check;
ALTER TABLE moderation_actions ADD CONSTRAINT moderation_actions_action_type_check CHECK (action_type IN (
    'terminate', 'grant_appeal', 'deny_appeal', 'resolve_report', 'dismiss_report',
    'resolve_queue_item', 'dismiss_queue_item', 'age_verify', 'age_unverify'
));

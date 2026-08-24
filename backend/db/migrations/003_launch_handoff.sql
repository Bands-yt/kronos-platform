-- Kronos ("Open in Kronos" launch hand-off): a real, single-use bridge
-- from an authenticated browser session to a freshly-launched desktop
-- client process, which starts with no session of its own -- the
-- browser's own access_token lives in JS memory only (deliberately never
-- persisted, see backend/public/index.html's own comment on why) and is
-- not something a native process can ever read.
--
-- Same shape as password_reset_tokens/email_verification_tokens on
-- purpose: only the hash is stored (a leaked database does not hand out
-- working codes), and issueOneShotToken()/consumeOneShotToken() in
-- auth/tokens.js already implement the real, atomic single-use logic
-- generically over a table name -- this is a new table for them to
-- operate on, not new application logic.
CREATE TABLE IF NOT EXISTS launch_handoff_tokens (
    id          BIGSERIAL PRIMARY KEY,
    user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token_hash  TEXT        NOT NULL UNIQUE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at  TIMESTAMPTZ NOT NULL,
    used_at     TIMESTAMPTZ
);
CREATE INDEX IF NOT EXISTS launch_handoff_user_idx ON launch_handoff_tokens(user_id);

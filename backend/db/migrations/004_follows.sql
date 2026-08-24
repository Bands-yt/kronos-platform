-- One-way "Follow" (creators/players), deliberately separate from the
-- bidirectional friendships table (002_social_and_lifecycle.sql): a
-- follow needs no consent from the followee and carries no accept/
-- decline step, so it is a different real relationship, not a second
-- status value bolted onto friendships.
CREATE TABLE IF NOT EXISTS follows (
    id            BIGSERIAL PRIMARY KEY,
    follower_id   BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    followee_id   BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT follows_not_self CHECK (follower_id <> followee_id),
    CONSTRAINT follows_unique_pair UNIQUE (follower_id, followee_id)
);
-- "Who do I follow" and "who follows me" are both real, frequent
-- queries (a profile page needs the second one), so both directions get
-- their own index rather than relying on the unique constraint's
-- leading column alone.
CREATE INDEX IF NOT EXISTS follows_follower_idx ON follows(follower_id);
CREATE INDEX IF NOT EXISTS follows_followee_idx ON follows(followee_id);

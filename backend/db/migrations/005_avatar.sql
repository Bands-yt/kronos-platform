-- Backend persistence for a player's own avatar appearance -- Kronos
-- Avatar & Starter Marketplace Foundation. The actual data MODEL
-- (skin tone, head shape, body proportion sliders, clothing fit,
-- per-category equipped catalogue item ids) already exists and is rich
-- client-side (engine/src/core/AvatarLoadout.hpp, LocalProfile.hpp) --
-- this table is what was actually missing: nowhere for it to live
-- beyond a single machine's local disk. One row per user, upserted
-- whole -- an avatar config is small and always edited as a unit (the
-- client's own AvatarEditor/equip flow already treats it that way), so
-- there is no reason to split it into per-field update statements.
CREATE TABLE IF NOT EXISTS avatar_configs (
    user_id             BIGINT      PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    -- -1 means "never chosen -- use the real default" (kDefaultSkinToneColor),
    -- same convention core::LocalProfile::skinToneIndex already uses.
    skin_tone_index     INTEGER     NOT NULL DEFAULT -1,
    -- 0 = Oval, 1 = Sphere (core::HeadShape).
    head_shape_index    INTEGER     NOT NULL DEFAULT 0,
    -- Real procedural skeleton/mesh scale multipliers, clamped to
    -- [0.85, 1.15] by core::RiggedAvatar.hpp -- re-clamped server-side
    -- too, never trusting the client's own clamp.
    body_height         REAL        NOT NULL DEFAULT 1.0,
    body_width          REAL        NOT NULL DEFAULT 1.0,
    body_limb_scale     REAL        NOT NULL DEFAULT 1.0,
    body_torso_length   REAL        NOT NULL DEFAULT 1.0,
    body_shoulder_width REAL        NOT NULL DEFAULT 1.0,
    -- 0 = Tight, 1 = Loose (core::ClothingFit).
    clothing_fit_index  INTEGER     NOT NULL DEFAULT 0,
    -- {"Hair": "<itemId>", "Torso": "<itemId>", ...} -- one entry per
    -- occupied core::AvatarItemCategory slot, matching
    -- AvatarLoadout::equippedItems_ exactly (see avatar/routes.js's own
    -- ALLOWED_CATEGORIES for the fixed key set this is validated
    -- against). Item ids are NOT validated against a catalogue here --
    -- there is no backend-authoritative item catalogue yet (each client
    -- still keeps its own local catalogue.json); that is real,
    -- deliberately out-of-scope future marketplace-backend work.
    equipped_items      JSONB       NOT NULL DEFAULT '{}'::jsonb,
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

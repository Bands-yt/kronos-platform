-- Dynamic Asset Streaming: the S3-backed home for a published game's
-- real .kronos package archive. /v1/catalog/games/publish already
-- validated scene_sha256 as a real hex SHA-256 digest -- it was just
-- never stored anywhere (see catalog/routes.js's own header comment on
-- why the archive's bytes themselves stay out of Postgres). This adds
-- the columns that were the actual missing piece: where the real
-- uploaded object lives, how big it really is, and the hash that
-- content-addresses it.
ALTER TABLE games ADD COLUMN IF NOT EXISTS scene_sha256 TEXT;
ALTER TABLE games ADD COLUMN IF NOT EXISTS package_object_key TEXT;
ALTER TABLE games ADD COLUMN IF NOT EXISTS package_size_bytes BIGINT;
ALTER TABLE games ADD COLUMN IF NOT EXISTS package_uploaded_at TIMESTAMPTZ;

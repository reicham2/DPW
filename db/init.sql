CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS activities (
    id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    text        TEXT        NOT NULL DEFAULT '',
    title       TEXT        NOT NULL DEFAULT '',
    description TEXT        NOT NULL DEFAULT '',
    responsible TEXT        NOT NULL DEFAULT '',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Idempotenter Migration-Block für bestehende DB-Volumes
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                   WHERE table_name='activities' AND column_name='title') THEN
        ALTER TABLE activities ADD COLUMN title       TEXT NOT NULL DEFAULT '';
        ALTER TABLE activities ADD COLUMN description TEXT NOT NULL DEFAULT '';
        ALTER TABLE activities ADD COLUMN responsible TEXT NOT NULL DEFAULT '';
    END IF;
END
$$;

CREATE OR REPLACE FUNCTION touch_updated_at()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS trg_activities_updated_at ON activities;
CREATE TRIGGER trg_activities_updated_at
    BEFORE UPDATE ON activities
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_activities_created_at ON activities (created_at DESC);

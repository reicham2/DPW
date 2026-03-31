CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE activities (
    id         UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    text       TEXT        NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE OR REPLACE FUNCTION touch_updated_at()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$;

CREATE TRIGGER trg_activities_updated_at
    BEFORE UPDATE ON activities
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX idx_activities_created_at ON activities (created_at DESC);

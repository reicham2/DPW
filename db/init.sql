CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TYPE department_enum AS ENUM (
    'Jungschar', 'Pfadi', 'Rover', 'PTA', 'Leiter', 'Sonstige'
);

CREATE TABLE activities (
    id               UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    title            TEXT        NOT NULL,
    date             DATE        NOT NULL,
    start_time       TEXT        NOT NULL,
    end_time         TEXT        NOT NULL,
    goal             TEXT        NOT NULL,
    location         TEXT        NOT NULL,
    responsible      TEXT        NOT NULL,
    department       department_enum,
    material         TEXT[]      NOT NULL DEFAULT '{}',
    needs_siko       BOOLEAN     NOT NULL DEFAULT false,
    siko             BYTEA,
    bad_weather_info TEXT,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE programs (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id UUID NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    time        TEXT NOT NULL,
    title       TEXT NOT NULL,
    description TEXT NOT NULL,
    responsible TEXT NOT NULL
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

CREATE INDEX idx_activities_date ON activities (date DESC, start_time);
CREATE INDEX idx_programs_activity_id ON programs (activity_id);

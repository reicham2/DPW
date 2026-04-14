CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TYPE department_enum AS ENUM (
    'Leiter', 'Pio', 'Pfadi', 'Wölfe', 'Biber'
);

CREATE TYPE user_role AS ENUM (
    'admin', 'Stufenleiter', 'Leiter', 'Pio'
);

CREATE TABLE activities (
    id               UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    title            TEXT        NOT NULL,
    date             DATE        NOT NULL,
    start_time       TEXT        NOT NULL,
    end_time         TEXT        NOT NULL,
    goal             TEXT        NOT NULL,
    location         TEXT        NOT NULL,
    responsible      TEXT[]      NOT NULL DEFAULT '{}',
    department       department_enum,
    material         JSONB       NOT NULL DEFAULT '[]',
    siko_text        TEXT,
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
    responsible TEXT[] NOT NULL DEFAULT '{}'
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

CREATE TABLE users (
    id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    microsoft_oid TEXT        NOT NULL UNIQUE,
    email         TEXT        NOT NULL,
    display_name  TEXT        NOT NULL,
    department    department_enum,
    role          user_role   NOT NULL DEFAULT 'Leiter',
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX idx_users_microsoft_oid ON users (microsoft_oid);

-- Mail templates (one per department)
CREATE TABLE mail_templates (
    id          UUID            PRIMARY KEY DEFAULT gen_random_uuid(),
    department  department_enum NOT NULL UNIQUE,
    subject     TEXT            NOT NULL DEFAULT '',
    body        TEXT            NOT NULL DEFAULT '',
    recipients  TEXT[]          NOT NULL DEFAULT '{}',
    created_at  TIMESTAMPTZ     NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ     NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_mail_templates_updated_at
    BEFORE UPDATE ON mail_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Seed default (empty) templates for every department
INSERT INTO mail_templates (department, subject, body, recipients) VALUES
    ('Leiter', '', '', '{}'),
    ('Pio',    '', '', '{}'),
    ('Pfadi',  '', '', '{}'),
    ('Wölfe',  '', '', '{}'),
    ('Biber',  '', '', '{}');

-- Sent mail log (audit trail)
CREATE TABLE sent_mails (
    id           UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id  UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    sender_id    UUID        REFERENCES users(id) ON DELETE SET NULL,
    sender_email TEXT        NOT NULL,
    to_emails    TEXT[]      NOT NULL,
    subject      TEXT        NOT NULL,
    body_html    TEXT        NOT NULL,
    sent_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_sent_mails_activity_id ON sent_mails (activity_id);

-- Predefined locations
CREATE TABLE predefined_locations (
    id   UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name TEXT NOT NULL UNIQUE
);

INSERT INTO predefined_locations (name) VALUES
    ('Pfadiheim'),
    ('Schulhaus'),
    ('Wald'),
    ('Sportplatz'),
    ('Gemeindesaal');

-- Attachments
CREATE TABLE attachments (
    id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id   UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    filename      TEXT        NOT NULL,
    content_type  TEXT        NOT NULL DEFAULT 'application/octet-stream',
    data          BYTEA       NOT NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_attachments_activity_id ON attachments (activity_id);

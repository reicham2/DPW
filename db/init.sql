CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ── Dynamic departments & roles ─────────────────────────────────────────────

CREATE TABLE departments (
    name       TEXT PRIMARY KEY,
    color      TEXT NOT NULL DEFAULT '#6b7280',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE roles (
    name       TEXT PRIMARY KEY,
    color      TEXT NOT NULL DEFAULT '#6b7280',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

INSERT INTO departments (name, color) VALUES
    ('Allgemein', '#6b7280');

INSERT INTO roles (name, color) VALUES
    ('admin',    '#92400e'),
    ('Mitglied', '#6b7280');

-- ── Core tables ─────────────────────────────────────────────────────────────

CREATE TABLE activities (
    id               UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    title            TEXT        NOT NULL,
    date             DATE        NOT NULL,
    start_time       TEXT        NOT NULL,
    end_time         TEXT        NOT NULL,
    goal             TEXT        NOT NULL,
    location         TEXT        NOT NULL,
    responsible      TEXT[]      NOT NULL DEFAULT '{}',
    department       TEXT        REFERENCES departments(name) ON UPDATE CASCADE ON DELETE SET NULL,
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
    department    TEXT        REFERENCES departments(name) ON UPDATE CASCADE ON DELETE SET NULL,
    role          TEXT        NOT NULL DEFAULT 'Mitglied' REFERENCES roles(name) ON UPDATE CASCADE,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX idx_users_microsoft_oid ON users (microsoft_oid);

-- Mail templates (one per department)
CREATE TABLE mail_templates (
    id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    department  TEXT        NOT NULL UNIQUE REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    subject     TEXT        NOT NULL DEFAULT '',
    body        TEXT        NOT NULL DEFAULT '',
    recipients  TEXT[]      NOT NULL DEFAULT '{}',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_mail_templates_updated_at
    BEFORE UPDATE ON mail_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Seed default (empty) template for default department
INSERT INTO mail_templates (department, subject, body, recipients) VALUES
    ('Allgemein', '', '', '{}');

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

-- ── Role permissions ────────────────────────────────────────────────────────

CREATE TABLE role_permissions (
    role                 TEXT PRIMARY KEY REFERENCES roles(name) ON UPDATE CASCADE ON DELETE CASCADE,
    can_read_own_dept    BOOLEAN NOT NULL DEFAULT true,
    can_write_own_dept   BOOLEAN NOT NULL DEFAULT false,
    can_read_all_depts   BOOLEAN NOT NULL DEFAULT false,
    can_write_all_depts  BOOLEAN NOT NULL DEFAULT false,
    mail_send_scope      TEXT    NOT NULL DEFAULT 'none'
        CHECK (mail_send_scope IN ('none', 'own', 'same_dept', 'all')),
    mail_templates_scope TEXT    NOT NULL DEFAULT 'none'
        CHECK (mail_templates_scope IN ('none', 'own_dept', 'all')),
    user_dept_scope      TEXT    NOT NULL DEFAULT 'none'
        CHECK (user_dept_scope IN ('none', 'own', 'own_dept', 'all')),
    user_role_scope      TEXT    NOT NULL DEFAULT 'none'
        CHECK (user_role_scope IN ('none', 'own', 'own_dept', 'all'))
);

-- Cross-department access per role (beyond own department)
CREATE TABLE role_dept_access (
    role       TEXT NOT NULL REFERENCES roles(name) ON UPDATE CASCADE ON DELETE CASCADE,
    department TEXT NOT NULL REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    can_read   BOOLEAN NOT NULL DEFAULT false,
    can_write  BOOLEAN NOT NULL DEFAULT false,
    PRIMARY KEY (role, department)
);

-- Seed default role permissions
INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, can_read_all_depts, can_write_all_depts, mail_send_scope, mail_templates_scope, user_dept_scope, user_role_scope) VALUES
    ('admin',    true, true, true,  true,  'all', 'all', 'all', 'all'),
    ('Mitglied', true, true, false, false, 'own', 'none', 'none', 'none');

-- Triggers for departments & roles updated_at
CREATE TRIGGER trg_departments_updated_at
    BEFORE UPDATE ON departments
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TRIGGER trg_roles_updated_at
    BEFORE UPDATE ON roles
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

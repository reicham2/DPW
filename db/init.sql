CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- ── Dynamic departments & roles ─────────────────────────────────────────────

CREATE TABLE departments (
    name       TEXT PRIMARY KEY,
    color      TEXT NOT NULL DEFAULT '#6b7280',
    midata_group_id TEXT,
    midata_child_roles TEXT[] NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE roles (
    name       TEXT PRIMARY KEY,
    color      TEXT NOT NULL DEFAULT '#6b7280',
    sort_order INTEGER NOT NULL UNIQUE CHECK (sort_order >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

INSERT INTO departments (name, color) VALUES
    ('Allgemein', '#6b7280');

INSERT INTO roles (name, color, sort_order) VALUES
    ('admin',    '#92400e', 0),
    ('Mitglied', '#6b7280', 1);

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
    planned_participants_estimate INTEGER CHECK (planned_participants_estimate >= 0),
    midata_children_value INTEGER,
    midata_children_recorded_at TIMESTAMPTZ,
    weather_location TEXT,
    weather_snapshot JSONB,
    weather_recorded_at TIMESTAMPTZ,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE programs (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id      UUID NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    duration_minutes INTEGER NOT NULL DEFAULT 0 CHECK (duration_minutes >= 0),
    title            TEXT NOT NULL,
    description      TEXT NOT NULL,
    responsible      TEXT[] NOT NULL DEFAULT '{}'
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
    time_display_mode TEXT    NOT NULL DEFAULT 'minutes'
        CHECK (time_display_mode IN ('minutes', 'clock')),
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
    cc          TEXT[]      NOT NULL DEFAULT '{}',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_mail_templates_updated_at
    BEFORE UPDATE ON mail_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Seed default (empty) template for default department
INSERT INTO mail_templates (department, subject, body, recipients, cc) VALUES
    ('Allgemein', '', '', '{}', '{}');

-- Sent mail log (audit trail)
CREATE TABLE sent_mails (
    id           UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id  UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    sender_id    UUID        REFERENCES users(id) ON DELETE SET NULL,
    sender_email TEXT        NOT NULL,
    to_emails    TEXT[]      NOT NULL,
    cc_emails    TEXT[]      NOT NULL DEFAULT '{}',
    subject      TEXT        NOT NULL,
    body_html    TEXT        NOT NULL,
    sent_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_sent_mails_activity_id ON sent_mails (activity_id);

-- Predefined locations (global, shared across all departments)
CREATE TABLE predefined_locations (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name       TEXT NOT NULL UNIQUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TRIGGER trg_predefined_locations_updated_at
    BEFORE UPDATE ON predefined_locations
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

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

-- Activity share links (public read-only access)
CREATE TABLE activity_share_links (
    id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id   UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    share_token   TEXT        NOT NULL UNIQUE DEFAULT encode(gen_random_bytes(16), 'hex'),
    created_by    UUID        REFERENCES users(id) ON DELETE SET NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_activity_share_links_token ON activity_share_links (share_token);
CREATE INDEX idx_activity_share_links_activity ON activity_share_links (activity_id);

CREATE INDEX idx_attachments_activity_id ON attachments (activity_id);

-- ── Role permissions ────────────────────────────────────────────────────────

CREATE TABLE role_permissions (
    role                 TEXT PRIMARY KEY REFERENCES roles(name) ON UPDATE CASCADE ON DELETE CASCADE,
    can_read_own_dept    BOOLEAN NOT NULL DEFAULT true,
    can_write_own_dept   BOOLEAN NOT NULL DEFAULT false,
    can_read_all_depts   BOOLEAN NOT NULL DEFAULT false,
    can_write_all_depts  BOOLEAN NOT NULL DEFAULT false,
    activity_read_scope  TEXT    NOT NULL DEFAULT 'none'
        CHECK (activity_read_scope IN ('none', 'same_dept', 'all')),
    activity_create_scope TEXT   NOT NULL DEFAULT 'none'
        CHECK (activity_create_scope IN ('none', 'own_dept', 'all')),
    activity_edit_scope  TEXT    NOT NULL DEFAULT 'none'
        CHECK (activity_edit_scope IN ('none', 'own', 'same_dept', 'all')),
    mail_send_scope      TEXT    NOT NULL DEFAULT 'none'
        CHECK (mail_send_scope IN ('none', 'own', 'same_dept', 'all')),
    mail_templates_scope TEXT    NOT NULL DEFAULT 'none'
        CHECK (mail_templates_scope IN ('none', 'own_dept', 'all')),
    form_scope           TEXT    NOT NULL DEFAULT 'none'
        CHECK (form_scope IN ('none', 'own', 'same_dept', 'all')),
    form_templates_scope TEXT    NOT NULL DEFAULT 'none'
        CHECK (form_templates_scope IN ('none', 'own_dept', 'all')),
    user_dept_scope      TEXT    NOT NULL DEFAULT 'none'
        CHECK (user_dept_scope IN ('none', 'own', 'own_dept', 'all')),
    user_role_scope           TEXT    NOT NULL DEFAULT 'none'
        CHECK (user_role_scope IN ('none', 'own', 'own_dept', 'all')),
    locations_manage_scope   TEXT    NOT NULL DEFAULT 'none'
        CHECK (locations_manage_scope IN ('none', 'all'))
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
INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, can_read_all_depts, can_write_all_depts, activity_read_scope, activity_create_scope, activity_edit_scope, mail_send_scope, mail_templates_scope, form_scope, form_templates_scope, user_dept_scope, user_role_scope, locations_manage_scope) VALUES
    ('admin',    true, true, true,  true,  'all',       'all',      'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all'),
    ('Mitglied', true, true, false, false, 'same_dept', 'own_dept', 'own', 'own', 'none', 'own', 'none', 'none', 'none', 'none');

-- Triggers for departments & roles updated_at
CREATE TRIGGER trg_departments_updated_at
    BEFORE UPDATE ON departments
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TRIGGER trg_roles_updated_at
    BEFORE UPDATE ON roles
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- ── Forms system ─────────────────────────────────────────────────────────────

CREATE TABLE signup_forms (
    id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    public_slug TEXT        NOT NULL UNIQUE DEFAULT substr(encode(gen_random_bytes(12), 'hex'), 1, 20),
    form_type   TEXT        NOT NULL CHECK (form_type IN ('registration', 'deregistration')),
    title       TEXT        NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_by  UUID        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE (activity_id)
);

CREATE TRIGGER trg_signup_forms_updated_at
    BEFORE UPDATE ON signup_forms
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TABLE form_questions (
    id            UUID    PRIMARY KEY DEFAULT gen_random_uuid(),
    form_id       UUID    NOT NULL REFERENCES signup_forms(id) ON DELETE CASCADE,
    question_text TEXT    NOT NULL,
    question_type TEXT    NOT NULL CHECK (question_type IN ('section', 'text_input', 'single_choice', 'multiple_choice', 'dropdown')),
    position      INTEGER NOT NULL,
    is_required   BOOLEAN NOT NULL DEFAULT TRUE,
    metadata      JSONB   NOT NULL DEFAULT '{}',
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE form_responses (
    id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    form_id         UUID        NOT NULL REFERENCES signup_forms(id) ON DELETE CASCADE,
    submission_mode TEXT        NOT NULL CHECK (submission_mode IN ('registration', 'deregistration')),
    submitted_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    user_agent      TEXT,
    ip_address      TEXT
);

CREATE TABLE response_answers (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    response_id UUID NOT NULL REFERENCES form_responses(id) ON DELETE CASCADE,
    question_id UUID NOT NULL REFERENCES form_questions(id) ON DELETE CASCADE,
    answer_value TEXT,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE form_templates (
    id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    name            TEXT        NOT NULL,
    department      TEXT        NOT NULL REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    form_type       TEXT        NOT NULL CHECK (form_type IN ('registration', 'deregistration')),
    template_config JSONB       NOT NULL DEFAULT '[]',
    is_default      BOOLEAN     NOT NULL DEFAULT FALSE,
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    created_by      UUID        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE (department, form_type, name)
);

-- Ensure at most one default template per department
CREATE UNIQUE INDEX idx_form_templates_default
    ON form_templates (department)
    WHERE is_default = TRUE;

CREATE TRIGGER trg_form_templates_updated_at
    BEFORE UPDATE ON form_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX idx_signup_forms_activity    ON signup_forms(activity_id);
CREATE INDEX idx_signup_forms_public_slug ON signup_forms(public_slug);
CREATE INDEX idx_form_questions_form      ON form_questions(form_id);
CREATE INDEX idx_form_responses_form      ON form_responses(form_id);
CREATE INDEX idx_response_answers_resp    ON response_answers(response_id);
CREATE INDEX idx_form_templates_dept      ON form_templates(department);

-- ── Mail drafts (autosave) ──────────────────────────────────────────────────

CREATE TABLE mail_drafts (
    id           UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id  UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    recipients   TEXT[]      NOT NULL DEFAULT '{}',
    cc           TEXT[]      NOT NULL DEFAULT '{}',
    subject      TEXT        NOT NULL DEFAULT '',
    body_html    TEXT        NOT NULL DEFAULT '',
    updated_by   UUID        REFERENCES users(id) ON DELETE SET NULL,
    updated_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (activity_id)
);

CREATE TRIGGER trg_mail_drafts_updated_at
    BEFORE UPDATE ON mail_drafts
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX idx_mail_drafts_activity ON mail_drafts (activity_id);

CREATE TABLE form_drafts (
    id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id     UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    form_type       TEXT        NOT NULL DEFAULT 'registration',
    title           TEXT        NOT NULL DEFAULT '',
    questions_json  JSONB       NOT NULL DEFAULT '[]',
    updated_by      UUID        REFERENCES users(id) ON DELETE SET NULL,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (activity_id)
);

CREATE TRIGGER trg_form_drafts_updated_at
    BEFORE UPDATE ON form_drafts
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX idx_form_drafts_activity ON form_drafts (activity_id);

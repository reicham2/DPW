CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- Runtime app configuration values (stored from admin UI)
CREATE TABLE IF NOT EXISTS app_settings (
    key         TEXT PRIMARY KEY,
    is_secret   BOOLEAN NOT NULL DEFAULT true,
    value_text  TEXT,
    value_secret BYTEA,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CHECK (value_text IS NULL OR value_secret IS NULL)
);

INSERT INTO app_settings (key, is_secret, value_text, value_secret) VALUES
    ('midata.api_key', true, NULL, NULL),
    ('midata.api_url_template', false, 'https://db.scout.ch/de/groups/{group_id}/people.json', NULL),
    ('midata.api_timeout_ms', false, '8000', NULL),
    ('azure.tenant_id', false, NULL, NULL),
    ('azure.client_id', false, NULL, NULL),
    ('azure.client_secret', true, NULL, NULL),
    ('push.vapid_public_key', true, NULL, NULL),
    ('push.vapid_private_key', true, NULL, NULL),
    ('push.vapid_subject', false, 'mailto:admin@example.com', NULL),
    ('frontend.public_base_url', false, NULL, NULL),
    ('frontend.autosave_interval_ms', false, '1500', NULL),
    ('frontend.autosave_debounce', false, 'true', NULL),
    ('midata.weather_refresh_interval_ms', false, '900000', NULL),
    ('github.token', true, NULL, NULL),
    ('github.repo', false, 'reicham2/DPW', NULL),
    ('wp.url', true, NULL, NULL),
    ('wp.user', true, NULL, NULL),
    ('wp.app_password', true, NULL, NULL)
ON CONFLICT (key) DO NOTHING;

-- ── Dynamic departments & roles ─────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS departments (
    name       TEXT PRIMARY KEY,
    color      TEXT NOT NULL DEFAULT '#6b7280',
    midata_group_id TEXT,
    midata_child_roles TEXT[] NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS roles (
    name       TEXT PRIMARY KEY,
    color      TEXT NOT NULL DEFAULT '#6b7280',
    sort_order INTEGER NOT NULL UNIQUE CHECK (sort_order >= 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

INSERT INTO departments (name, color) VALUES
    ('Allgemein', '#6b7280')
ON CONFLICT (name) DO NOTHING;

INSERT INTO roles (name, color, sort_order) VALUES
    ('admin',    '#92400e', 0),
    ('Mitglied', '#6b7280', 1)
ON CONFLICT (name) DO NOTHING;

-- ── Core tables ─────────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS activities (
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

CREATE TABLE IF NOT EXISTS programs (
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

DROP TRIGGER IF EXISTS trg_activities_updated_at ON activities;
CREATE TRIGGER trg_activities_updated_at
    BEFORE UPDATE ON activities
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_activities_date ON activities (date DESC, start_time);
CREATE INDEX IF NOT EXISTS idx_programs_activity_id ON programs (activity_id);

CREATE TABLE IF NOT EXISTS ideenkiste (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    title            TEXT NOT NULL,
    duration_minutes INTEGER NOT NULL DEFAULT 0 CHECK (duration_minutes >= 0),
    description      TEXT NOT NULL DEFAULT '',
    department       TEXT REFERENCES departments(name) ON UPDATE CASCADE ON DELETE SET NULL,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_ideenkiste_updated_at ON ideenkiste;
CREATE TRIGGER trg_ideenkiste_updated_at
    BEFORE UPDATE ON ideenkiste
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_ideenkiste_department ON ideenkiste (department);

CREATE TABLE IF NOT EXISTS users (
    id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    microsoft_oid TEXT        NOT NULL UNIQUE,
    email         TEXT        NOT NULL,
    display_name  TEXT        NOT NULL,
    department    TEXT        REFERENCES departments(name) ON UPDATE CASCADE ON DELETE SET NULL,
    role          TEXT        NOT NULL DEFAULT 'Mitglied' REFERENCES roles(name) ON UPDATE CASCADE,
    time_display_mode TEXT    NOT NULL DEFAULT 'minutes'
        CHECK (time_display_mode IN ('minutes', 'clock')),
    notify_material_assigned BOOLEAN NOT NULL DEFAULT true,
    notify_activity_assigned BOOLEAN NOT NULL DEFAULT true,
    notify_program_assigned BOOLEAN NOT NULL DEFAULT true,
    notify_mail_own_activity BOOLEAN NOT NULL DEFAULT true,
    notify_mail_department BOOLEAN NOT NULL DEFAULT true,
    notify_channel_websocket BOOLEAN NOT NULL DEFAULT true,
    notify_channel_email BOOLEAN NOT NULL DEFAULT false,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_users_updated_at ON users;
CREATE TRIGGER trg_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_users_microsoft_oid ON users (microsoft_oid);

-- Mail templates (one per department)
CREATE TABLE IF NOT EXISTS mail_templates (
    id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    department  TEXT        NOT NULL UNIQUE REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    subject     TEXT        NOT NULL DEFAULT '',
    body        TEXT        NOT NULL DEFAULT '',
    recipients  TEXT[]      NOT NULL DEFAULT '{}',
    cc          TEXT[]      NOT NULL DEFAULT '{}',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_mail_templates_updated_at ON mail_templates;
CREATE TRIGGER trg_mail_templates_updated_at
    BEFORE UPDATE ON mail_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Seed default (empty) template for default department
INSERT INTO mail_templates (department, subject, body, recipients, cc) VALUES
    ('Allgemein', '', '', '{}', '{}')
ON CONFLICT (department) DO NOTHING;

-- Event templates (one per department, for WordPress event publishing)
CREATE TABLE IF NOT EXISTS event_templates (
    id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    department  TEXT        NOT NULL UNIQUE REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    title       TEXT        NOT NULL DEFAULT '',
    body        TEXT        NOT NULL DEFAULT '',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_event_templates_updated_at ON event_templates;
CREATE TRIGGER trg_event_templates_updated_at
    BEFORE UPDATE ON event_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Seed default (empty) event template for default department
INSERT INTO event_templates (department, title, body) VALUES
    ('Allgemein', '', '')
ON CONFLICT (department) DO NOTHING;

-- Event publications (tracks which activities have been published to WordPress)
CREATE TABLE IF NOT EXISTS event_publications (
    id             UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id    UUID        NOT NULL UNIQUE REFERENCES activities(id) ON DELETE CASCADE,
    published_by   UUID        REFERENCES users(id) ON DELETE SET NULL,
    title          TEXT        NOT NULL,
    body_html      TEXT        NOT NULL,
    wp_event_id    TEXT,
    published_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_event_publications_activity ON event_publications (activity_id);

-- Sent mail log (audit trail)
CREATE TABLE IF NOT EXISTS sent_mails (
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

CREATE INDEX IF NOT EXISTS idx_sent_mails_activity_id ON sent_mails (activity_id);

-- User notifications
CREATE TABLE IF NOT EXISTS notifications (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    category   TEXT NOT NULL CHECK (category IN (
        'material_assigned',
        'activity_assigned',
        'program_assigned',
        'mail_own_activity',
        'mail_department'
    )),
    title      TEXT NOT NULL,
    message    TEXT NOT NULL,
    link       TEXT,
    payload    JSONB NOT NULL DEFAULT '{}',
    is_read    BOOLEAN NOT NULL DEFAULT false,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_notifications_user_created ON notifications (user_id, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_notifications_user_unread ON notifications (user_id, is_read, created_at DESC);

-- Web push subscriptions (browser/app devices per user)
CREATE TABLE IF NOT EXISTS push_subscriptions (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id    UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    endpoint   TEXT NOT NULL UNIQUE,
    p256dh     TEXT NOT NULL,
    auth       TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_push_subscriptions_user ON push_subscriptions (user_id);

-- Predefined locations (global, shared across all departments)
CREATE TABLE IF NOT EXISTS predefined_locations (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name       TEXT NOT NULL UNIQUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_predefined_locations_updated_at ON predefined_locations;
CREATE TRIGGER trg_predefined_locations_updated_at
    BEFORE UPDATE ON predefined_locations
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

INSERT INTO predefined_locations (name) VALUES
    ('Pfadiheim'),
    ('Schulhaus'),
    ('Wald'),
    ('Sportplatz'),
    ('Gemeindesaal')
ON CONFLICT (name) DO NOTHING;

-- Attachments
CREATE TABLE IF NOT EXISTS attachments (
    id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id   UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    filename      TEXT        NOT NULL,
    content_type  TEXT        NOT NULL DEFAULT 'application/octet-stream',
    data          BYTEA       NOT NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Activity share links (public read-only access)
CREATE TABLE IF NOT EXISTS activity_share_links (
    id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id   UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    share_token   TEXT        NOT NULL UNIQUE DEFAULT encode(gen_random_bytes(16), 'hex'),
    created_by    UUID        REFERENCES users(id) ON DELETE SET NULL,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_activity_share_links_token ON activity_share_links (share_token);
CREATE INDEX IF NOT EXISTS idx_activity_share_links_activity ON activity_share_links (activity_id);

CREATE INDEX IF NOT EXISTS idx_attachments_activity_id ON attachments (activity_id);

-- ── Role permissions ────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS role_permissions (
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
    event_templates_scope TEXT   NOT NULL DEFAULT 'none'
        CHECK (event_templates_scope IN ('none', 'own_dept', 'all')),
    event_publish_scope  TEXT    NOT NULL DEFAULT 'none'
        CHECK (event_publish_scope IN ('none', 'own', 'own_dept', 'all')),
    user_dept_scope      TEXT    NOT NULL DEFAULT 'none'
        CHECK (user_dept_scope IN ('none', 'own', 'own_dept', 'all')),
    user_role_scope           TEXT    NOT NULL DEFAULT 'none'
        CHECK (user_role_scope IN ('none', 'own', 'own_dept', 'all')),
    locations_manage_scope   TEXT    NOT NULL DEFAULT 'none'
        CHECK (locations_manage_scope IN ('none', 'all')),
    ideenkiste_scope         TEXT    NOT NULL DEFAULT 'none'
        CHECK (ideenkiste_scope IN ('none', 'own_dept', 'all')),
    ideenkiste_add_scope     TEXT    NOT NULL DEFAULT 'none'
        CHECK (ideenkiste_add_scope IN ('none', 'own_dept', 'all')),
    ideenkiste_delete_scope  TEXT    NOT NULL DEFAULT 'none'
        CHECK (ideenkiste_delete_scope IN ('none', 'own_dept', 'all'))
);

-- Cross-department access per role (beyond own department)
CREATE TABLE IF NOT EXISTS role_dept_access (
    role       TEXT NOT NULL REFERENCES roles(name) ON UPDATE CASCADE ON DELETE CASCADE,
    department TEXT NOT NULL REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    can_read   BOOLEAN NOT NULL DEFAULT false,
    can_write  BOOLEAN NOT NULL DEFAULT false,
    PRIMARY KEY (role, department)
);

-- Seed default role permissions
INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, can_read_all_depts, can_write_all_depts, activity_read_scope, activity_create_scope, activity_edit_scope, mail_send_scope, mail_templates_scope, form_scope, form_templates_scope, user_dept_scope, user_role_scope, locations_manage_scope, ideenkiste_scope, ideenkiste_add_scope, ideenkiste_delete_scope) VALUES
    ('admin',    true, true, true,  true,  'all',       'all',      'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all'),
    ('Mitglied', true, true, false, false, 'same_dept', 'own_dept', 'own', 'own', 'none', 'own', 'none', 'none', 'none', 'none', 'none', 'none', 'none')
ON CONFLICT (role) DO NOTHING;

-- Triggers for departments & roles updated_at
DROP TRIGGER IF EXISTS trg_departments_updated_at ON departments;
CREATE TRIGGER trg_departments_updated_at
    BEFORE UPDATE ON departments
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS trg_roles_updated_at ON roles;
CREATE TRIGGER trg_roles_updated_at
    BEFORE UPDATE ON roles
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- ── Forms system ─────────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS signup_forms (
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

DROP TRIGGER IF EXISTS trg_signup_forms_updated_at ON signup_forms;
CREATE TRIGGER trg_signup_forms_updated_at
    BEFORE UPDATE ON signup_forms
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TABLE IF NOT EXISTS form_questions (
    id            UUID    PRIMARY KEY DEFAULT gen_random_uuid(),
    form_id       UUID    NOT NULL REFERENCES signup_forms(id) ON DELETE CASCADE,
    question_text TEXT    NOT NULL,
    question_type TEXT    NOT NULL CHECK (question_type IN ('section', 'text_input', 'single_choice', 'multiple_choice', 'dropdown')),
    position      INTEGER NOT NULL,
    is_required   BOOLEAN NOT NULL DEFAULT TRUE,
    metadata      JSONB   NOT NULL DEFAULT '{}',
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS form_responses (
    id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    form_id         UUID        NOT NULL REFERENCES signup_forms(id) ON DELETE CASCADE,
    submission_mode TEXT        NOT NULL CHECK (submission_mode IN ('registration', 'deregistration')),
    submitted_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    user_agent      TEXT,
    ip_address      TEXT
);

CREATE TABLE IF NOT EXISTS response_answers (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    response_id UUID NOT NULL REFERENCES form_responses(id) ON DELETE CASCADE,
    question_id UUID NOT NULL REFERENCES form_questions(id) ON DELETE CASCADE,
    answer_value TEXT,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS form_templates (
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
CREATE UNIQUE INDEX IF NOT EXISTS idx_form_templates_default
    ON form_templates (department)
    WHERE is_default = TRUE;

DROP TRIGGER IF EXISTS trg_form_templates_updated_at ON form_templates;
CREATE TRIGGER trg_form_templates_updated_at
    BEFORE UPDATE ON form_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_signup_forms_activity    ON signup_forms(activity_id);
CREATE INDEX IF NOT EXISTS idx_signup_forms_public_slug ON signup_forms(public_slug);
CREATE INDEX IF NOT EXISTS idx_form_questions_form      ON form_questions(form_id);
CREATE INDEX IF NOT EXISTS idx_form_responses_form      ON form_responses(form_id);
CREATE INDEX IF NOT EXISTS idx_response_answers_resp    ON response_answers(response_id);
CREATE INDEX IF NOT EXISTS idx_form_templates_dept      ON form_templates(department);

-- ── Mail drafts (autosave) ──────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS mail_drafts (
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

DROP TRIGGER IF EXISTS trg_mail_drafts_updated_at ON mail_drafts;
CREATE TRIGGER trg_mail_drafts_updated_at
    BEFORE UPDATE ON mail_drafts
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_mail_drafts_activity ON mail_drafts (activity_id);

CREATE TABLE IF NOT EXISTS form_drafts (
    id              UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id     UUID        NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    form_type       TEXT        NOT NULL DEFAULT 'registration',
    title           TEXT        NOT NULL DEFAULT '',
    questions_json  JSONB       NOT NULL DEFAULT '[]',
    updated_by      UUID        REFERENCES users(id) ON DELETE SET NULL,
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (activity_id)
);

DROP TRIGGER IF EXISTS trg_form_drafts_updated_at ON form_drafts;
CREATE TRIGGER trg_form_drafts_updated_at
    BEFORE UPDATE ON form_drafts
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- ── Schema sync for existing databases ─────────────────────────────────────
-- Additive ALTERs live in this file as well, so init.sql remains the single source of truth.

ALTER TABLE departments ADD COLUMN IF NOT EXISTS midata_group_id TEXT;
ALTER TABLE departments ADD COLUMN IF NOT EXISTS midata_child_roles TEXT[] NOT NULL DEFAULT '{}';

ALTER TABLE roles ADD COLUMN IF NOT EXISTS sort_order INTEGER;
WITH ranked_roles AS (
    SELECT name, ROW_NUMBER() OVER (ORDER BY name) - 1 AS rn
    FROM roles
)
UPDATE roles r
SET sort_order = rr.rn
FROM ranked_roles rr
WHERE r.name = rr.name
  AND r.sort_order IS NULL;
ALTER TABLE roles ALTER COLUMN sort_order SET NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS idx_roles_sort_order_unique ON roles (sort_order);

ALTER TABLE users ADD COLUMN IF NOT EXISTS time_display_mode TEXT;
UPDATE users SET time_display_mode = 'minutes' WHERE time_display_mode IS NULL;
ALTER TABLE users ALTER COLUMN time_display_mode SET DEFAULT 'minutes';
ALTER TABLE users ALTER COLUMN time_display_mode SET NOT NULL;

DO $$
BEGIN
    ALTER TABLE users DROP CONSTRAINT IF EXISTS users_time_display_mode_check;
    ALTER TABLE users ADD CONSTRAINT users_time_display_mode_check
        CHECK (time_display_mode IN ('minutes', 'clock'));
EXCEPTION
    WHEN OTHERS THEN
        NULL;
END $$;

ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_material_assigned BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_activity_assigned BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_program_assigned BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_mail_own_activity BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_mail_department BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_channel_websocket BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_channel_email BOOLEAN NOT NULL DEFAULT false;

ALTER TABLE activities ADD COLUMN IF NOT EXISTS material JSONB NOT NULL DEFAULT '[]';
ALTER TABLE activities ADD COLUMN IF NOT EXISTS siko_text TEXT;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS bad_weather_info TEXT;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS planned_participants_estimate INTEGER;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS midata_children_value INTEGER;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS midata_children_recorded_at TIMESTAMPTZ;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS weather_location TEXT;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS weather_snapshot JSONB;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS weather_recorded_at TIMESTAMPTZ;

DO $$
BEGIN
    ALTER TABLE notifications DROP CONSTRAINT IF EXISTS notifications_category_check;
    ALTER TABLE notifications ADD CONSTRAINT notifications_category_check
        CHECK (category IN (
            'material_assigned',
            'activity_assigned',
            'program_assigned',
            'mail_own_activity',
            'mail_department'
        ));
EXCEPTION
    WHEN OTHERS THEN
        NULL;
END $$;

CREATE INDEX IF NOT EXISTS idx_form_drafts_activity ON form_drafts (activity_id);

ALTER TABLE role_permissions ADD COLUMN IF NOT EXISTS ideenkiste_scope TEXT NOT NULL DEFAULT 'none'
    CHECK (ideenkiste_scope IN ('none', 'own_dept', 'all'));
ALTER TABLE role_permissions ADD COLUMN IF NOT EXISTS ideenkiste_add_scope TEXT NOT NULL DEFAULT 'none'
    CHECK (ideenkiste_add_scope IN ('none', 'own_dept', 'all'));
ALTER TABLE role_permissions ADD COLUMN IF NOT EXISTS ideenkiste_delete_scope TEXT NOT NULL DEFAULT 'none'
    CHECK (ideenkiste_delete_scope IN ('none', 'own_dept', 'all'));

CREATE TABLE IF NOT EXISTS ideenkiste (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    title            TEXT NOT NULL,
    duration_minutes INTEGER NOT NULL DEFAULT 0 CHECK (duration_minutes >= 0),
    description      TEXT NOT NULL DEFAULT '',
    department       TEXT REFERENCES departments(name) ON UPDATE CASCADE ON DELETE SET NULL,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_ideenkiste_updated_at ON ideenkiste;
CREATE TRIGGER trg_ideenkiste_updated_at
    BEFORE UPDATE ON ideenkiste
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_ideenkiste_department ON ideenkiste (department);

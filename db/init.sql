CREATE EXTENSION IF NOT EXISTS "pgcrypto";

-- Audit journal has no user/resource foreign keys: deletion must preserve history.
CREATE TABLE IF NOT EXISTS audit_events (
    id BIGSERIAL PRIMARY KEY,
    occurred_at TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
    finished_at TIMESTAMPTZ,
    actor_id TEXT,
    actor_name TEXT,
    action TEXT NOT NULL,
    resource_type TEXT NOT NULL,
    resource_id TEXT NOT NULL DEFAULT '',
    outcome TEXT NOT NULL DEFAULT 'pending'
        CHECK (outcome IN ('pending', 'completed', 'rejected', 'unknown')),
    http_status INTEGER CHECK (http_status BETWEEN 0 AND 599)
);
CREATE INDEX IF NOT EXISTS audit_events_actor_id_idx ON audit_events(actor_id, id DESC);
CREATE INDEX IF NOT EXISTS audit_events_outcome_idx ON audit_events(outcome, id DESC);
ALTER TABLE audit_events ADD COLUMN IF NOT EXISTS details JSONB NOT NULL DEFAULT '{}'::jsonb;
CREATE INDEX IF NOT EXISTS audit_events_occurred_at_idx ON audit_events(occurred_at);

CREATE OR REPLACE FUNCTION purge_audit_events(retention_days INTEGER) RETURNS INTEGER
LANGUAGE plpgsql AS $$
DECLARE deleted_count INTEGER;
BEGIN
    IF retention_days IS NULL OR retention_days < 1 OR retention_days > 3650 THEN
        RAISE EXCEPTION 'Audit retention must be between 1 and 3650 days';
    END IF;
    DELETE FROM audit_events WHERE occurred_at < NOW() - make_interval(days => retention_days);
    GET DIAGNOSTICS deleted_count = ROW_COUNT;
    RETURN deleted_count;
END;
$$;

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
    ('audit.retention_days', false, '14', NULL),
    ('midata.weather_refresh_interval_ms', false, '900000', NULL),
    ('github.token', true, NULL, NULL),
    ('github.repo', false, 'reicham2/DPW', NULL),
    ('wp.url', true, NULL, NULL),
    ('wp.user', true, NULL, NULL),
    ('wp.app_password', true, NULL, NULL),
    ('maintenance.enabled', false, 'false', NULL),
    ('maintenance.message', false, '', NULL),
    ('maintenance.scheduled_start', false, '', NULL),
    ('maintenance.scheduled_end', false, '', NULL),
    ('maintenance.windows_json', false, '[]', NULL)
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

-- Ensure sort_order exists before seeding roles (idempotent for existing DBs)
ALTER TABLE roles ADD COLUMN IF NOT EXISTS sort_order INTEGER;
WITH null_roles AS (
    SELECT name, ROW_NUMBER() OVER (ORDER BY name) AS rn
    FROM roles
    WHERE sort_order IS NULL
),
max_existing AS (
    SELECT COALESCE(MAX(sort_order), -1) AS max_so
    FROM roles
    WHERE sort_order IS NOT NULL
)
UPDATE roles r
SET sort_order = me.max_so + nr.rn
FROM null_roles nr
CROSS JOIN max_existing me
WHERE r.name = nr.name;
ALTER TABLE roles ALTER COLUMN sort_order SET NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS idx_roles_sort_order_unique ON roles (sort_order);

-- Seed default roles idempotently while keeping admin pinned at sort_order=0.
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM roles WHERE name = 'admin') THEN
        UPDATE roles SET color = '#92400e' WHERE name = 'admin';

        IF EXISTS (
            SELECT 1 FROM roles
            WHERE name = 'admin' AND sort_order <> 0
        ) THEN
            WITH zero_roles AS (
                SELECT name, ROW_NUMBER() OVER (ORDER BY name) AS rn
                FROM roles
                WHERE name <> 'admin' AND sort_order = 0
            ),
            max_existing AS (
                SELECT COALESCE(MAX(sort_order), -1) AS max_so
                FROM roles
                WHERE name <> 'admin' AND sort_order <> 0
            )
            UPDATE roles r
            SET sort_order = me.max_so + zr.rn
            FROM zero_roles zr
            CROSS JOIN max_existing me
            WHERE r.name = zr.name;

            UPDATE roles SET sort_order = 0 WHERE name = 'admin';
        END IF;
    ELSE
        WITH zero_roles AS (
            SELECT name, ROW_NUMBER() OVER (ORDER BY name) AS rn
            FROM roles
            WHERE sort_order = 0
        ),
        max_existing AS (
            SELECT COALESCE(MAX(sort_order), -1) AS max_so
            FROM roles
            WHERE sort_order <> 0
        )
        UPDATE roles r
        SET sort_order = me.max_so + zr.rn
        FROM zero_roles zr
        CROSS JOIN max_existing me
        WHERE r.name = zr.name;

        INSERT INTO roles (name, color, sort_order)
        VALUES ('admin', '#92400e', 0);
    END IF;
END;
$$;

INSERT INTO roles (name, color, sort_order)
SELECT 'Mitglied', '#6b7280', COALESCE(MAX(sort_order), -1) + 1
FROM roles
ON CONFLICT (name) DO UPDATE SET color = EXCLUDED.color;

-- ── Core tables ─────────────────────────────────────────────────────────────

CREATE TABLE IF NOT EXISTS activities (
    id               UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    title            TEXT        NOT NULL,
    date             DATE        NOT NULL,
    start_time       TEXT        NOT NULL,
    end_time         TEXT        NOT NULL,
    goal             TEXT        NOT NULL,
    guiding_thread   TEXT,
    location         TEXT        NOT NULL,
    responsible      TEXT[]      NOT NULL DEFAULT '{}',
    department       TEXT        REFERENCES departments(name) ON UPDATE CASCADE ON DELETE SET NULL,
    material         JSONB       NOT NULL DEFAULT '[]',
    tn_material      JSONB       NOT NULL DEFAULT '[]',
    siko_text        TEXT,
    bad_weather_info TEXT,
    planned_participants_estimate INTEGER CHECK (planned_participants_estimate >= 0),
    midata_children_value INTEGER,
    midata_children_recorded_at TIMESTAMPTZ,
    weather_location TEXT,
    weather_snapshot JSONB,
    weather_recorded_at TIMESTAMPTZ,
    deleted_at       TIMESTAMPTZ,
    deleted_by       UUID,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE activities ADD COLUMN IF NOT EXISTS deleted_at TIMESTAMPTZ;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS deleted_by UUID;
ALTER TABLE activities ADD COLUMN IF NOT EXISTS tn_material JSONB NOT NULL DEFAULT '[]';

CREATE TABLE IF NOT EXISTS programs (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id      UUID NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    duration_minutes INTEGER NOT NULL DEFAULT 0 CHECK (duration_minutes >= 0),
    title            TEXT NOT NULL,
    description      TEXT NOT NULL,
    responsible      TEXT[] NOT NULL DEFAULT '{}',
    position         INTEGER NOT NULL DEFAULT 0
);

-- Backfill position for databases created before the column existed.
ALTER TABLE programs ADD COLUMN IF NOT EXISTS position INTEGER NOT NULL DEFAULT 0;
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM programs WHERE position <> 0) THEN
        RETURN;
    END IF;
    UPDATE programs p
    SET position = ordered.rn - 1
    FROM (
        SELECT id, row_number() OVER (PARTITION BY activity_id ORDER BY ctid) AS rn
        FROM programs
    ) ordered
    WHERE p.id = ordered.id;
END $$;

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
CREATE INDEX IF NOT EXISTS idx_activities_deleted_at ON activities (deleted_at);
CREATE INDEX IF NOT EXISTS idx_programs_activity_id ON programs (activity_id, position);

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

-- A ledger outlives notification dismissal/expiry and isolates meaningful schedule changes.
ALTER TABLE activities ADD COLUMN IF NOT EXISTS reminder_revision BIGINT NOT NULL DEFAULT 0;

CREATE OR REPLACE FUNCTION activity_reminder_start(activity_date DATE, activity_time TEXT)
RETURNS TIMESTAMPTZ LANGUAGE SQL IMMUTABLE STRICT AS $$
    SELECT CASE WHEN activity_time ~ '^([01][0-9]|2[0-3]):[0-5][0-9](:[0-5][0-9])?$'
        THEN (activity_date + activity_time::time) AT TIME ZONE 'Europe/Zurich'
        ELSE NULL END;
$$;

CREATE OR REPLACE FUNCTION revise_activity_reminders()
RETURNS TRIGGER LANGUAGE plpgsql AS $$
BEGIN
    IF TG_OP = 'DELETE' THEN
        DELETE FROM notifications
        WHERE category = 'activity_reminder' AND payload->>'activity_id' = OLD.id::text;
        RETURN OLD;
    END IF;
    IF (NEW.date, NEW.start_time, NEW.department, NEW.deleted_at)
        IS DISTINCT FROM (OLD.date, OLD.start_time, OLD.department, OLD.deleted_at) THEN
        NEW.reminder_revision := OLD.reminder_revision + 1;
        DELETE FROM notifications
        WHERE category = 'activity_reminder' AND payload->>'activity_id' = OLD.id::text;
    END IF;
    RETURN NEW;
END;
$$;
DROP TRIGGER IF EXISTS trg_activity_reminder_revision ON activities;
CREATE TRIGGER trg_activity_reminder_revision BEFORE UPDATE OR DELETE ON activities
    FOR EACH ROW EXECUTE FUNCTION revise_activity_reminders();

CREATE OR REPLACE FUNCTION create_due_activity_reminders(
    check_at TIMESTAMPTZ DEFAULT NOW(), public_base_url TEXT DEFAULT '')
RETURNS INTEGER LANGUAGE plpgsql AS $$
DECLARE
    activity RECORD;
    recipient RECORD;
    note_id UUID;
    activity_link TEXT;
    created_count INTEGER := 0;
BEGIN
    -- Serialize concurrent scheduler processes, while still allowing normal activity edits.
    PERFORM pg_advisory_xact_lock(126, 1);
    IF EXISTS (SELECT 1 FROM activities a WHERE a.deleted_at IS NULL
        AND a.date BETWEEN (check_at AT TIME ZONE 'Europe/Zurich')::date
            AND (check_at AT TIME ZONE 'Europe/Zurich')::date + 2
        AND activity_reminder_start(a.date, a.start_time) IS NULL) THEN
        RAISE WARNING 'activity-reminders: invalid activity start times skipped';
    END IF;
    FOR activity IN
        SELECT a.*, activity_reminder_start(a.date, a.start_time) AS starts_at
        FROM activities a
        WHERE a.deleted_at IS NULL AND a.department IS NOT NULL
          AND activity_reminder_start(a.date, a.start_time) > check_at
          AND activity_reminder_start(a.date, a.start_time) - INTERVAL '48 hours' <= check_at
        ORDER BY a.id
        FOR SHARE OF a
    LOOP
        activity_link := rtrim(public_base_url, '/') || '/activities/' || activity.id;
        FOR recipient IN
            SELECT u.* FROM users u
            WHERE u.department = activity.department AND u.notify_activity_reminder
              AND activity.starts_at - make_interval(hours => u.activity_reminder_hours) <= check_at
              AND EXISTS (
                  SELECT 1 FROM role_permissions p WHERE p.role = u.role AND (
                      u.role = 'admin' OR p.activity_read_scope IN ('all', 'same_dept')
                      OR EXISTS (SELECT 1 FROM role_dept_access d
                          WHERE d.role = u.role AND d.department = activity.department AND d.can_read)
                  )
              )
            ORDER BY u.id
            FOR SHARE OF u
        LOOP
            INSERT INTO activity_reminders (activity_id, revision, user_id, next_attempt_at)
                VALUES (activity.id, activity.reminder_revision, recipient.id, check_at)
                ON CONFLICT DO NOTHING;
            IF NOT FOUND THEN CONTINUE; END IF;
            INSERT INTO notifications (user_id, category, title, message, link, payload, created_at)
            VALUES (recipient.id, 'activity_reminder', 'Erinnerung an deine nächste Aktivität',
                'Die Aktivität "' || activity.title || '" am ' || to_char(activity.date, 'DD.MM.YYYY')
                    || ' um ' || activity.start_time || ' steht bevor.',
                activity_link,
                jsonb_build_object(
                    'activity_id', activity.id, 'activity_title', activity.title,
                    'activity_date', activity.date, 'activity_date_display', to_char(activity.date, 'DD.MM.YYYY'),
                    'activity_time', activity.start_time || '-' || activity.end_time,
                    'activity_start_at', activity.starts_at, 'reminder_revision', activity.reminder_revision,
                    'reminder_hours', recipient.activity_reminder_hours,
                    'department', activity.department, 'location', activity.location,
                    'notification_recipient_name', recipient.display_name,
                    'notification_recipient_email', recipient.email,
                    'activity_url', activity_link, 'triggered_by_name', 'Server'),
                check_at) RETURNING id INTO note_id;
            UPDATE activity_reminders SET notification_id = note_id
                WHERE activity_id = activity.id AND revision = activity.reminder_revision AND user_id = recipient.id;
            created_count := created_count + 1;
        END LOOP;
    END LOOP;
    RETURN created_count;
END;
$$;

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
    notify_activity_reminder BOOLEAN NOT NULL DEFAULT true,
    activity_reminder_hours INTEGER NOT NULL DEFAULT 24 CHECK (activity_reminder_hours IN (2, 24, 48)),
    notify_program_assigned BOOLEAN NOT NULL DEFAULT true,
    notify_mail_own_activity BOOLEAN NOT NULL DEFAULT true,
    notify_mail_department BOOLEAN NOT NULL DEFAULT true,
    notify_channel_websocket BOOLEAN NOT NULL DEFAULT true,
    notify_channel_email BOOLEAN NOT NULL DEFAULT false,
    avatar_color  TEXT        NOT NULL DEFAULT '#4f8cff',
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_users_updated_at ON users;
CREATE TRIGGER trg_users_updated_at
    BEFORE UPDATE ON users
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE INDEX IF NOT EXISTS idx_users_microsoft_oid ON users (microsoft_oid);

CREATE TABLE IF NOT EXISTS proofreading_dictionary (
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    word TEXT NOT NULL CHECK (char_length(word) BETWEEN 1 AND 100 AND octet_length(word) <= 256),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, word)
);

ALTER TABLE users ADD COLUMN IF NOT EXISTS avatar_color TEXT NOT NULL DEFAULT '#4f8cff';

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'activities_deleted_by_fkey'
    ) THEN
        ALTER TABLE activities
            ADD CONSTRAINT activities_deleted_by_fkey
            FOREIGN KEY (deleted_by) REFERENCES users(id) ON DELETE SET NULL;
    END IF;
END;
$$;

-- Mail templates
CREATE TABLE IF NOT EXISTS mail_templates (
    id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    name        TEXT        NOT NULL DEFAULT 'Standard',
    department  TEXT        NOT NULL REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    subject     TEXT        NOT NULL DEFAULT '',
    body        TEXT        NOT NULL DEFAULT '',
    recipients  TEXT[]      NOT NULL DEFAULT '{}',
    cc          TEXT[]      NOT NULL DEFAULT '{}',
    is_default  BOOLEAN     NOT NULL DEFAULT FALSE,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE mail_templates ADD COLUMN IF NOT EXISTS name TEXT NOT NULL DEFAULT 'Standard';
ALTER TABLE mail_templates ADD COLUMN IF NOT EXISTS is_default BOOLEAN NOT NULL DEFAULT FALSE;

-- Migrate the former one-template-per-department rows to the department default.
DO $$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'mail_templates_department_key'
          AND conrelid = 'mail_templates'::regclass
    ) THEN
        UPDATE mail_templates SET is_default = TRUE;
        ALTER TABLE mail_templates DROP CONSTRAINT mail_templates_department_key;
    END IF;
END;
$$;

CREATE UNIQUE INDEX IF NOT EXISTS idx_mail_templates_default
    ON mail_templates (department)
    WHERE is_default = TRUE;
CREATE INDEX IF NOT EXISTS idx_mail_templates_department ON mail_templates(department);

DROP TRIGGER IF EXISTS trg_mail_templates_updated_at ON mail_templates;
CREATE TRIGGER trg_mail_templates_updated_at
    BEFORE UPDATE ON mail_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- Seed default (empty) template for default department
INSERT INTO mail_templates (name, department, subject, body, recipients, cc, is_default)
SELECT 'Standard', 'Allgemein', '', '', '{}', '{}', TRUE
WHERE NOT EXISTS (
    SELECT 1 FROM mail_templates WHERE department = 'Allgemein' AND is_default = TRUE
);

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
        'activity_reminder',
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

-- Expiry is enforced on reads even between scheduled physical cleanup runs.
CREATE OR REPLACE VIEW active_notifications AS
SELECT n.* FROM notifications n
WHERE NOT EXISTS (
    SELECT 1 FROM activities a
    WHERE n.payload ? 'activity_id'
      AND (n.payload->>'activity_id') = a.id::text
      AND NOW() >= ((a.date::timestamp) + INTERVAL '1 day')
);

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

-- Predefined material names (global, shared across all departments)
CREATE TABLE IF NOT EXISTS predefined_materials (
    id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    name       TEXT NOT NULL UNIQUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

DROP TRIGGER IF EXISTS trg_predefined_materials_updated_at ON predefined_materials;
CREATE TRIGGER trg_predefined_materials_updated_at
    BEFORE UPDATE ON predefined_materials
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

INSERT INTO predefined_materials (name) VALUES
    ('Einkaufen'),
    ('Pfadiheim'),
    ('Material')
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

-- Inline description images have their own parent, independent of replaced program rows.
CREATE TABLE IF NOT EXISTS program_images (
    id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
    activity_id   UUID        REFERENCES activities(id) ON DELETE CASCADE,
    ideenkiste_id UUID        REFERENCES ideenkiste(id) ON DELETE CASCADE,
    filename      TEXT        NOT NULL,
    content_type  TEXT        NOT NULL CHECK (content_type IN ('image/png', 'image/jpeg', 'image/gif', 'image/webp')),
    data          BYTEA       NOT NULL CHECK (octet_length(data) BETWEEN 1 AND 10485760),
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CHECK ((activity_id IS NULL) <> (ideenkiste_id IS NULL))
);

CREATE INDEX IF NOT EXISTS idx_program_images_activity ON program_images (activity_id);
CREATE INDEX IF NOT EXISTS idx_program_images_ideenkiste ON program_images (ideenkiste_id);

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
    materials_manage_scope   TEXT    NOT NULL DEFAULT 'none'
        CHECK (materials_manage_scope IN ('none', 'all')),
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

-- Ensure ideenkiste columns exist before seeding (idempotent for existing DBs)
ALTER TABLE role_permissions ADD COLUMN IF NOT EXISTS ideenkiste_scope TEXT NOT NULL DEFAULT 'none'
    CHECK (ideenkiste_scope IN ('none', 'own_dept', 'all'));
ALTER TABLE role_permissions ADD COLUMN IF NOT EXISTS ideenkiste_add_scope TEXT NOT NULL DEFAULT 'none'
    CHECK (ideenkiste_add_scope IN ('none', 'own_dept', 'all'));
ALTER TABLE role_permissions ADD COLUMN IF NOT EXISTS ideenkiste_delete_scope TEXT NOT NULL DEFAULT 'none'
    CHECK (ideenkiste_delete_scope IN ('none', 'own_dept', 'all'));
ALTER TABLE role_permissions ADD COLUMN IF NOT EXISTS materials_manage_scope TEXT NOT NULL DEFAULT 'none'
    CHECK (materials_manage_scope IN ('none', 'all'));

-- Seed default role permissions
INSERT INTO role_permissions (role, can_read_own_dept, can_write_own_dept, can_read_all_depts, can_write_all_depts, activity_read_scope, activity_create_scope, activity_edit_scope, mail_send_scope, mail_templates_scope, form_scope, form_templates_scope, user_dept_scope, user_role_scope, locations_manage_scope, materials_manage_scope, ideenkiste_scope, ideenkiste_add_scope, ideenkiste_delete_scope) VALUES
    ('admin',    true, true, true,  true,  'all',       'all',      'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all', 'all'),
    ('Mitglied', true, true, false, false, 'same_dept', 'own_dept', 'own', 'own', 'none', 'own', 'none', 'none', 'none', 'none', 'none', 'none', 'none', 'none')
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
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_activity_reminder BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS activity_reminder_hours INTEGER NOT NULL DEFAULT 24
    CHECK (activity_reminder_hours IN (2, 24, 48));
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_program_assigned BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_mail_own_activity BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_mail_department BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_channel_websocket BOOLEAN NOT NULL DEFAULT true;
ALTER TABLE users ADD COLUMN IF NOT EXISTS notify_channel_email BOOLEAN NOT NULL DEFAULT false;

ALTER TABLE activities ADD COLUMN IF NOT EXISTS material JSONB NOT NULL DEFAULT '[]';
ALTER TABLE activities ADD COLUMN IF NOT EXISTS guiding_thread TEXT;
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
            'activity_reminder',
            'program_assigned',
            'mail_own_activity',
            'mail_department'
        ));
END $$;

CREATE INDEX IF NOT EXISTS idx_form_drafts_activity ON form_drafts (activity_id);

CREATE TABLE IF NOT EXISTS activity_reminders (
    activity_id UUID NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    revision BIGINT NOT NULL,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    notification_id UUID REFERENCES notifications(id) ON DELETE SET NULL,
    browser_delivered BOOLEAN NOT NULL DEFAULT false,
    next_attempt_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (activity_id, revision, user_id)
);
CREATE INDEX IF NOT EXISTS idx_activity_reminders_pending
    ON activity_reminders (next_attempt_at) WHERE NOT browser_delivered;

CREATE OR REPLACE FUNCTION claim_activity_reminder_deliveries(check_at TIMESTAMPTZ DEFAULT NOW())
RETURNS SETOF notifications LANGUAGE SQL AS $$
    WITH pending AS (
        SELECT r.activity_id, r.revision, r.user_id
        FROM activity_reminders r
        JOIN activities a ON a.id = r.activity_id AND a.reminder_revision = r.revision
        JOIN users u ON u.id = r.user_id AND u.department = a.department
        JOIN notifications n ON n.id = r.notification_id
        WHERE NOT r.browser_delivered AND r.next_attempt_at <= check_at
          AND a.deleted_at IS NULL AND activity_reminder_start(a.date, a.start_time) > check_at
          AND u.notify_activity_reminder AND u.notify_channel_websocket AND NOT n.is_read
          AND EXISTS (SELECT 1 FROM role_permissions p WHERE p.role = u.role AND (
              u.role = 'admin' OR p.activity_read_scope IN ('all', 'same_dept')
              OR EXISTS (SELECT 1 FROM role_dept_access d
                  WHERE d.role = u.role AND d.department = a.department AND d.can_read)))
        ORDER BY r.next_attempt_at, r.activity_id, r.user_id
        LIMIT 20 FOR UPDATE OF r SKIP LOCKED
    ), claimed AS (
        UPDATE activity_reminders r SET next_attempt_at = check_at + INTERVAL '5 minutes'
        FROM pending p WHERE (r.activity_id, r.revision, r.user_id) = (p.activity_id, p.revision, p.user_id)
        RETURNING r.notification_id
    )
    SELECT n.* FROM notifications n JOIN claimed c ON c.notification_id = n.id;
$$;

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

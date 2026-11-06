#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
container="dpw-schema-test-$(date +%s)-$$"
started=0

cleanup() {
    if [ "$started" -eq 1 ] && docker inspect "$container" >/dev/null 2>&1; then
        if ! docker stop --time 3 "$container" >/dev/null; then
            echo "Could not stop test container $container" >&2
            exit 1
        fi
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

docker run --rm -d --name "$container" \
    -e POSTGRES_PASSWORD=temporary-test-password postgres:16-alpine >/dev/null
started=1

attempt=0
until docker exec -e PGPASSWORD=temporary-test-password "$container" \
    psql -X -q -h 127.0.0.1 -U postgres -d postgres -c 'SELECT 1' >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 240 ]; then
        echo "Temporary PostgreSQL container did not become ready" >&2
        docker logs "$container" >&2 || true
        exit 1
    fi
    sleep 0.25
done

{
    cat <<'SQL'
BEGIN;
CREATE TABLE activities (
    id UUID PRIMARY KEY,
    title TEXT NOT NULL,
    date DATE NOT NULL,
    start_time TEXT NOT NULL,
    end_time TEXT NOT NULL,
    goal TEXT NOT NULL,
    location TEXT NOT NULL,
    responsible TEXT[] NOT NULL DEFAULT '{}',
    department TEXT,
    material JSONB NOT NULL DEFAULT '[]',
    tn_material JSONB NOT NULL DEFAULT '[]',
    siko_text TEXT,
    bad_weather_info TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
CREATE TABLE programs (
    id UUID PRIMARY KEY,
    activity_id UUID NOT NULL REFERENCES activities(id) ON DELETE CASCADE,
    duration_minutes INTEGER NOT NULL DEFAULT 0,
    title TEXT NOT NULL,
    description TEXT NOT NULL,
    responsible TEXT[] NOT NULL DEFAULT '{}',
    position INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE users (
    id UUID PRIMARY KEY,
    microsoft_oid TEXT NOT NULL UNIQUE,
    email TEXT NOT NULL,
    display_name TEXT NOT NULL,
    department TEXT,
    role TEXT NOT NULL DEFAULT 'Mitglied',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
INSERT INTO users (id, microsoft_oid, email, display_name)
VALUES ('00000000-0000-0000-0000-000000000003', 'legacy-reminder-user',
        'legacy@example.invalid', 'Existing user');
INSERT INTO activities (id, title, date, start_time, end_time, goal, location, siko_text)
VALUES ('00000000-0000-0000-0000-000000000001', 'Bestehende Aktivität', '2026-07-01',
        '10:00', '11:00', 'Bestehendes Ziel', 'Wald', 'Sicherheitskonzept');
INSERT INTO programs (id, activity_id, duration_minutes, title, description, position)
VALUES ('00000000-0000-0000-0000-000000000002', '00000000-0000-0000-0000-000000000001',
        30, 'Programm', 'Beschreibung', 1);
CREATE TABLE departments (
    name TEXT PRIMARY KEY,
    color TEXT NOT NULL DEFAULT '#6b7280',
    midata_group_id TEXT,
    midata_child_roles TEXT[] NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
INSERT INTO departments (name) VALUES ('Legacy mail department');
CREATE TABLE mail_templates (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    department TEXT NOT NULL UNIQUE REFERENCES departments(name) ON UPDATE CASCADE ON DELETE CASCADE,
    subject TEXT NOT NULL DEFAULT '',
    body TEXT NOT NULL DEFAULT '',
    recipients TEXT[] NOT NULL DEFAULT '{}',
    cc TEXT[] NOT NULL DEFAULT '{}',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
INSERT INTO mail_templates (id, department, subject, body, recipients, cc)
VALUES ('00000000-0000-0000-0000-000000000004', 'Legacy mail department',
        'Custom legacy subject', '<p>Custom legacy body</p>',
        ARRAY['legacy@example.invalid'], ARRAY['copy@example.invalid']);
DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_schema = 'public' AND table_name = 'activities' AND column_name = 'guiding_thread'
    ) THEN
        RAISE EXCEPTION 'Legacy schema already contains the new column';
    END IF;
END $$;
SQL
    cat db/init.sql
    cat <<'SQL'
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM activities
        WHERE id = '00000000-0000-0000-0000-000000000001'
          AND title = 'Bestehende Aktivität'
          AND goal = 'Bestehendes Ziel'
          AND siko_text = 'Sicherheitskonzept'
          AND guiding_thread IS NULL
    ) THEN
        RAISE EXCEPTION 'Existing activity changed or new field is not null';
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM programs
        WHERE id = '00000000-0000-0000-0000-000000000002'
          AND title = 'Programm' AND description = 'Beschreibung' AND position = 1
    ) THEN
        RAISE EXCEPTION 'Existing program changed during schema sync';
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM users WHERE microsoft_oid = 'legacy-reminder-user'
            AND notify_activity_reminder AND activity_reminder_hours = 24
    ) THEN
        RAISE EXCEPTION 'Existing user did not receive the subscribed 24h reminder default';
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM mail_templates
        WHERE department = 'Legacy mail department'
          AND name = 'Standard' AND is_default
          AND subject = 'Custom legacy subject'
          AND body = '<p>Custom legacy body</p>'
          AND recipients = ARRAY['legacy@example.invalid']
          AND cc = ARRAY['copy@example.invalid']
    ) THEN
        RAISE EXCEPTION 'Existing mail template content was not preserved as the default';
    END IF;
END $$;
UPDATE activities SET guiding_thread = E'Erste Zeile\nZweite Zeile'
WHERE id = '00000000-0000-0000-0000-000000000001';
UPDATE users SET activity_reminder_hours = 2, notify_activity_reminder = false
WHERE microsoft_oid = 'legacy-reminder-user';
SQL
    cat db/init.sql
    cat <<'SQL'
DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM activities
        WHERE id = '00000000-0000-0000-0000-000000000001'
          AND title = 'Bestehende Aktivität'
          AND guiding_thread = E'Erste Zeile\nZweite Zeile'
    ) THEN
        RAISE EXCEPTION 'Repeated schema sync changed the guiding thread';
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM users WHERE microsoft_oid = 'legacy-reminder-user'
            AND NOT notify_activity_reminder AND activity_reminder_hours = 2
    ) THEN
        RAISE EXCEPTION 'Repeated schema sync changed the reminder preferences';
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM mail_templates
        WHERE department = 'Legacy mail department'
          AND name = 'Standard' AND is_default
          AND subject = 'Custom legacy subject'
          AND body = '<p>Custom legacy body</p>'
          AND recipients = ARRAY['legacy@example.invalid']
          AND cc = ARRAY['copy@example.invalid']
    ) THEN
        RAISE EXCEPTION 'Repeated schema sync changed the existing mail template';
    END IF;
END $$;
ROLLBACK;
SQL
} | docker exec -i -e PGPASSWORD=temporary-test-password \
    -e 'PGOPTIONS=-c client_min_messages=warning' "$container" \
    psql -X -q -v ON_ERROR_STOP=1 -h 127.0.0.1 -U postgres -d postgres

{
    printf 'BEGIN;\n'
    cat db/init.sql
    cat db/init.sql
    printf 'ROLLBACK;\n'
} | docker exec -i -e PGPASSWORD=temporary-test-password \
    -e 'PGOPTIONS=-c client_min_messages=warning' "$container" \
    psql -X -q -v ON_ERROR_STOP=1 -h 127.0.0.1 -U postgres -d postgres

echo "Schema upgrade and repeated sync passed for existing and fresh databases."

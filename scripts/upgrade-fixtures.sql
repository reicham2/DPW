-- Insert only after the OLD backend has initialized its own schema.
BEGIN;
INSERT INTO departments (name, color) VALUES ('Upgrade CI', '#123456');
INSERT INTO roles (name, color, sort_order)
SELECT 'Upgrade CI', '#123456', max(sort_order) + 1 FROM roles;
INSERT INTO role_permissions (role, activity_read_scope, activity_edit_scope)
VALUES ('Upgrade CI', 'same_dept', 'own');
INSERT INTO users (id, microsoft_oid, email, display_name, department, role)
VALUES ('15500000-0000-0000-0000-000000000001', 'upgrade-ci-user',
        'upgrade@example.invalid', 'Upgrade User', 'Upgrade CI', 'Upgrade CI');
INSERT INTO activities
    (id, title, date, start_time, end_time, goal, location, department, responsible, material)
VALUES ('15500000-0000-0000-0000-000000000002', 'Persisted activity',
        '2099-06-15', '10:00', '11:00', 'Keep existing data', 'Forest', 'Upgrade CI',
        ARRAY['15500000-0000-0000-0000-000000000001'],
        '[{"name":"Rope","responsible":["15500000-0000-0000-0000-000000000001"]}]');
INSERT INTO programs (id, activity_id, duration_minutes, title, description, position)
VALUES ('15500000-0000-0000-0000-000000000003', '15500000-0000-0000-0000-000000000002',
        30, 'Persisted program', 'Keep child relationships', 1);
INSERT INTO attachments (id, activity_id, filename, content_type, data)
VALUES ('15500000-0000-0000-0000-000000000004', '15500000-0000-0000-0000-000000000002',
        'upgrade.txt', 'text/plain', decode('000102ff', 'hex'));
INSERT INTO signup_forms (id, activity_id, public_slug, form_type, title, created_by)
VALUES ('15500000-0000-0000-0000-000000000005', '15500000-0000-0000-0000-000000000002',
        'upgrade-ci-form', 'registration', 'Persisted form', '15500000-0000-0000-0000-000000000001');
INSERT INTO form_questions (id, form_id, question_text, question_type, position)
VALUES ('15500000-0000-0000-0000-000000000006', '15500000-0000-0000-0000-000000000005',
        'Persisted question', 'text_input', 0);
INSERT INTO form_responses (id, form_id, submission_mode)
VALUES ('15500000-0000-0000-0000-000000000007', '15500000-0000-0000-0000-000000000005', 'registration');
INSERT INTO response_answers (id, response_id, question_id, answer_value)
VALUES ('15500000-0000-0000-0000-000000000008', '15500000-0000-0000-0000-000000000007',
        '15500000-0000-0000-0000-000000000006', 'Persisted answer');
UPDATE app_settings SET value_text = '2345' WHERE key = 'frontend.autosave_interval_ms';
UPDATE app_settings SET value_text = NULL,
    value_secret = pgp_sym_encrypt('upgrade-test-secret', :'encryption_key')
WHERE key = 'wp.app_password';

-- Keep the oracle outside public so schema sync cannot seed it afresh.
CREATE SCHEMA upgrade_test;
CREATE TABLE upgrade_test.expected (table_name text PRIMARY KEY, rows jsonb NOT NULL);
DO $$
DECLARE
    tbl text;
    snapshot jsonb;
BEGIN
    FOREACH tbl IN ARRAY ARRAY[
        'departments', 'roles', 'role_permissions', 'users', 'activities', 'programs',
        'attachments', 'signup_forms', 'form_questions', 'form_responses', 'response_answers'
    ] LOOP
        EXECUTE format(
            'SELECT jsonb_agg(to_jsonb(t) - ''updated_at'') FROM public.%I t', tbl)
        INTO snapshot;
        IF snapshot IS NULL THEN
            RAISE EXCEPTION 'Missing baseline fixture in %', tbl;
        END IF;
        INSERT INTO upgrade_test.expected VALUES (tbl, snapshot);
    END LOOP;
END $$;
INSERT INTO upgrade_test.expected
SELECT 'app_settings', jsonb_agg(to_jsonb(t) - 'updated_at')
FROM app_settings t WHERE key IN (
    'frontend.autosave_interval_ms', 'wp.app_password',
    'push.vapid_public_key', 'push.vapid_private_key'
);
COMMIT;

\set ON_ERROR_STOP on
BEGIN;

CREATE FUNCTION pg_temp.assert_event_template(condition BOOLEAN, message TEXT)
RETURNS VOID LANGUAGE plpgsql AS $$
BEGIN
    IF condition IS DISTINCT FROM true THEN RAISE EXCEPTION '%', message; END IF;
END;
$$;

DO $$
DECLARE
    department_name TEXT := 'Event template regression ' || gen_random_uuid()::text;
    original_template UUID;
    alternate_template UUID;
BEGIN
    INSERT INTO departments(name) VALUES (department_name);
    INSERT INTO event_templates (name, department, title, body, is_default)
    VALUES ('Standard', department_name, '{{titel}}', '<p>Bestehender Inhalt</p>', TRUE)
    RETURNING id INTO original_template;
    INSERT INTO event_templates (name, department, title, body, is_default)
    VALUES ('Abend', department_name, 'Abend: {{titel}}', '<p>Alternative</p>', FALSE)
    RETURNING id INTO alternate_template;
    PERFORM pg_temp.assert_event_template(
        (SELECT count(*) = 2 FROM event_templates WHERE department = department_name),
        'Multiple named Event templates must coexist in one department');
    PERFORM pg_temp.assert_event_template(
        (SELECT body = '<p>Bestehender Inhalt</p>' AND is_default FROM event_templates WHERE id = original_template),
        'Adding an alternative must preserve the original content and default');

    BEGIN
        UPDATE event_templates SET is_default = TRUE WHERE id = alternate_template;
        RAISE EXCEPTION 'The schema accepted two defaults in the same department';
    EXCEPTION WHEN unique_violation THEN
        NULL;
    END;
    UPDATE event_templates SET is_default = FALSE WHERE id = original_template;
    UPDATE event_templates SET is_default = TRUE WHERE id = alternate_template;
    PERFORM pg_temp.assert_event_template(
        (SELECT count(*) = 1 AND min(name) = 'Abend' FROM event_templates
         WHERE department = department_name AND is_default),
        'Exactly the chosen template must be the department default');
    UPDATE event_templates SET name = 'Umbenannt' WHERE id = original_template;
    PERFORM pg_temp.assert_event_template(
        (SELECT title = '{{titel}}' AND body = '<p>Bestehender Inhalt</p>' FROM event_templates WHERE id = original_template),
        'Renaming a template must preserve its Event content');
    DELETE FROM event_templates WHERE id = alternate_template;
    PERFORM pg_temp.assert_event_template(
        (SELECT count(*) = 1 FROM event_templates WHERE department = department_name)
        AND NOT EXISTS (SELECT 1 FROM event_templates WHERE department = department_name AND is_default),
        'Deleting the default must leave an explicit choice, not promote another template');
END;
$$;

ROLLBACK;
